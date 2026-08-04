#include "traps.hpp"
#include <cmath>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#include <algorithm>
#include <set>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <dirent.h>
#include <fcntl.h>
// Sockets. Every POSIX header mingw-w64 lacks is a socket header — the rest of
// this file's POSIX surface (dirent, fcntl, sys/stat, sys/time, unistd) is
// present and links. winsock2.h MUST precede windows.h, which is why it is here
// and not beside the other Windows includes.
#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
// After winsock2.h, never before: windows.h would otherwise drag in the
// original winsock.h and the two collide. Needed for the waitable-timer sleep
// (CreateWaitableTimerEx) and, on its fallback path, timeBeginPeriod.
#  include <windows.h>
#  include <mmsystem.h>
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#include <stdexcept>

using namespace guestmap;

// ---- sscanf: a from-scratch scanf engine over guest memory ----------------
// str/fmt are guest pointers (args 0,1); each non-suppressed conversion stores
// through the next guest vararg pointer. Returns the assignment count (or EOF).
namespace {
bool sws(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f'; }

uint32_t do_sscanf(Machine& m, uint32_t esp) {
    std::string in = m.cstr(TrapLayer::arg_at(m, esp, 0));
    std::string fmt = m.cstr(TrapLayer::arg_at(m, esp, 1));
    size_t ip = 0, fp = 0;
    int argidx = 2, assigned = 0;
    bool eof_before_assign = false;
    auto put = [&](uint32_t ptr, const void* p, uint32_t n) { m.write(ptr, p, n); };

    while (fp < fmt.size()) {
        char fc = fmt[fp];
        if (sws(fc)) { fp++; while (ip < in.size() && sws(in[ip])) ip++; continue; }
        if (fc != '%') {                       // literal
            if (ip < in.size() && in[ip] == fc) { ip++; fp++; continue; }
            break;
        }
        fp++;                                  // past '%'
        if (fp < fmt.size() && fmt[fp] == '%') {
            fp++;
            if (ip < in.size() && in[ip] == '%') { ip++; continue; }
            break;
        }
        bool suppress = false;
        if (fp < fmt.size() && fmt[fp] == '*') { suppress = true; fp++; }
        int width = 0;
        while (fp < fmt.size() && isdigit((unsigned char)fmt[fp]))
            width = width * 10 + (fmt[fp++] - '0');
        int len = 0;                           // 0=int 1=h(short) 2=hh(char) 3=l(long/double) 4=ll/q(8) 5=L
        if (fp < fmt.size()) {
            char lm = fmt[fp];
            if (lm=='h') { fp++; if (fp<fmt.size()&&fmt[fp]=='h') { fp++; len=2; } else len=1; }
            else if (lm=='l') { fp++; if (fp<fmt.size()&&fmt[fp]=='l') { fp++; len=4; } else len=3; }
            else if (lm=='q') { fp++; len=4; }
            else if (lm=='L') { fp++; len=5; }
        }
        if (fp >= fmt.size()) break;
        char conv = fmt[fp++];

        auto int_bytes = [&]() -> int {
            if (conv=='p') return 4;
            switch (len) { case 1: return 2; case 2: return 1; case 4: return 8; default: return 4; }
        };
        auto store_int = [&](unsigned long long v) {
            if (suppress) return;
            uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
            int nb = int_bytes();
            for (int i = 0; i < nb; ++i) { uint8_t b = (v >> (8*i)) & 0xff; put(ptr + i, &b, 1); }
            assigned++;
        };

        if (conv=='d'||conv=='i'||conv=='u'||conv=='x'||conv=='X'||conv=='o'||conv=='p') {
            while (ip < in.size() && sws(in[ip])) ip++;
            if (ip >= in.size()) { if (!assigned) eof_before_assign = true; break; }
            int base = (conv=='x'||conv=='X'||conv=='p') ? 16 : conv=='o' ? 8 : conv=='i' ? 0 : 10;
            std::string cand = in.substr(ip, width ? (size_t)width : std::string::npos);
            char* end = nullptr;
            unsigned long long v = std::strtoull(cand.c_str(), &end, base);
            size_t used = end - cand.c_str();
            if (used == 0) break;              // match failure
            ip += used;
            store_int(v);
        } else if (conv=='f'||conv=='e'||conv=='g'||conv=='E'||conv=='G'||conv=='a') {
            while (ip < in.size() && sws(in[ip])) ip++;
            if (ip >= in.size()) { if (!assigned) eof_before_assign = true; break; }
            std::string cand = in.substr(ip, width ? (size_t)width : std::string::npos);
            char* end = nullptr;
            double v = std::strtod(cand.c_str(), &end);
            size_t used = end - cand.c_str();
            if (used == 0) break;
            ip += used;
            if (!suppress) {
                uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                if (len==3||len==5) { double d=v; put(ptr,&d,8); }
                else { float f=(float)v; put(ptr,&f,4); }
                assigned++;
            }
        } else if (conv=='s') {
            while (ip < in.size() && sws(in[ip])) ip++;
            if (ip >= in.size()) { if (!assigned) eof_before_assign = true; break; }
            size_t start = ip;
            while (ip < in.size() && !sws(in[ip]) && (!width || (int)(ip-start) < width)) ip++;
            if (ip == start) break;
            if (!suppress) {
                uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                put(ptr, in.data()+start, (uint32_t)(ip-start));
                uint8_t z = 0; put(ptr + (ip-start), &z, 1);
                assigned++;
            }
        } else if (conv=='c') {
            int w = width ? width : 1;
            if (ip >= in.size()) { if (!assigned) eof_before_assign = true; break; }
            int n = 0; size_t start = ip;
            while (ip < in.size() && n < w) { ip++; n++; }
            if (!suppress) {
                uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                put(ptr, in.data()+start, (uint32_t)n);
                assigned++;
            }
        } else if (conv=='[') {
            bool neg = false;
            if (fp < fmt.size() && fmt[fp]=='^') { neg = true; fp++; }
            bool set[256] = {false};
            bool first = true;
            while (fp < fmt.size() && (fmt[fp] != ']' || first)) {
                if (fp+2 < fmt.size() && fmt[fp+1]=='-' && fmt[fp+2]!=']') {
                    for (int c=(unsigned char)fmt[fp]; c<=(unsigned char)fmt[fp+2]; ++c) set[c]=true;
                    fp += 3;
                } else { set[(unsigned char)fmt[fp]] = true; fp++; }
                first = false;
            }
            if (fp < fmt.size() && fmt[fp]==']') fp++;
            size_t start = ip;
            while (ip < in.size() && (set[(unsigned char)in[ip]] != neg) &&
                   (!width || (int)(ip-start) < width)) ip++;
            if (ip == start) break;
            if (!suppress) {
                uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                put(ptr, in.data()+start, (uint32_t)(ip-start));
                uint8_t z = 0; put(ptr + (ip-start), &z, 1);
                assigned++;
            }
        } else if (conv=='n') {
            if (!suppress) { uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                             uint32_t v = (uint32_t)ip; put(ptr, &v, 4); }
        } else break;                          // unknown conversion
    }
    return (assigned == 0 && eof_before_assign) ? (uint32_t)-1 : (uint32_t)assigned;
}
}  // namespace

using namespace guestmap;

TrapLayer::TrapLayer(std::vector<std::string> names)
    : names_(std::move(names)), hits_(names_.size(), 0), heap_next_(HEAP_BASE) {
    if (const char* e = std::getenv("THEOC_DATA")) data_root_ = e;
    else data_root_ = "data/game";
    register_builtins();
}

TrapLayer::~TrapLayer() {
    if (wd_thread_.joinable()) {
        wd_stop_.store(true, std::memory_order_relaxed);
        wd_thread_.join();
    }
    if (audio_dev_) {
        SDL_CloseAudioDevice(audio_dev_);
        audio_dev_ = 0;
    }
    for (auto& [_, f] : files_)
        if (f.fp && f.fp != stdin && f.fp != stdout && f.fp != stderr) std::fclose(f.fp);
    for (auto& [_, f] : fds_)
        if (f.host_fd >= 0) ::close(f.host_fd);
}

// THEOC_AUTO_KEYS=1: tap SPACE every few seconds through the real SDL event
// path. The mouse self-drivers never press a key, so the keyboard half of the
// input path had no unattended coverage — and SPACE (eKey 0x51, odd) is exactly
// the case that wedged cIntuition::PushKeyInput on a stale flags word. Called
// from both present paths so it also fires during cutscenes (where it skips).
void TrapLayer::auto_keys_tick() {
    static const bool on = std::getenv("THEOC_AUTO_KEYS") != nullptr;
    if (!on) return;
    using clock = std::chrono::steady_clock;
    static auto t0 = clock::now();
    static int fired = 0;
    static bool held = false;
    double t = std::chrono::duration<double>(clock::now() - t0).count();
    auto tap = [&](bool down) {
        SDL_Event ev{};
        ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        ev.key.keysym.scancode = SDL_SCANCODE_SPACE;
        std::fprintf(stderr, "  [input] AUTO_KEYS space %s @%.1fs\n", down ? "down" : "up", t);
        on_sdl_event(ev);
    };
    if (!held && t >= 6.0 * (fired + 1)) { tap(true);  held = true; }
    else if (held && t >= 6.0 * (fired + 1) + 0.2) { tap(false); held = false; fired++; }
}

// ---- soak driver (THEOC_SOAK=cycles) ----------------------------------------
// Loops: main menu → Prophecy → OK → province → map → exit → confirm → menu,
// snapshotting resource use at the same point in every cycle. This is the G15
// pattern (load / unload repeatedly), which is what exhausted the guest heap.
//
// Steps wait on an observable transition, not a stopwatch. The active cScreen*
// at Intuition+0x24 changes on every screen change, so "click, then wait for
// that pointer to differ" survives a slow load; a wall-clock script does not,
// and one late load would desync every later click onto the wrong screen.
// A click has to be paced across frames, not squeezed into one. At the 12fps
// province cadence a frame is 83ms, and the game only samples the pointer once
// per ProcessInputs — aim and press in the same frame, or a release under one
// frame later, and the press is never observed. Mirrors the spacing the working
// AUTO_PROVINCE driver uses. Returns true when the click is finished.
bool TrapLayer::soak_click_step(int x, int y) {
    const int kFramesPerPhase = 3;
    if (++soak_click_frames_ < kFramesPerPhase) return false;
    soak_click_frames_ = 0;
    switch (soak_click_phase_++) {
        case 0:                                  // aim
            mouse_x_ = x; mouse_y_ = y;
            update_intuition_pointer(x, y, 0);
            push_intuition_move(x, y);
            return false;
        case 1:                                  // press
            push_intuition_button_edges(0, 1);
            update_intuition_pointer(x, y, 1);
            mouse_buttons_ = 1;
            return false;
        default:                                 // release
            push_intuition_button_edges(1, 0);
            update_intuition_pointer(x, y, 0);
            mouse_buttons_ = 0;
            return true;
    }
}

uint32_t TrapLayer::active_screen() const {
    uint32_t intu = intuition_obj();
    if (!intu || !machine_) return 0;
    try { return machine_->r32(intu + 0x24); } catch (...) { return 0; }
}

void TrapLayer::soak_snapshot(const char* tag) {
    size_t rss = 0;
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t cnt = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &cnt) == KERN_SUCCESS)
        rss = info.resident_size;
#endif
    uint32_t esp = 0;
    if (machine_) { try { esp = machine_->reg(UC_X86_REG_ESP); } catch (...) {} }
    std::fprintf(stderr,
        "[soak] %-9s cycle %d | heap %.2f MB live / %.2f MB frontier "
        "| rss %.1f MB | esp %#x | stubs %u B | fds %zu\n",
        tag, soak_cycle_, heap_live_ / 1048576.0,
        (heap_next_ - HEAP_BASE) / 1048576.0, rss / 1048576.0, esp,
        stub_next_ ? stub_next_ - STUB_CODE : 0u, fds_.size());
    std::fflush(stderr);
}

void TrapLayer::soak_tick() {
    static const int want_cycles = [] {
        const char* e = std::getenv("THEOC_SOAK");
        if (!e) return 0;
        int v = std::atoi(e);
        return v > 0 ? v : 5;
    }();
    if (!want_cycles) return;

    using clock = std::chrono::steady_clock;
    auto now = clock::now();

    // The script. wait_screen = proceed when the active cScreen* changes from
    // what it was when the step began; wait_sec = settle time (the quit confirm
    // is an overlay on the same screen, so there is no pointer change to wait
    // for). Every step has a deadline: a step that never completes is a bug we
    // want reported loudly, not a driver that hangs silently.
    struct Step { const char* what; int x, y; bool wait_screen; double wait_sec; double timeout; };
    static const Step script[] = {
        {"prophecy",  65, 260, true,  0.0, 90.0},  // main menu → campaign
        {"ok",       464, 535, true,  0.0, 90.0},  // scrolling intro → province
        {"play",       0,   0, false, 0.0, 0.0},   // dwell (THEOC_SOAK_PLAY)
        {"map",       93,  15, true,  0.0, 60.0},  // province → map
        {"exit",      91,  10, false, 1.5, 60.0},  // map → confirm overlay
        {"confirm",  357, 326, true,  0.0, 60.0},  // → back at main menu
    };
    static const int nsteps = (int)(sizeof script / sizeof script[0]);
    static const double play_sec = [] {
        const char* e = std::getenv("THEOC_SOAK_PLAY");
        double v = e ? std::atof(e) : 20.0;
        return v > 0 ? v : 20.0;
    }();

    if (soak_t0_.time_since_epoch().count() == 0) {
        soak_t0_ = now;
        soak_step_started_ = now;
        // Line-buffer stdout so guest prints interleave correctly with our
        // stderr diagnostics; block buffering makes the log unreadable.
        setvbuf(stdout, nullptr, _IOLBF, 0);
        std::fprintf(stderr, "[soak] driver ON — %d cycles, %.0fs play per cycle\n",
                     want_cycles, play_sec);
    }
    if (soak_done_) return;

    // Settle before the very first click: the menu needs to exist.
    double since_step = std::chrono::duration<double>(now - soak_step_started_).count();
    if (soak_step_ == 0 && soak_cycle_ == 0 && since_step < 3.0) return;

    const Step& st = script[soak_step_];

    if (!soak_clicked_) {                       // ---- perform the step
        if (std::strcmp(st.what, "play") == 0) {
            soak_clicked_ = true;
            soak_screen_before_ = active_screen();
            soak_step_started_ = now;
            return;
        }
        if (soak_click_phase_ == 0 && soak_click_frames_ == 0) {
            soak_screen_before_ = active_screen();
            std::fprintf(stderr, "[soak] cycle %d step %-8s click %d,%d (win %dx%d screen %#x)\n",
                         soak_cycle_, st.what, st.x, st.y,
                         video_.width(), video_.height(), soak_screen_before_);
        }
        if (soak_click_step(st.x, st.y)) {
            soak_click_phase_ = 0;
            soak_click_frames_ = 0;
            soak_clicked_ = true;
            soak_step_started_ = now;
        }
        return;
    }

    // ---- wait for the step to complete
    bool done = false;
    if (std::strcmp(st.what, "play") == 0)      done = since_step >= play_sec;
    else if (st.wait_screen)                    done = active_screen() != soak_screen_before_
                                                       && active_screen() != 0;
    else                                        done = since_step >= st.wait_sec;

    if (!done) {
        if (st.timeout > 0 && since_step > st.timeout) {
            std::fprintf(stderr,
                "\n[soak] FAILED: step '%s' (cycle %d) did not complete in %.0fs.\n"
                "  active screen %#x (was %#x), win %dx%d — driver stopping.\n",
                st.what, soak_cycle_, st.timeout, active_screen(),
                soak_screen_before_, video_.width(), video_.height());
            soak_snapshot("TIMEOUT");
            soak_done_ = true;
        }
        return;
    }

    soak_clicked_ = false;
    soak_step_++;
    soak_step_started_ = now;
    if (soak_step_ >= nsteps) {                 // ---- cycle complete
        soak_step_ = 0;
        soak_cycle_++;
        soak_snapshot("cycle-end");
        if (soak_cycle_ >= want_cycles) {
            double mins = std::chrono::duration<double>(now - soak_t0_).count() / 60.0;
            std::fprintf(stderr, "[soak] COMPLETE — %d cycles in %.1f min\n",
                         soak_cycle_, mins);
            soak_done_ = true;
        }
    }
}

// ---- render-bug harness -----------------------------------------------------
// THEOC_CLICKS="x,y;x,y"  click a path (paced, waiting between clicks)
// THEOC_MOUSE_SWEEP=1     then drag the pointer across the screen
// THEOC_SHOT_EVERY=N      save every Nth frame to THEOC_SHOT_DIR as BMP
// Built for the cursor-trail bug: trails only show across consecutive frames
// with a moving pointer, which no amount of log reading will reveal.
void TrapLayer::render_probe_tick() {
    static const char* clicks_env = std::getenv("THEOC_CLICKS");
    static const bool  sweep      = std::getenv("THEOC_MOUSE_SWEEP") != nullptr;
    static const int   shot_every = [] {
        const char* e = std::getenv("THEOC_SHOT_EVERY");
        return e ? std::atoi(e) : 0;
    }();
    static const char* shot_dir = [] {
        const char* e = std::getenv("THEOC_SHOT_DIR");
        return e ? e : ".";
    }();
    if (!clicks_env && !sweep && !shot_every) return;

    using clock = std::chrono::steady_clock;
    static auto t0 = clock::now();
    double t = std::chrono::duration<double>(clock::now() - t0).count();
    if (t < 3.0) return;                       // let the first screen settle

    // --- click path
    static std::vector<std::pair<int,int>> path = [] {
        std::vector<std::pair<int,int>> v;
        if (const char* e = std::getenv("THEOC_CLICKS")) {
            int x, y; const char* p = e;
            while (*p) {
                if (std::sscanf(p, "%d,%d", &x, &y) == 2) v.push_back({x, y});
                const char* semi = std::strchr(p, ';');
                if (!semi) break;
                p = semi + 1;
            }
        }
        return v;
    }();
    static size_t click_i = 0;
    static auto last_click_done = clock::now();
    if (click_i < path.size()) {
        if (std::chrono::duration<double>(clock::now() - last_click_done).count() < 2.0)
            return;                            // settle between clicks
        if (soak_click_step(path[click_i].first, path[click_i].second)) {
            std::fprintf(stderr, "  [probe] clicked %d,%d (win %dx%d screen %#x)\n",
                         path[click_i].first, path[click_i].second,
                         video_.width(), video_.height(), active_screen());
            soak_click_phase_ = 0;
            soak_click_frames_ = 0;
            click_i++;
            last_click_done = clock::now();
        }
        return;
    }

    // --- pointer sweep: a few px per frame, so a failed restore leaves a track
    static int sweep_frame = 0;
    if (sweep) {
        int W = video_.width(), H = video_.height();
        int x = 60 + (sweep_frame * 7) % (W > 160 ? W - 120 : 1);
        int y = H / 2 + (int)(40 * std::sin(sweep_frame * 0.15));
        mouse_x_ = x; mouse_y_ = y;
        update_intuition_pointer(x, y, 0);
        push_intuition_move(x, y);
        sweep_frame++;
    }

    shot_tick();
}

// THEOC_SHOT_EVERY=N — save every Nth presented frame to THEOC_SHOT_DIR.
// Split out of render_probe_tick so cutscenes can be captured too: they present
// from SMPEG_playvideoframe, not the normal frame path, so the harness used to
// have a blind spot over exactly the frames a video-scaling bug shows up in.
// Deliberately capture-only — the click/sweep drivers must NOT run during a
// cutscene, where a synthesized click would skip the thing being photographed.
void TrapLayer::shot_tick() {
    static const int shot_every = [] {
        const char* e = std::getenv("THEOC_SHOT_EVERY");
        return e ? std::atoi(e) : 0;
    }();
    static const char* shot_dir = [] {
        const char* e = std::getenv("THEOC_SHOT_DIR");
        return e ? e : ".";
    }();
    if (!shot_every) return;
    static int shot_frame = 0, shot_n = 0;
    if ((shot_frame++ % shot_every) == 0 && shot_n < 40) {
        char path_buf[512];
        std::snprintf(path_buf, sizeof path_buf, "%s/frame_%03d.bmp", shot_dir, shot_n);
        if (video_.save_bmp(path_buf)) shot_n++;
    }
}

double TrapLayer::slowlog_ms() {
    static const double lim = [] {
        const char* e = std::getenv("THEOC_SLOWLOG");
        if (!e) return 0.0;
        double v = std::atof(e);
        return v > 1.0 ? v : 250.0;    // "=1" means "on", not "1 ms"
    }();
    return lim;
}

// ---- stall watchdog ---------------------------------------------------------
// A freeze is only ambiguous until you know whether the CPU is still running.
// This thread watches the present counter; when frames stop it samples guest
// blocks and trap calls over half a second and says which side is stuck.
void TrapLayer::start_watchdog(Machine& m) {
    if (wd_thread_.joinable()) return;
    const char* env = std::getenv("THEOC_WATCHDOG");
    if (!env) return;
    double secs = std::atof(env);
    if (secs <= 1.0) secs = 10.0;          // "=1" means "on", not "1 second"
    wd_m_ = &m;
    wd_t0_ = std::chrono::steady_clock::now();
    m.enable_block_counter();              // no-op if THEOC_FPS/PROFILE armed it
    std::fprintf(stderr, "[watchdog] stall watchdog ON (report after %.0fs "
                 "without a frame)\n", secs);
    wd_thread_ = std::thread([this, secs] { watchdog_loop(secs); });
}

void TrapLayer::watchdog_loop(double stall_sec) {
    using clock = std::chrono::steady_clock;
    uint64_t seen = present_seq_.load(std::memory_order_relaxed);
    auto last_frame = clock::now();
    while (!wd_stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        uint64_t now_seq = present_seq_.load(std::memory_order_relaxed);
        auto now = clock::now();
        if (now_seq != seen) {             // frames still coming — not wedged
            seen = now_seq;
            last_frame = now;
            continue;
        }
        double stalled = std::chrono::duration<double>(now - last_frame).count();
        if (stalled < stall_sec) continue;
        // Sample both counters across a window: are we moving at all?
        uint64_t b0 = wd_m_->exec_blocks(), t0 = trap_seq_.load(std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        uint64_t db = wd_m_->exec_blocks() - b0;
        uint64_t dt = trap_seq_.load(std::memory_order_relaxed) - t0;
        uint32_t eip = wd_m_->last_block();
        const char* tn = last_trap_.load(std::memory_order_relaxed);
        const uint32_t mvos = mvos_base_ ? mvos_base_ : 0x10000000u;
        char lbl[48];
        if (eip >= mvos && eip < mvos + 0x200000)
            std::snprintf(lbl, sizeof lbl, "mvos+%#x", eip - mvos);
        else
            std::snprintf(lbl, sizeof lbl, "game %#010x", eip);
        double uptime = std::chrono::duration<double>(now - wd_t0_).count();
        std::fprintf(stderr,
                     "\n[watchdog] t=%.1fs STALLED %.1fs with no frame — guest %s\n"
                     "  +%llu blocks / +%llu traps in 0.5s | last guest EIP %s"
                     " | last trap %s | heap %.1f MB live\n",
                     uptime, stalled,
                     db ? "STILL RUNNING (spinning)" : "NOT EXECUTING (stuck host-side)",
                     (unsigned long long)db, (unsigned long long)dt, lbl,
                     tn ? tn : "(none)", heap_live_ / 1048576.0);
        // THEOC_WATCHDOG_SAMPLE=<path>: on a host-side stall (guest not
        // executing), grab a native stack of ourselves right now. Aggregate
        // profiles can't isolate a 1.5s window in a 40s run; this samples
        // exactly the stall.
        static const char* sample_to = std::getenv("THEOC_WATCHDOG_SAMPLE");
        if (sample_to && db == 0) {
            char cmd[512];
            std::snprintf(cmd, sizeof cmd,
                          "sample %d 1 -file '%s' >/dev/null 2>&1",
                          (int)getpid(), sample_to);
            int rc = std::system(cmd);
            std::fprintf(stderr, "  [watchdog] host stack -> %s (rc=%d)\n",
                         sample_to, rc);
        }
        last_frame = now;                  // re-report once per stall_sec
    }
}

void TrapLayer::ensure_audio() {
    if (audio_dev_) return;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "  [audio] SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = 22050;
    want.format = AUDIO_S16LSB;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &TrapLayer::audio_callback;
    want.userdata = this;
    audio_dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!audio_dev_) {
        std::fprintf(stderr, "  [audio] OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(audio_dev_, 0);
    std::fprintf(stderr, "  [audio] SDL device %u @ %d Hz %d ch\n",
                audio_dev_, have.freq, have.channels);
}

void TrapLayer::audio_callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<TrapLayer*>(userdata);
    auto* out = reinterpret_cast<int16_t*>(stream);
    int nsamp = len / 2;
    std::lock_guard<std::mutex> lock(self->audio_mu_);
    for (int i = 0; i < nsamp; ++i) {
        if (!self->audio_q_.empty()) {
            out[i] = self->audio_q_.front();
            self->audio_q_.pop_front();
        } else {
            out[i] = 0;
            self->audio_underrun_++;   // silence gap = audible stutter (THEOC_FPS)
        }
    }
}

void TrapLayer::audio_push(const void* data, size_t nbytes) {
    if (!audio_dev_ || !data || !nbytes) return;
    // Assume little-endian 16-bit PCM (game SoundCard_Linux / SoftwareMix default).
    const auto* s = static_cast<const int16_t*>(data);
    size_t n = nbytes / 2;
    std::lock_guard<std::mutex> lock(audio_mu_);
    // Cap queue ~1s @ 22k stereo to avoid unbounded growth if callback lags.
    const size_t cap = 22050 * 2;
    for (size_t i = 0; i < n; ++i) {
        if (audio_q_.size() >= cap) audio_q_.pop_front();
        audio_q_.push_back(s[i]);
    }
}

std::string TrapLayer::resolve_path(const std::string& guest) const {
    if (guest.empty()) return guest;
    // Device nodes are never real files on the Mac host.
    if (guest.rfind("/dev/", 0) == 0) return guest;
    // Linux install default mountpoint — map to the CD tree we ship.
    // VM_GetCDRomName opens `$mount/cd.key` and checks for "Theocracy".
    const char* cd = std::getenv("THEOC_CD");
    std::string cd_root = cd ? cd : "data/cd";
    if (guest.rfind("/mnt/cdrom", 0) == 0) {
        std::string rest = guest.size() > 10 ? guest.substr(10) : "";
        if (rest.empty() || rest == "/") return cd_root;
        if (rest[0] == '/') return cd_root + rest;
        return cd_root + "/" + rest;
    }
    // Intro/logo MPEG cutscenes live on the CD, not in the installed data tree.
    // Game asks for "movie/ubi_logo.mpg", "logo.mpg", "intro.mpg", etc.
    if (guest.rfind("movie/", 0) == 0 || guest.rfind("movie\\", 0) == 0)
        return cd_root + "/" + guest;
    if (guest.size() > 4) {
        auto ends = [&](const char* ext) {
            auto n = std::strlen(ext);
            return guest.size() >= n &&
                   guest.compare(guest.size() - n, n, ext) == 0;
        };
        if (ends(".mpg") || ends(".MPG") || ends(".mpeg"))
            return cd_root + "/movie/" + guest;
    }
    if (guest[0] == '/') return guest;                    // other absolute paths
    // Guest uses "data/…"; install root is $THEOC_DATA (default data/game).
    return data_root_ + "/" + guest;
}

// ---- host platform differences, isolated -------------------------------------
// Exactly two things in this file are BSD-specific, and both are in the socket
// path below. Kept as narrow conditionals rather than a platform/ abstraction:
// two sites do not earn an interface. That calculus changes at Winsock — see
// docs/porting/other-os-ports.md.
//
// Note the direction of travel. The *guest* is a Linux binary, so on a Linux
// host most of this file's translation work is the identity function and the
// job is to remove it, not to add more. These two are the exceptions: things
// the BSD host needed that Linux simply does not have.
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(BSD)
#  define THEOC_HAVE_SIN_LEN      1  // sockaddr_in carries a leading length byte
#  define THEOC_HAVE_SO_NOSIGPIPE 1  // per-socket SIGPIPE suppression
#endif

// ---- Winsock2 compatibility --------------------------------------------------
// This is where the platform seam finally earned its keep. Linux needed two #if
// guards; Windows needs a whole vocabulary, because Winsock is not a POSIX
// socket API wearing a different hat:
//
//  * sockets are not file descriptors. They live in a separate namespace from
//    CRT fds, so ::close/::read/::write on a SOCKET are wrong — see the `sock`
//    flag on HostFile and the dispatch in close/read/write. This is the one that
//    would have compiled cleanly and failed at runtime.
//  * errors come from WSAGetLastError(), not errno, and use their own 100xx
//    numbering that shares nothing with Linux's.
//  * non-blocking is ioctlsocket(FIONBIO), and there is no F_GETFL to read it
//    back, so the flag has to be remembered host-side.
//  * send/recv take char* rather than void*.
//  * there is no SIGPIPE at all, so both the BSD and Linux mitigations are moot.
#if defined(_WIN32)
// Winsock's socket handle is UINT_PTR. Every handle Windows actually hands out
// fits in 32 bits and the whole fd table here is int-keyed, so narrow at the
// boundary rather than churn the table — but do it in one named place.
typedef int socklen_t_compat;
#  define THEOC_CLOSESOCKET(fd) ::closesocket((SOCKET)(fd))
#  define THEOC_SOCK_CAST(p)    ((char*)(p))
static inline int theoc_mkdir(const char* p) { return ::mkdir(p); }
#else
typedef socklen_t socklen_t_compat;
#  define THEOC_CLOSESOCKET(fd) ::close(fd)
#  define THEOC_SOCK_CAST(p)    (p)
static inline int theoc_mkdir(const char* p) { return ::mkdir(p, 0755); }
#endif

// ---- sub-millisecond sleep ---------------------------------------------------
// The one place where a host difference is a *gameplay* difference rather than a
// compile error. This port sleeps in slices bounded by the next 30Hz heartbeat —
// tens of sub-millisecond sleeps per second — and the game's frame limiter is
// elapsed-based, so a sleep that overshoots does not merely jitter, it slows the
// simulation. On POSIX ::usleep resolves this to well under a millisecond and
// there is nothing to do. On Windows the default scheduler tick is ~15.6 ms, and
// mingw's ::usleep is a Sleep() wrapper riding it: measured, that ran the
// province frame at 94 ms against an 83.3 ms target (~13% slow) with the
// heartbeat 9 ms late every tick.
//
// CreateWaitableTimerEx(HIGH_RESOLUTION) is the fix and the whole fix: 0.63 ms
// for a 0.1 ms request, province frame 84.0 ms. Measured alternatives and why
// they are not here — docs/porting/other-os-ports.md, "The probe's answer":
//
//  * timeBeginPeriod(1) on top of the timer buys nothing (0.647 vs 0.655 ms,
//    i.e. noise), and raising the timer resolution is a system-wide side effect
//    with a power cost. So it appears below only on the path that has no timer.
//  * a *coarse* waitable timer, which is what CreateWaitableTimerEx degrades to
//    if the flag is rejected, rides the same 15.6 ms tick as Sleep and would buy
//    nothing over it. The probe measures that fallback; the port skips straight
//    past it to timeBeginPeriod + Sleep, which measured 2.0 ms.
#if defined(_WIN32)
#  ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION   // pre-Win10-1803 SDK headers
#    define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#  endif
static inline void theoc_sleep_us(uint32_t us) {
    if (!us) return;
    // One handle for the process, not one per sleep: at ~40 slices/s a create +
    // close pair per sleep is pure syscall overhead. Created on first use; the
    // host runs one thread, and a function-local static is race-free regardless.
    static HANDLE timer = []() -> HANDLE {
        HANDLE h = CreateWaitableTimerExW(nullptr, nullptr,
                                          CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
        if (h) return h;
        // No high-resolution timer (pre-1803). Raise the scheduler tick for this
        // process once and live with Sleep()'s whole-millisecond granularity.
        timeBeginPeriod(1);
        std::fprintf(stderr,
                     "[timing] no high-resolution waitable timer (err %lu) — "
                     "falling back to timeBeginPeriod(1)+Sleep; expect the "
                     "province frame ~2%% slow\n",
                     (unsigned long)GetLastError());
        return nullptr;
    }();
    if (timer) {
        // Relative due time, in negative 100 ns units.
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)us * 10;
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }
    // Round up: Sleep(0) is "yield the rest of the quantum", a different
    // operation, and would return early from a sleep the caller asked for.
    DWORD ms = (DWORD)((us + 999) / 1000);
    if (!ms) ms = 1;
    Sleep(ms);
}
#else
static inline void theoc_sleep_us(uint32_t us) { if (us) ::usleep(us); }
#endif

// ---- O_BINARY: the quiet data-corruption hazard ------------------------------
// Windows opens files in *text* mode by default, which translates \n <-> \r\n on
// the way through and stops reading at a 0x1a byte. Every file this port touches
// on the guest's behalf is binary: the .tsg saves, the PHLS .pck archives, the
// MPEG cutscenes, the XOR-obfuscated config. Without O_BINARY they would be
// silently mangled — a bug that compiles cleanly, runs, and only shows up as a
// corrupt save days later.
//
// O_BINARY simply does not exist on POSIX (where there is no such translation to
// suppress), so it is defined to 0 there and every guest-facing open ORs it in
// unconditionally. Same for the "b" in fopen mode strings, which is already
// ignored on POSIX but load-bearing here.
#if !defined(O_BINARY)
#  define O_BINARY 0
#endif

// ---- guest(Linux/i386) <-> host(BSD/macOS) socket translation ----------------
// The guest is a 1999 Linux i386 binary and we are on BSD. Three things differ in
// ways that fail silently rather than loudly, so each is translated explicitly:
//
//  1. `struct sockaddr_in`. Linux: u16 family @0, u16 port @2, u32 addr @4.
//     BSD: u8 sin_len @0, u8 sin_family @1, then the same. Passing a guest
//     sockaddr straight to the host reads family as 0x0002 -> len=2, family=0.
//  2. Flag/level constants. Linux O_NONBLOCK is 0x800 (BSD 0x0004),
//     SOL_SOCKET is 1 (BSD 0xffff), SO_REUSEADDR is 2 (BSD 4).
//  3. errno values. Linux EAGAIN=11 (BSD 35), EINPROGRESS=115 (BSD 36),
//     EADDRINUSE=98 (BSD 48). The guest compares against its own numbers, and a
//     non-blocking socket returns EAGAIN constantly, so getting this wrong turns
//     "no data yet" into a hard error.
namespace {

// Host errno -> the Linux value the guest expects. Anything unmapped passes
// through: the shared low numbers (EINTR/EBADF/EINVAL/EPIPE) already agree.
int to_linux_errno(int e) {
    switch (e) {
        case EAGAIN:       return 11;   // == EWOULDBLOCK on both sides
        case EINPROGRESS:  return 115;
        case EALREADY:     return 114;
        case EADDRINUSE:   return 98;
        case EADDRNOTAVAIL:return 99;
        case ECONNREFUSED: return 111;
        case ECONNRESET:   return 104;
        case EISCONN:      return 106;
        case ENOTCONN:     return 107;
        case ETIMEDOUT:    return 110;
        case EHOSTUNREACH: return 113;
        case ENETUNREACH:  return 101;
        default:           return e;
    }
}

// The socket-error accessor. On POSIX the socket calls set errno like anything
// else; Winsock keeps its own error slot and its own numbering, which shares
// *nothing* with Linux's — WSAEWOULDBLOCK is 10035 where the guest expects 11.
//
// Why this matters more than it looks: libmvos's cIPCO_TCPIP switches on errno
// 4..22 and maps anything outside that window to its generic "unknown error"
// (5). A non-blocking read with no data pending is the common case on every
// netgame frame, so leaking 10035 instead of 11 would turn "no data yet" into a
// hard error on every poll — the exact bug the BSD EAGAIN=35 mistake caused,
// with a number three orders of magnitude further out of range.
int last_socket_errno() {
#if defined(_WIN32)
    switch (WSAGetLastError()) {
        case WSAEWOULDBLOCK:   return 11;    // EAGAIN
        case WSAEINPROGRESS:   return 115;
        case WSAEALREADY:      return 114;
        case WSAEADDRINUSE:    return 98;
        case WSAEADDRNOTAVAIL: return 99;
        case WSAECONNREFUSED:  return 111;
        case WSAECONNRESET:    return 104;
        case WSAECONNABORTED:  return 103;
        case WSAEISCONN:       return 106;
        case WSAENOTCONN:      return 107;
        case WSAETIMEDOUT:     return 110;
        case WSAEHOSTUNREACH:  return 113;
        case WSAENETUNREACH:   return 101;
        case WSAENETDOWN:      return 100;
        case WSAENOTSOCK:      return 88;
        case WSAEINTR:         return 4;
        case WSAEBADF:         return 9;
        case WSAEINVAL:        return 22;
        case WSAEMFILE:        return 24;
        case WSAEACCES:        return 13;
        case WSAEFAULT:        return 14;
        case WSAEMSGSIZE:      return 90;
        case WSAEAFNOSUPPORT:  return 97;
        case WSAEPROTONOSUPPORT: return 93;
        case WSAESHUTDOWN:     return 108;
        default:               return 5;     // EIO — libmvos's own "unknown"
    }
#else
    return to_linux_errno(errno);
#endif
}

constexpr uint32_t kGuestAfInet = 2;   // AF_INET agrees, but check it explicitly

// Read a guest (Linux-layout) sockaddr_in. Returns false for anything that is
// not AF_INET — we deliberately do not guess at IPX/unix addresses.
bool guest_to_host_sin(Machine& m, uint32_t gaddr, uint32_t glen, sockaddr_in& out) {
    if (!gaddr || glen < 8) {
        std::fprintf(stderr, "  [net] bad sockaddr: ptr=%#x len=%u\n", gaddr, glen);
        return false;
    }
    uint16_t fam = 0, port = 0;
    uint32_t ip = 0;
    m.read(gaddr + 0, &fam, 2);
    m.read(gaddr + 2, &port, 2);   // already network byte order in the guest
    m.read(gaddr + 4, &ip, 4);
    // Family 0 (AF_UNSPEC) is ACCEPTED as AF_INET. The engine's single-instance
    // lock binds {family=0, port=5043, addr=INADDR_ANY} — it simply never sets
    // sin_family. Linux tolerates that on a socket already created as AF_INET;
    // BSD returns EAFNOSUPPORT. Since our socket() only ever creates AF_INET, the
    // guest's intent is unambiguous, so honour it the way Linux did rather than
    // failing a bind the original binary relied on.
    if (fam != kGuestAfInet && fam != 0) {
        // Never fail a sockaddr silently — that turns a translation bug into a
        // mystery "-1" three layers up in guest code.
        uint8_t raw[16] = {0};
        m.read(gaddr, raw, glen < 16 ? glen : 16);
        std::fprintf(stderr, "  [net] bad sockaddr family=%u len=%u raw=", fam, glen);
        for (int i = 0; i < 16; ++i) std::fprintf(stderr, "%02x ", raw[i]);
        std::fprintf(stderr, "\n");
        return false;
    }
    std::memset(&out, 0, sizeof out);
#ifdef THEOC_HAVE_SIN_LEN
    out.sin_len = sizeof(sockaddr_in);   // the byte the guest does not know about
#endif                                   // (Linux has no such field — see above)
    out.sin_family = AF_INET;
    out.sin_port = port;
    out.sin_addr.s_addr = ip;
    return true;
}

// Write a host sockaddr_in back in guest layout (accept/recvfrom peer address).
void host_to_guest_sin(Machine& m, uint32_t gaddr, uint32_t glen_ptr,
                       const sockaddr_in& in) {
    if (!gaddr) return;
    uint16_t fam = (uint16_t)kGuestAfInet;
    uint8_t zero[8] = {0};
    m.write(gaddr + 0, &fam, 2);
    m.write(gaddr + 2, &in.sin_port, 2);
    m.write(gaddr + 4, &in.sin_addr.s_addr, 4);
    m.write(gaddr + 8, zero, 8);
    if (glen_ptr) m.w32(glen_ptr, 16);
}

}  // namespace

void TrapLayer::set_errno(Machine& m, int err) {
    m.w32(ERRNO_ADDR, (uint32_t)err);
}

// Map a guest fd to its host fd, or -1. Sockets live in the same table as files;
// the guest tells us which is which by calling send/recv vs read/write.
int TrapLayer::host_fd_of(int gfd) {
    auto it = fds_.find(gfd);
    if (it == fds_.end()) return -1;
    return it->second.host_fd;
}

int TrapLayer::adopt_host_fd(int hfd, bool is_socket) {
    int gfd = next_fd_++;
    HostFile hf{};
    hf.host_fd = hfd;
    hf.sock    = is_socket;
    fds_[gfd]  = hf;
    return gfd;
}

// Free list is kept twice: by address (so neighbours can be coalesced) and by
// size (so allocation is a best-fit O(log n) lookup instead of a linear scan —
// a scenario load churns enough blocks that first-fit would be a real cost).
void TrapLayer::fl_insert(uint32_t addr, uint32_t size) {
    free_addr_[addr] = size;
    free_size_.emplace(size, addr);
}

void TrapLayer::fl_erase(uint32_t addr, uint32_t size) {
    free_addr_.erase(addr);
    auto r = free_size_.equal_range(size);
    for (auto i = r.first; i != r.second; ++i)
        if (i->second == addr) { free_size_.erase(i); break; }
}

uint32_t TrapLayer::bump_alloc(uint32_t size) {
    uint32_t aligned = (size + 15u) & ~15u;   // 16-byte aligned
    if (!aligned) aligned = 16;               // malloc(0) still needs an address

    // Reuse a freed block before touching fresh memory (best fit).
    auto it = free_size_.lower_bound(aligned);
    if (it != free_size_.end()) {
        uint32_t p = it->second, blk = it->first;
        fl_erase(p, blk);
        if (blk - aligned >= 32) {            // split; tail goes back on the list
            fl_insert(p + aligned, blk - aligned);
            blk = aligned;
        }
        alloc_sz_[p] = blk;
        heap_live_ += blk;
        return p;
    }

    if (heap_next_ + aligned > HEAP_BASE + HEAP_SIZE) {
        std::fprintf(stderr,
                     "[heap] OUT OF MEMORY requesting %u bytes "
                     "(live %.1f MB, frontier %.1f MB of %.0f MB)\n",
                     size, heap_live_ / 1048576.0,
                     (heap_next_ - HEAP_BASE) / 1048576.0, HEAP_SIZE / 1048576.0);
        return 0;
    }
    uint32_t p = heap_next_;
    heap_next_ += aligned;
    alloc_sz_[p] = aligned;
    heap_live_ += aligned;
    return p;
}

// Randomized soak of the allocator. The failure this is really guarding is
// two live blocks overlapping — that corrupts guest memory silently, which is
// strictly worse than the leak this allocator replaced. Returns true on pass.
bool TrapLayer::heap_selftest() {
    uint32_t seed = 12345;                       // deterministic
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    std::vector<uint32_t> live;                  // block addresses
    int errors = 0;

    auto check_overlap = [&](uint32_t p) {
        uint32_t ps = alloc_sz_.count(p) ? alloc_sz_[p] : 0;
        for (uint32_t q : live) {
            if (q == p) { std::fprintf(stderr, "  [heaptest] FAIL: %#x handed out twice\n", p); return false; }
            uint32_t qs = alloc_sz_.count(q) ? alloc_sz_[q] : 0;
            if (p < q + qs && q < p + ps) {
                std::fprintf(stderr, "  [heaptest] FAIL: %#x+%u overlaps %#x+%u\n", p, ps, q, qs);
                return false;
            }
        }
        return true;
    };

    for (int round = 0; round < 6 && errors == 0; ++round) {
        for (int i = 0; i < 3000; ++i) {
            if (!live.empty() && (rnd() % 100) < 45) {          // free
                size_t k = rnd() % live.size();
                guest_free(live[k]);
                live[k] = live.back();
                live.pop_back();
            } else {                                            // alloc
                uint32_t want = 1 + rnd() % 6000;
                uint32_t p = bump_alloc(want);
                if (!p) { std::fprintf(stderr, "  [heaptest] FAIL: OOM (round %d)\n", round); errors++; break; }
                if (!check_overlap(p)) { errors++; break; }
                live.push_back(p);
            }
        }
        std::fprintf(stderr, "  [heaptest] round %d: %zu live, %.2f MB live, "
                    "%.2f MB frontier, %zu free blocks\n",
                    round, live.size(), heap_live_ / 1048576.0,
                    (heap_next_ - HEAP_BASE) / 1048576.0, free_addr_.size());
    }

    uint32_t frontier_after_churn = heap_next_;
    for (uint32_t p : live) guest_free(p);
    live.clear();
    std::fprintf(stderr, "  [heaptest] all freed: %u B live, %.2f MB frontier, %zu free blocks\n",
                heap_live_, (heap_next_ - HEAP_BASE) / 1048576.0, free_addr_.size());

    // Everything returned => zero live bytes, and coalescing should have merged
    // the arena back into (nearly) one block rather than leaving thousands.
    if (heap_live_ != 0) { std::fprintf(stderr, "  [heaptest] FAIL: %u bytes leaked\n", heap_live_); errors++; }
    if (free_addr_.size() > 4) { std::fprintf(stderr, "  [heaptest] FAIL: poor coalescing\n"); errors++; }
    // Re-allocating after a full free must reuse, not extend the frontier.
    for (int i = 0; i < 500; ++i) if (!bump_alloc(4096)) { errors++; break; }
    if (heap_next_ > frontier_after_churn) {
        std::fprintf(stderr, "  [heaptest] FAIL: frontier grew on reuse (%.2f -> %.2f MB)\n",
                    (frontier_after_churn - HEAP_BASE) / 1048576.0,
                    (heap_next_ - HEAP_BASE) / 1048576.0);
        errors++;
    }
    std::fprintf(stderr, "  [heaptest] %s\n", errors ? "FAILED" : "PASSED");
    return errors == 0;
}

// Reuse is more faithful than never-reuse: real malloc hands the same memory
// back too. Unknown pointers (interior, double free, not ours) are ignored.
void TrapLayer::guest_free(uint32_t p) {
    if (!p) return;
    auto it = alloc_sz_.find(p);
    if (it == alloc_sz_.end()) return;
    uint32_t sz = it->second;
    alloc_sz_.erase(it);
    heap_live_ -= sz;

    auto nx = free_addr_.lower_bound(p);
    if (nx != free_addr_.end() && p + sz == nx->first) {   // coalesce forward
        uint32_t na = nx->first, ns = nx->second;
        ++nx;
        fl_erase(na, ns);
        sz += ns;
    }
    if (nx != free_addr_.begin()) {                        // coalesce backward
        auto pv = std::prev(nx);
        if (pv->first + pv->second == p) {
            uint32_t pa = pv->first, ps = pv->second;
            fl_erase(pa, ps);
            fl_insert(pa, ps + sz);
            return;
        }
    }
    fl_insert(p, sz);
}

uint32_t TrapLayer::stub_alloc(Machine& m, uint32_t size) {
    if (!stub_next_) {
        try {
            m.map(STUB_CODE, STUB_CODE_SIZE, UC_PROT_READ | UC_PROT_EXEC | UC_PROT_WRITE);
        } catch (...) {
            std::fprintf(stderr, "[stub] map RX page failed\n");
            return 0;
        }
        // Keep writeable for install; Unicorn needs W to write then can leave WX.
        stub_next_ = STUB_CODE;
    }
    uint32_t aligned = (size + 15u) & ~15u;
    if (stub_next_ + aligned > STUB_CODE + STUB_CODE_SIZE) {
        std::fprintf(stderr, "[stub] OUT OF STUB SPACE\n");
        return 0;
    }
    uint32_t p = stub_next_;
    stub_next_ += aligned;
    return p;
}

// Best-effort printf: pulls 32-bit varargs off the stack starting at argidx.
// Enough for diagnostics during bring-up; not a conformant implementation.
std::string TrapLayer::format(Machine& m, const std::string& fmt, uint32_t esp,
                              int argidx) {
    std::string out;
    char tmp[64];
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') { out.push_back(fmt[i]); continue; }
        size_t j = i + 1;
        while (j < fmt.size() && strchr("-+ #0123456789.lhLzq", fmt[j])) ++j;
        if (j >= fmt.size()) break;
        char conv = fmt[j];
        switch (conv) {
            case '%': out.push_back('%'); break;
            case 'd': case 'i':
                std::snprintf(tmp, sizeof tmp, "%d", (int)arg(m, esp, argidx++));
                out += tmp; break;
            case 'u':
                std::snprintf(tmp, sizeof tmp, "%u", arg(m, esp, argidx++));
                out += tmp; break;
            case 'x': case 'X': case 'p':
                std::snprintf(tmp, sizeof tmp, conv == 'X' ? "%X" : "%x",
                              arg(m, esp, argidx++));
                out += tmp; break;
            case 'c': out.push_back((char)arg(m, esp, argidx++)); break;
            case 's': {
                uint32_t p = arg(m, esp, argidx++);
                out += p ? m.cstr(p) : "(null)";
                break;
            }
            case 'f': case 'g': case 'e':   // double = 8 bytes on the stack
                out += "<float>"; argidx += 2; break;
            default: out.push_back('%'); out.push_back(conv); break;
        }
        i = j;
    }
    return out;
}

// Which stream gets what, and why it matters:
//
//   stdout — the *guest's* output. `puts`, `printf`, and the two write paths
//            that honour the guest's own fd. Four sites, all in this file.
//   stderr — everything the *port* says. Every [tag] line, the boot narrative,
//            the trap report.
//
// The documented way to capture a session is `2>session.log`, so anything of
// ours on stdout is absent from the log the analysis is done on. That has now
// cost three separate measurements: TrapLayer::report()'s end-of-run allocator
// state, the [video]/[click] lines during the fullscreen trial, and [console]
// during the battle trials. Each was fixed on its own; the split is written
// down here so the next diagnostic starts on the right stream.
void TrapLayer::register_builtins() {
    auto& t = table_;

    t["malloc"] = [this](Machine& m, uint32_t esp) {
        return bump_alloc(arg(m, esp, 0));
    };
    t["calloc"] = [this](Machine& m, uint32_t esp) {
        uint32_t n = arg(m, esp, 0) * arg(m, esp, 1);
        uint32_t p = bump_alloc(n);
        if (p && n) { std::vector<uint8_t> z(n, 0); m.write(p, z.data(), n); }
        return p;
    };
    t["realloc"] = [this](Machine& m, uint32_t esp) {
        uint32_t old = arg(m, esp, 0), nsz = arg(m, esp, 1);
        if (!old) return bump_alloc(nsz);
        if (!nsz) { guest_free(old); return 0u; }
        auto it = alloc_sz_.find(old);
        uint32_t oldsz = it != alloc_sz_.end() ? it->second : 0;
        uint32_t p = bump_alloc(nsz);
        if (p) {
            uint32_t cpy = std::min(oldsz, nsz);
            for (uint32_t k = 0; k < cpy; ++k) { uint8_t b; m.read(old + k, &b, 1); m.write(p + k, &b, 1); }
            guest_free(old);   // must come after the copy
        }
        return p;
    };
    t["free"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        guest_free(arg(m, esp, 0));
        return 0;
    };
    t["__builtin_vec_delete"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        guest_free(arg(m, esp, 0));
        return 0;
    };

    t["memcpy"] = [](Machine& m, uint32_t esp) {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1), n = arg(m, esp, 2);
        std::vector<uint8_t> b(n);
        if (n) { m.read(s, b.data(), n); m.write(d, b.data(), n); }
        return d;
    };
    t["memmove"] = t["memcpy"];
    t["memset"] = [](Machine& m, uint32_t esp) {
        uint32_t d = arg(m, esp, 0), c = arg(m, esp, 1), n = arg(m, esp, 2);
        std::vector<uint8_t> b(n, (uint8_t)c);
        if (n) m.write(d, b.data(), n);
        return d;
    };
    t["strcpy"] = [](Machine& m, uint32_t esp) {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1);
        std::string v = m.cstr(s);
        m.write(d, v.c_str(), (uint32_t)v.size() + 1);
        return d;
    };
    t["strncpy"] = [](Machine& m, uint32_t esp) {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1), n = arg(m, esp, 2);
        if (!d || !n) return d;
        std::string v = m.cstr(s, n);
        uint32_t copy = (uint32_t)std::min<size_t>(v.size(), n);
        if (copy) m.write(d, v.c_str(), copy);
        // C99: pad with zeros up to n if src shorter.
        if (copy < n) {
            std::vector<uint8_t> z(n - copy, 0);
            m.write(d + copy, z.data(), n - copy);
        }
        return d;
    };
    // strchr / strrchr return a POINTER INTO the guest string, so they must be
    // computed as guest addresses, not host ones. First surfaced on the netgame
    // path (the only route that reaches them): the stub returned 0, guest code
    // called through the NULL result, and it faulted as a fetch at eip=0.
    t["strchr"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t p = arg(m, esp, 0);
        int c = (int)(arg(m, esp, 1) & 0xff);
        if (!p) return 0;
        std::string s = m.cstr(p);
        size_t i = s.find((char)c);
        if (i != std::string::npos) return p + (uint32_t)i;
        return c == 0 ? p + (uint32_t)s.size() : 0;   // '\0' matches the terminator
    };
    t["strrchr"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t p = arg(m, esp, 0);
        int c = (int)(arg(m, esp, 1) & 0xff);
        if (!p) return 0;
        std::string s = m.cstr(p);
        size_t i = s.rfind((char)c);
        if (i != std::string::npos) return p + (uint32_t)i;
        return c == 0 ? p + (uint32_t)s.size() : 0;
    };

    t["strlen"] = [](Machine& m, uint32_t esp) {
        return (uint32_t)m.cstr(arg(m, esp, 0)).size();
    };
    t["strcmp"] = [](Machine& m, uint32_t esp) {
        return (uint32_t)(int32_t)std::strcmp(m.cstr(arg(m, esp, 0)).c_str(),
                                              m.cstr(arg(m, esp, 1)).c_str());
    };
    // strtok(str, delim): tokenizes in place. str==NULL continues from the saved
    // position; writes '\0' over the terminating delimiter and returns a pointer
    // into the guest buffer. Static `saved` holds the guest resume address.
    t["strtok"] = [](Machine& m, uint32_t esp) -> uint32_t {
        static uint32_t saved = 0;
        uint32_t s = arg(m, esp, 0);
        std::string delim = m.cstr(arg(m, esp, 1));
        bool isdelim[256] = {false};
        for (unsigned char c : delim) isdelim[c] = true;
        uint32_t p = s ? s : saved;
        if (!p) return 0;
        uint8_t c;
        while (true) { m.read(p, &c, 1); if (!c || !isdelim[c]) break; p++; }  // skip leading
        if (!c) { saved = 0; return 0; }
        uint32_t tok = p;
        while (true) { m.read(p, &c, 1); if (!c || isdelim[c]) break; p++; }   // find end
        if (!c) { saved = 0; }
        else { uint8_t z = 0; m.write(p, &z, 1); saved = p + 1; }
        return tok;
    };

    // Guest output, not ours: stays on stdout. See the stream split above
    // register_builtins — stdout is the game's, stderr is the port's.
    t["puts"] = [](Machine& m, uint32_t esp) {
        std::printf("%s\n", m.cstr(arg(m, esp, 0)).c_str());
        return 1u;
    };
    t["printf"] = [this](Machine& m, uint32_t esp) {
        std::string s = format(m, m.cstr(arg(m, esp, 0)), esp, 1);
        std::fputs(s.c_str(), stdout);
        return (uint32_t)s.size();
    };
    t["fprintf"] = [this](Machine& m, uint32_t esp) {   // arg0 = FILE*, ignored
        std::string s = format(m, m.cstr(arg(m, esp, 1)), esp, 2);
        std::fputs(s.c_str(), stderr);
        return (uint32_t)s.size();
    };
    t["sprintf"] = [this](Machine& m, uint32_t esp) {
        uint32_t buf = arg(m, esp, 0);
        std::string s = format(m, m.cstr(arg(m, esp, 1)), esp, 2);
        m.write(buf, s.c_str(), (uint32_t)s.size() + 1);
        return (uint32_t)s.size();
    };
    // vsprintf(buf, fmt, va_list ap). On i386 cdecl a va_list is just a pointer
    // into the caller's stack at the first vararg, so it reuses format() by
    // handing it a base such that arg(base, 0) == [ap]: format reads
    // esp + 4 + 4*i, hence ap - 4.
    //
    // This was the last unimplemented libc symbol libmvos actually calls, and it
    // is the entry point of cConsole::Input — so every console command silently
    // formatted into an unwritten buffer. Nothing downstream could work.
    t["vsprintf"] = [this](Machine& m, uint32_t esp) {
        uint32_t buf = arg(m, esp, 0);
        uint32_t ap  = arg(m, esp, 2);
        std::string s = format(m, m.cstr(arg(m, esp, 1)), ap - 4, 0);
        m.write(buf, s.c_str(), (uint32_t)s.size() + 1);
        return (uint32_t)s.size();
    };
    t["sscanf"] = [](Machine& m, uint32_t esp) { return do_sscanf(m, esp); };

    t["__write"] = [](Machine& m, uint32_t esp) {
        uint32_t fd = arg(m, esp, 0), p = arg(m, esp, 1), n = arg(m, esp, 2);
        std::vector<uint8_t> b(n);
        if (n) m.read(p, b.data(), n);
        std::fwrite(b.data(), 1, n, fd == 2 ? stderr : stdout);
        return n;
    };

    // ---- abort / exit -------------------------------------------------------
    // Fatal() ends in abort. Default (bring-up): log and return so the caller
    // can continue past non-critical Fatals. THEOC_LOUD_ABORT=1: dump a guest
    // backtrace and stop the current call() so a real fault surfaces here
    // instead of hiding as a silent OpenSubsystems restart / continued run.
    // exit/_exit always stop the current call() cleanly (no guest EH unwind).
    t["abort"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        static const bool loud = std::getenv("THEOC_LOUD_ABORT") != nullptr;
        if (!loud) {
            // An ignored abort returns into guest code that has already decided
            // it cannot continue, so control flow past this point is undefined —
            // and the comment above names what that looks like in practice: a
            // silent OpenSubsystems restart. On Windows it did exactly that and
            // spun forever, writing the same 13 lines until the user killed it.
            //
            // So the ignore is now *bounded*. A healthy run aborts zero times;
            // the cap is set far above any legitimate count and far below
            // infinity, and tripping it prints the diagnosis rather than making
            // someone infer it from a repeating log.
            static int ignored = 0;
            const int cap = []{
                const char* e = std::getenv("THEOC_ABORT_CAP");
                int v = e ? std::atoi(e) : 32;
                return v > 0 ? v : 32;
            }();
            if (++ignored > cap) {
                std::fprintf(stderr,
                    "\n  [abort] %d ignored aborts — the guest is looping on a Fatal it\n"
                    "          cannot get past, and continuing would only repeat it.\n"
                    "          Re-run with THEOC_LOUD_ABORT=1 for the guest backtrace\n"
                    "          that says which Fatal (THEOC_ABORT_CAP raises this cap).\n",
                    ignored - 1);
                m.request_stop();
                return 0;
            }
            if (rl_allow("abort-ignored", 5, std::chrono::seconds(60)))
                std::fprintf(stderr,
                             "  [abort] ignored (bring-up; THEOC_LOUD_ABORT=1 to trap)\n");
            return 0;
        }
        const uint32_t mvos = mvos_base_ ? mvos_base_ : 0x10000000u;
        auto label = [mvos](uint32_t a) -> std::string {
            char b[48];
            if (a >= mvos && a < mvos + 0x200000)
                std::snprintf(b, sizeof b, "mvos+%#x", a - mvos);
            else if (a >= 0x08048000 && a < 0x08a00000)
                std::snprintf(b, sizeof b, "game %#010x", a);
            else
                std::snprintf(b, sizeof b, "%#010x", a);
            return b;
        };
        std::fprintf(stderr, "\n=== [abort] LOUD: guest abort()/Fatal — backtrace ===\n");
        try {
            std::fprintf(stderr, "  called from %s\n", label(m.r32(esp)).c_str());
        } catch (...) {}
        // Walk the g++ 2.95 EBP frame chain: [ebp]=saved ebp, [ebp+4]=ret addr.
        uint32_t ebp = m.reg(UC_X86_REG_EBP);
        for (int i = 0; i < 24 && ebp; ++i) {
            uint32_t ret = 0, next = 0;
            try { ret = m.r32(ebp + 4); next = m.r32(ebp); } catch (...) { break; }
            if (ret) std::fprintf(stderr, "  #%-2d %s\n", i, label(ret).c_str());
            if (next <= ebp) break;   // frame pointers must ascend, else bail
            ebp = next;
        }
        std::fprintf(stderr, "=== stopping call (unset THEOC_LOUD_ABORT to continue past) ===\n\n");
        m.request_stop();
        return 0;
    };
    auto stop = [](Machine& m, uint32_t) -> uint32_t {
        m.request_stop();
        return 0;
    };
    t["exit"]  = stop;
    t["_exit"] = stop;

    t["__errno_location"] = [](Machine& m, uint32_t) -> uint32_t {
        return ERRNO_ADDR;
    };

    t["getenv"] = [](Machine& m, uint32_t esp) -> uint32_t {
        // Return 0 (NULL) — env not needed for asset load; avoids dangling host ptrs.
        (void)m; (void)esp;
        return 0;
    };

    t["strncmp"] = [](Machine& m, uint32_t esp) -> uint32_t {
        std::string a = m.cstr(arg(m, esp, 0)), b = m.cstr(arg(m, esp, 1));
        uint32_t n = arg(m, esp, 2);
        return (uint32_t)(int32_t)std::strncmp(a.c_str(), b.c_str(), n);
    };
    t["memcmp"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t a = arg(m, esp, 0), b = arg(m, esp, 1), n = arg(m, esp, 2);
        std::vector<uint8_t> A(n), B(n);
        if (n) { m.read(a, A.data(), n); m.read(b, B.data(), n); }
        return (uint32_t)(int32_t)std::memcmp(A.data(), B.data(), n);
    };
    t["__strtol_internal"] = [](Machine& m, uint32_t esp) -> uint32_t {
        // glibc: strtol(nptr, endptr, base, group) — ignore group.
        std::string s = m.cstr(arg(m, esp, 0));
        uint32_t endp = arg(m, esp, 1);
        int base = (int)arg(m, esp, 2);
        char* end = nullptr;
        long v = std::strtol(s.c_str(), &end, base);
        if (endp && end) {
            uint32_t off = (uint32_t)(end - s.c_str());
            m.w32(endp, arg(m, esp, 0) + off);
        }
        return (uint32_t)v;
    };
    t["__strtod_internal"] = [](Machine& m, uint32_t esp) -> uint32_t {
        // Returns double in st0 — use return_double. Simplified: return 0.0.
        std::string s = m.cstr(arg(m, esp, 0));
        uint32_t endp = arg(m, esp, 1);
        char* end = nullptr;
        double v = std::strtod(s.c_str(), &end);
        if (endp && end) {
            uint32_t off = (uint32_t)(end - s.c_str());
            m.w32(endp, arg(m, esp, 0) + off);
        }
        m.return_double(v);
        return 0;
    };
    t["gettimeofday"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        // struct timeval { time_t tv_sec; suseconds_t tv_usec; } — 32-bit each on i386.
        fps_gettime_calls_++;
        uint32_t tv = arg(m, esp, 0);
        struct timeval host{};
        gettimeofday(&host, nullptr);
        if (tv) {
            m.w32(tv, (uint32_t)host.tv_sec);
            m.w32(tv + 4, (uint32_t)host.tv_usec);
        }
        return 0;
    };
    t["usleep"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t req = arg(m, esp, 0);
        fps_usleep_calls_++;
        // The game's frame limiter (cSyncSystem::Sleep) sleeps here waiting for
        // its 30Hz heartbeat (SIGALRM → _TimerFunction). On real Linux the signal
        // interrupts usleep and runs the handler; our heartbeat is otherwise only
        // serviced at SwapBuffers, so a present-coupled timer runs at ~6Hz and the
        // limiter over-sleeps (~68ms), pinning province at 12fps. Deliver a due
        // tick right here, exactly as EINTR-from-SIGALRM would (THEOC_LEGACY_SLEEP
        // reverts to the old blind sleep for A/B).
        static const bool legacy = std::getenv("THEOC_LEGACY_SLEEP") != nullptr;
        if (legacy) {  // pre-2026-08-03 blind sleep, for A/B
            uint32_t us = req > 100000 ? 100000 : req;
            fps_usleep_us_ += us;
            theoc_sleep_us(us);
            return 0;
        }

        // Are we resuming a sleep we interrupted to deliver a tick? Then the
        // remainder is ours, not the guest's argument — which on re-entry is
        // still the *original* request and would restart the sleep from the top.
        // Guarded on the return address too, in case a spliced handler ever
        // calls usleep itself.
        uint32_t remaining = req;
        bool resumed = false;
        if (sleep_resuming_) {
            sleep_resuming_ = false;
            if (m.r32(esp) == sleep_resume_ret_) { remaining = sleep_remaining_us_; resumed = true; }
            sleep_remaining_us_ = 0;
        }
        if (remaining > 1000000) remaining = 1000000;  // sanity clamp

        // A tick we spliced has just run cIntuition::TimerProc, which repainted
        // the pointer into the LFB and flushed it through cGD_LFB16::Refresh.
        // This is the safe moment to show it: the frame limiter sleeps at the
        // *top* of cProvince_Do, so the LFB currently holds the last completed
        // frame, and TimerProc's erase/repaint pair has both finished. Skipped
        // when the scene is already presenting faster than the 30Hz heartbeat
        // (the realm screen), where an extra present would buy nothing.
        if (resumed && gd_refresh_dirty_) {
            auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - last_present_).count();
            if (last_present_.time_since_epoch().count() == 0 || since >= 25)
                present_async_cursor(m);
            else
                gd_refresh_dirty_ = false;
        }

        // Sound is buffer-driven with a ~120ms cushion, and its entry argument
        // (the cThread*) can't survive the re-entry frame, so it is serviced on
        // a *fresh* entry only. Doing it on a resume would splice back to the
        // original caller and silently drop the rest of the sleep — which the
        // game's elapsed-based limiter would absorb, but only by running an
        // extra full cProvince_Do body. A slice that comes due mid-sleep waits
        // for the next yield instead; the cushion absorbs it (measured 0
        // underruns/s in steady state, province and realm alike).
        if (!resumed && maybe_redirect_sound(m, esp)) return 0;

        // Sleep the FULL requested duration, delivering heartbeats *during* it
        // the way the kernel does — rather than returning at the first due tick.
        // Truncating is what cut the game's own 83ms province limiter to ~33ms
        // and made the sim run ~2.5x fast (frame-timing.md, Bug 2).
        for (;;) {
            if (!remaining) return 0;
            auto now = std::chrono::steady_clock::now();
            if (timer_armed_ && timer_next_ <= now) {
                if (timer_handler_ignores_signo() &&
                    redirect_timer_reentrant(m, esp, remaining))
                    return 0;                          // resumes on re-entry
                // Custom signo-reading handler: fall back to the old truncating
                // delivery rather than build a frame it would misread.
                if (maybe_redirect_timer(m, esp)) return 0;
                // Neither path fired (no mvos base, no usleep trap, timer
                // disarmed mid-flight). Push the schedule past now anyway, or
                // the zero-length slice below would spin forever.
                if (timer_armed_) advance_timer_schedule();
            }
            uint32_t slice = remaining;
            if (timer_armed_) {
                auto until = std::chrono::duration_cast<std::chrono::microseconds>(
                                 timer_next_ - now).count();
                if (until < 0) until = 0;
                if ((uint64_t)until < slice) slice = (uint32_t)until;
            }
            if (!slice) continue;   // tick is due; next pass delivers it
            theoc_sleep_us(slice);
            fps_usleep_us_ += slice;
            remaining -= slice;
        }
    };
    t["ioctl"] = [](Machine& m, uint32_t esp) -> uint32_t {
        // /dev/dsp probes etc. — succeed with zeros.
        (void)m; (void)esp;
        return 0;
    };
    t["strcat"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1);
        std::string ds = m.cstr(d), ss = m.cstr(s);
        std::string out = ds + ss;
        m.write(d, out.c_str(), (uint32_t)out.size() + 1);
        return d;
    };
    t["strncat"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1), n = arg(m, esp, 2);
        std::string ds = m.cstr(d), ss = m.cstr(s);
        if (ss.size() > n) ss.resize(n);
        std::string out = ds + ss;
        m.write(d, out.c_str(), (uint32_t)out.size() + 1);
        return d;
    };
    // ---- real BSD sockets ---------------------------------------------------
    // Was: unconditional lies (socket->32, bind->0, recv->0). Those were enough
    // for the single-instance lock and nothing else.
    //
    // THE SINGLE-INSTANCE LOCK IS DELIBERATELY STILL FAKED. cApplication::Start
    // binds localhost:5043 and Fatal()s if it is taken — "You can run only one
    // Theocracy in the same time!". Honouring that would make the *second*
    // instance die, which is exactly the two-client-on-one-Mac setup we need to
    // test multiplayer with. So bind() on port 5043 succeeds without touching the
    // network; every other port is real. THEOC_REAL_LOCK=1 restores the original
    // behaviour if we ever want to observe it.
    t["socket"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int dom = (int)arg(m, esp, 0), type = (int)arg(m, esp, 1),
            proto = (int)arg(m, esp, 2);
        if (dom != (int)kGuestAfInet) {   // IPX etc. — not supported, fail cleanly
            set_errno(m, 97);             // EAFNOSUPPORT (Linux)
            return (uint32_t)-1;
        }
        // SOCK_STREAM=1 / SOCK_DGRAM=2 agree between Linux and BSD.
        int hfd = (int)::socket(AF_INET, type, proto);
        if (hfd < 0) { set_errno(m, last_socket_errno()); return (uint32_t)-1; }
        // Address reuse by default: the game rebinds its listen port across
        // sessions, and BSD's TIME_WAIT would otherwise refuse for ~a minute.
        //
        // Windows note: SO_REUSEADDR there is *not* the POSIX option — it permits
        // another process to hijack a live bound port, where POSIX only relaxes
        // TIME_WAIT. It is kept anyway because it reproduces the rebind behaviour
        // the game depends on; SO_EXCLUSIVEADDRUSE would be the hardening move if
        // this ever faced a hostile network, which a LAN game from 2000 does not.
        int on = 1;
        ::setsockopt(hfd, SOL_SOCKET, SO_REUSEADDR, THEOC_SOCK_CAST(&on), sizeof on);
        // SO_NOSIGPIPE: writing to a socket whose peer has gone must return EPIPE,
        // not raise SIGPIPE and kill the host. Linux code says MSG_NOSIGNAL per
        // send(); BSD sets it once on the fd. Belt and braces with the global
        // SIGPIPE ignore in the `signal` handler — this one also covers the case
        // where guest code never asked.
        //
        // On Linux only that global ignore is left, and it is enough in practice:
        // libmvos `main()`'s very first act is signal(SIGPIPE, SIG_IGN), which we
        // honour on the host. If a future path ever writes to a socket before the
        // guest has asked, the fix is MSG_NOSIGNAL on the send, not this option.
#ifdef THEOC_HAVE_SO_NOSIGPIPE
        ::setsockopt(hfd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
        int gfd = adopt_host_fd(hfd, /*is_socket=*/true);
        std::fprintf(stderr, "  [net] socket(type=%d) -> guest fd %d\n", type, gfd);
        return (uint32_t)gfd;
    };

    t["bind"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int gfd = (int)arg(m, esp, 0);
        uint32_t gaddr = arg(m, esp, 1), glen = arg(m, esp, 2);
        int hfd = host_fd_of(gfd);
        sockaddr_in sa{};
        if (!guest_to_host_sin(m, gaddr, glen, sa)) { set_errno(m, 22); return (uint32_t)-1; }
        uint16_t port = ntohs(sa.sin_port);
        static const bool real_lock = std::getenv("THEOC_REAL_LOCK") != nullptr;
        if (port == 5043 && !real_lock) {
            // Bind for real, to an ephemeral loopback port. The purpose of the
            // fake is only to avoid *holding* :5043 — so a second instance can
            // boot, which is what multiplayer testing on one machine needs — not
            // to leave the socket unbound.
            //
            // Returning 0 without binding worked on macOS and Linux **by
            // accident**: POSIX listen() auto-binds an unbound socket to an
            // ephemeral port (verified: listen() returns 0 and getsockname then
            // reports a kernel-chosen port), so the game's next call succeeded
            // regardless. Winsock has no such behaviour — listen() on an unbound
            // socket fails with WSAEINVAL, the lock check reads that as "port
            // taken", and the game Fatals with "You can run only one Theocracy in
            // the same time!". Binding here makes all three platforms take the
            // same path instead of two of them relying on an implicit bind.
            if (hfd >= 0) {
                sockaddr_in eph{};
#ifdef THEOC_HAVE_SIN_LEN
                eph.sin_len = sizeof eph;
#endif
                eph.sin_family = AF_INET;
                eph.sin_port   = 0;                          // kernel picks
                eph.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                if (::bind(hfd, (sockaddr*)&eph, sizeof eph) < 0)
                    std::fprintf(stderr, "  [net] bind(:5043) stand-in bind failed "
                                "(harmless unless listen() follows)\n");
            }
            std::fprintf(stderr, "  [net] bind(:5043) faked OK — single-instance lock "
                        "(THEOC_REAL_LOCK=1 to honour it)\n");
            return 0;
        }
        if (hfd < 0) { set_errno(m, 9); return (uint32_t)-1; }
        if (::bind(hfd, (sockaddr*)&sa, sizeof sa) < 0) {
            int e = errno;
            std::fprintf(stderr, "  [net] bind(:%u) failed: %s\n", port, std::strerror(e));
            set_errno(m, to_linux_errno(e));
            return (uint32_t)-1;
        }
        std::fprintf(stderr, "  [net] bind(:%u) ok\n", port);
        return 0;
    };

    t["listen"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        if (hfd < 0) return 0;   // the faked lock socket never got a real fd
        if (::listen(hfd, (int)arg(m, esp, 1)) < 0) {
            set_errno(m, to_linux_errno(errno));
            return (uint32_t)-1;
        }
        return 0;
    };

    t["accept"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        uint32_t gaddr = arg(m, esp, 1), glen_ptr = arg(m, esp, 2);
        if (hfd < 0) { set_errno(m, 9); return (uint32_t)-1; }
        sockaddr_in peer{};
        socklen_t_compat plen = sizeof peer;
        int c = ::accept(hfd, (sockaddr*)&peer, &plen);
        if (c < 0) { set_errno(m, to_linux_errno(errno)); return (uint32_t)-1; }
        host_to_guest_sin(m, gaddr, glen_ptr, peer);
        int gfd = adopt_host_fd(c, /*is_socket=*/true);
        std::fprintf(stderr, "  [net] accept -> guest fd %d from %s:%u\n", gfd,
                    inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
        return (uint32_t)gfd;
    };

    t["connect"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        sockaddr_in sa{};
        if (!guest_to_host_sin(m, arg(m, esp, 1), arg(m, esp, 2), sa)) {
            set_errno(m, 22); return (uint32_t)-1;
        }
        if (hfd < 0) { set_errno(m, 9); return (uint32_t)-1; }
        int r = ::connect(hfd, (sockaddr*)&sa, sizeof sa);
        if (r < 0) {
            int e = errno;
            std::fprintf(stderr, "  [net] connect(%s:%u) -> %s\n", inet_ntoa(sa.sin_addr),
                        ntohs(sa.sin_port), std::strerror(e));
            set_errno(m, to_linux_errno(e));
            return (uint32_t)-1;
        }
        std::fprintf(stderr, "  [net] connect(%s:%u) ok\n", inet_ntoa(sa.sin_addr),
                    ntohs(sa.sin_port));
        return 0;
    };

    t["send"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        uint32_t buf = arg(m, esp, 1), n = arg(m, esp, 2);
        if (hfd < 0) { set_errno(m, 9); return (uint32_t)-1; }
        std::vector<uint8_t> tmp(n);
        if (n) m.read(buf, tmp.data(), n);
        // MSG_NOSIGNAL does not exist on BSD; SO_NOSIGPIPE is set on the fd
        // instead (below) so a dead peer returns EPIPE rather than killing us.
        ssize_t s = ::send(hfd, THEOC_SOCK_CAST(tmp.data()), n, 0);
        if (s < 0) { set_errno(m, to_linux_errno(errno)); return (uint32_t)-1; }
        return (uint32_t)s;
    };

    t["recv"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        uint32_t buf = arg(m, esp, 1), n = arg(m, esp, 2);
        if (hfd < 0) { set_errno(m, 9); return (uint32_t)-1; }
        std::vector<uint8_t> tmp(n);
        ssize_t got = ::recv(hfd, THEOC_SOCK_CAST(tmp.data()), n, 0);
        if (got < 0) { set_errno(m, to_linux_errno(errno)); return (uint32_t)-1; }
        if (got > 0) m.write(buf, tmp.data(), (uint32_t)got);
        return (uint32_t)got;
    };

    t["sendto"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        uint32_t buf = arg(m, esp, 1), n = arg(m, esp, 2);
        uint32_t gaddr = arg(m, esp, 4), glen = arg(m, esp, 5);
        if (hfd < 0) { set_errno(m, 9); return (uint32_t)-1; }
        std::vector<uint8_t> tmp(n);
        if (n) m.read(buf, tmp.data(), n);
        sockaddr_in sa{};
        bool have = guest_to_host_sin(m, gaddr, glen, sa);
        ssize_t s = ::sendto(hfd, THEOC_SOCK_CAST(tmp.data()), n, 0,
                             have ? (sockaddr*)&sa : nullptr, have ? sizeof sa : 0);
        if (s < 0) { set_errno(m, to_linux_errno(errno)); return (uint32_t)-1; }
        return (uint32_t)s;
    };

    t["recvfrom"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        uint32_t buf = arg(m, esp, 1), n = arg(m, esp, 2);
        uint32_t gaddr = arg(m, esp, 4), glen_ptr = arg(m, esp, 5);
        if (hfd < 0) { set_errno(m, 9); return (uint32_t)-1; }
        std::vector<uint8_t> tmp(n);
        sockaddr_in peer{};
        socklen_t_compat plen = sizeof peer;
        ssize_t got = ::recvfrom(hfd, THEOC_SOCK_CAST(tmp.data()), n, 0, (sockaddr*)&peer, &plen);
        if (got < 0) { set_errno(m, to_linux_errno(errno)); return (uint32_t)-1; }
        if (got > 0) m.write(buf, tmp.data(), (uint32_t)got);
        host_to_guest_sin(m, gaddr, glen_ptr, peer);
        return (uint32_t)got;
    };

    // Linux SOL_SOCKET=1 / SO_REUSEADDR=2 vs BSD 0xffff / 4. Only the options the
    // engine actually sets are translated; anything else is accepted and ignored
    // rather than passed through with a wrong number.
    t["setsockopt"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        int level = (int)arg(m, esp, 1), opt = (int)arg(m, esp, 2);
        uint32_t val = arg(m, esp, 3), len = arg(m, esp, 4);
        if (hfd < 0) return 0;
        if (level == 1 && (opt == 2 /*SO_REUSEADDR*/ || opt == 9 /*SO_KEEPALIVE*/)) {
            int v = 1;
            if (val && len >= 4) v = (int)m.r32(val);
            ::setsockopt(hfd, SOL_SOCKET, opt == 2 ? SO_REUSEADDR : SO_KEEPALIVE,
                         THEOC_SOCK_CAST(&v), sizeof v);
        }
        return 0;
    };
    t["getsockopt"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t val = arg(m, esp, 3);
        if (val) m.w32(val, 0);   // "no error" — the only thing this is used for
        return 0;
    };
    t["getsockname"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        if (hfd < 0) return (uint32_t)-1;
        sockaddr_in sa{};
        socklen_t_compat l = sizeof sa;
        if (::getsockname(hfd, (sockaddr*)&sa, &l) < 0) return (uint32_t)-1;
        host_to_guest_sin(m, arg(m, esp, 1), arg(m, esp, 2), sa);
        return 0;
    };
    t["shutdown"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int hfd = host_fd_of((int)arg(m, esp, 0));
        if (hfd >= 0) ::shutdown(hfd, (int)arg(m, esp, 1));
        return 0;
    };
    t["inet_addr"] = [](Machine& m, uint32_t esp) -> uint32_t {
        std::string s = m.cstr(arg(m, esp, 0));
        return (uint32_t)inet_addr(s.c_str());
    };
    t["htonl"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t v = arg(m, esp, 0);
        return ((v & 0xff) << 24) | ((v & 0xff00) << 8) |
               ((v >> 8) & 0xff00) | (v >> 24);
    };
    t["ntohl"] = t["htonl"];
    t["htons"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t v = arg(m, esp, 0) & 0xffff;
        return ((v & 0xff) << 8) | (v >> 8);
    };
    t["ntohs"] = t["htons"];
    t["pipe"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t p = arg(m, esp, 0);
        if (p) { m.w32(p, 40); m.w32(p + 4, 41); }
        return 0;
    };
    // cIPCO_TCPIP does fcntl(fd, F_SETFL, 0x800) — Linux O_NONBLOCK. On BSD that
    // bit is 0x0004, and passing 0x800 through would set O_ASYNC|junk and leave
    // the socket BLOCKING, which would wedge the whole single-threaded emulator on
    // the first recv. Translate the flag set, do not forward it.
    // Directory enumeration. libmvos imports opendir/readdir/chdir and its
    // cDirectory::Open (mvos 0x4bab0) drives them; the netgame map-selection
    // dialog enumerates data/map/netgame this way. All three were unimplemented.
    //
    // Guest `struct dirent` is the Linux/i386 non-LFS layout:
    //   +0x00 u32 d_ino   +0x04 u32 d_off   +0x08 u16 d_reclen
    //   +0x0a u8  d_type  +0x0b char d_name[256]
    // d_name is at offset 11 (NOT 12 — the struct is unpadded here), and the
    // DT_* constants agree between Linux and BSD, so d_type passes through.
    t["opendir"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string gp = m.cstr(arg(m, esp, 0));
        std::string hp = resolve_path(gp);
        DIR* d = ::opendir(hp.c_str());
        if (!d) {
            std::fprintf(stderr, "  [dir] opendir('%s' -> '%s') failed\n", gp.c_str(), hp.c_str());
            set_errno(m, 2 /*ENOENT*/);
            return 0;
        }
        uint32_t h = next_dir_++;
        uint32_t ent = guest_alloc(0x120);          // dirent incl. 256-byte name
        dirs_[h] = HostDir{(void*)d, ent, hp};
        std::fprintf(stderr, "  [dir] opendir('%s') -> handle %#x\n", gp.c_str(), h);
        return h;
    };
    t["readdir"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        auto it = dirs_.find(h);
        if (it == dirs_.end() || !it->second.d || !it->second.ent) return 0;
        struct dirent* e = ::readdir((DIR*)it->second.d);
        if (!e) return 0;                            // end of directory
        const uint32_t p = it->second.ent;
        std::string name = e->d_name;
        if (name.size() > 255) name.resize(255);
        m.w32(p + 0x00, (uint32_t)e->d_ino);
        m.w32(p + 0x04, 0);
        uint16_t reclen = (uint16_t)(0x0b + name.size() + 1);
#if defined(_WIN32)
        // mingw's struct dirent carries no d_type. cDirent reads this byte, so
        // derive it rather than leave it 0 (DT_UNKNOWN), which would make every
        // entry look like a non-directory.
        uint8_t dtype = 0;   // DT_UNKNOWN
        {
            struct stat dst{};
            std::string full = it->second.path + "/" + name;
            if (::stat(full.c_str(), &dst) == 0)
                dtype = S_ISDIR(dst.st_mode) ? 4 /*DT_DIR*/ : 8 /*DT_REG*/;
        }
#else
        uint8_t  dtype  = (uint8_t)e->d_type;
#endif
        m.write(p + 0x08, &reclen, 2);
        m.write(p + 0x0a, &dtype, 1);
        m.write(p + 0x0b, name.c_str(), (uint32_t)name.size() + 1);
        return p;
    };
    t["closedir"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        auto it = dirs_.find(h);
        if (it == dirs_.end()) return (uint32_t)-1;
        if (it->second.d) ::closedir((DIR*)it->second.d);
        if (it->second.ent) guest_release(it->second.ent);
        dirs_.erase(it);
        return 0;
    };
    t["rewinddir"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        auto it = dirs_.find(arg(m, esp, 0));
        if (it != dirs_.end() && it->second.d) ::rewinddir((DIR*)it->second.d);
        return 0;
    };
    // chdir: accepted and ignored. Every guest path already goes through
    // resolve_path() against $THEOC_DATA, so honouring a real host chdir would
    // desynchronise that mapping rather than help.
    t["chdir"] = [](Machine& m, uint32_t esp) -> uint32_t {
        (void)m; (void)esp;
        return 0;
    };

    // cIPCO_TCPIP does fcntl(fd, F_SETFL, 0x800) — Linux O_NONBLOCK — to make its
    // polling socket non-blocking, so this is the switch that decides whether the
    // netgame polls or hangs.
    //
    // Windows has no fcntl at all. The equivalent is ioctlsocket(FIONBIO), which
    // is *write-only*: there is no way to read the current blocking state back.
    // So the flag is remembered host-side in HostFile::nonblock and F_GETFL is
    // answered from that, which is exact here because this handler is the only
    // thing that ever changes it.
    t["fcntl"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int gfd = (int)arg(m, esp, 0), cmd = (int)arg(m, esp, 1);
        uint32_t a = arg(m, esp, 2);
        auto it = fds_.find(gfd);
        if (it == fds_.end() || it->second.host_fd < 0) return 0;  // faked fd: succeed
        int hfd = it->second.host_fd;
        if (cmd == 4 /*F_SETFL*/) {
            bool want_nb = (a & 0x800u) != 0;
#if defined(_WIN32)
            u_long nb = want_nb ? 1 : 0;
            if (::ioctlsocket((SOCKET)hfd, FIONBIO, &nb) != 0) {
                set_errno(m, last_socket_errno());
                return (uint32_t)-1;
            }
#else
            int fl = ::fcntl(hfd, F_GETFL, 0);
            if (fl < 0) fl = 0;
            if (want_nb) fl |= O_NONBLOCK; else fl &= ~O_NONBLOCK;
            if (::fcntl(hfd, F_SETFL, fl) < 0) {
                set_errno(m, to_linux_errno(errno));
                return (uint32_t)-1;
            }
#endif
            it->second.nonblock = want_nb;
            return 0;
        }
        if (cmd == 3 /*F_GETFL*/) {
#if defined(_WIN32)
            return it->second.nonblock ? 0x800u : 0u;
#else
            int fl = ::fcntl(hfd, F_GETFL, 0);
            if (fl < 0) return (uint32_t)-1;
            return (fl & O_NONBLOCK) ? 0x800u : 0u;   // report in Linux terms
#endif
        }
        return 0;
    };

    // Real select over the guest's fd_set (1024 bits, 32-bit words on i386),
    // translating guest fds to host fds and the ready set back.
    //
    // The timeout is CAPPED: we are single-threaded, so honouring a long guest
    // timeout would freeze rendering, input and the audio slice for its duration.
    // Capping makes select return "nothing ready" early and the guest poll again,
    // which is exactly what its non-blocking design already expects.
    t["select"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        fps_select_calls_++;
        int nfds = (int)arg(m, esp, 0);
        uint32_t gr = arg(m, esp, 1), gw = arg(m, esp, 2), ge = arg(m, esp, 3);
        uint32_t gt = arg(m, esp, 4);
        if (nfds < 0) nfds = 0;
        if (nfds > 1024) nfds = 1024;

        struct Set { uint32_t g; fd_set h; std::vector<std::pair<int,int>> map; };
        Set sets[3] = {{gr, {}, {}}, {gw, {}, {}}, {ge, {}, {}}};
        int maxh = -1;
        for (auto& s : sets) {
            FD_ZERO(&s.h);
            if (!s.g) continue;
            for (int fd = 0; fd < nfds; ++fd) {
                uint32_t word = m.r32(s.g + (uint32_t)(fd / 32) * 4);
                if (!(word & (1u << (fd % 32)))) continue;
                int hfd = host_fd_of(fd);
                if (hfd < 0) continue;          // faked fd: never ready
                FD_SET(hfd, &s.h);
                s.map.push_back({fd, hfd});
                if (hfd > maxh) maxh = hfd;
            }
        }

        timeval tv{0, 0};
        constexpr long kCapUs = 20000;          // 20 ms — under one 12fps frame
        if (gt) {
            long sec = (long)m.r32(gt), usec = (long)m.r32(gt + 4);
            long total = sec > 1000 ? kCapUs : sec * 1000000 + usec;
            tv.tv_usec = total > kCapUs ? kCapUs : (total < 0 ? 0 : total);
        } else {
            tv.tv_usec = kCapUs;                // NULL = block forever; we must not
        }
        int r = ::select(maxh + 1, sets[0].g ? &sets[0].h : nullptr,
                         sets[1].g ? &sets[1].h : nullptr,
                         sets[2].g ? &sets[2].h : nullptr, &tv);
        if (r < 0) {
            if (errno == EINTR) return 0;
            set_errno(m, to_linux_errno(errno));
            return (uint32_t)-1;
        }
        // Rewrite the guest sets to exactly the ready fds.
        int ready = 0;
        for (auto& s : sets) {
            if (!s.g) continue;
            for (int w = 0; w < (nfds + 31) / 32; ++w) m.w32(s.g + (uint32_t)w * 4, 0);
            for (auto& [gfd, hfd] : s.map) {
                if (!FD_ISSET(hfd, &s.h)) continue;
                uint32_t off = s.g + (uint32_t)(gfd / 32) * 4;
                m.w32(off, m.r32(off) | (1u << (gfd % 32)));
                ready++;
            }
        }
        return (uint32_t)ready;
    };

    // Must be real: cIPCO_TCPIP's ctor calls this FIRST and gives up with perror()
    // if it returns NULL — which the old stub always did, so a connect could never
    // even be attempted. Builds a Linux `struct hostent` in guest memory:
    //   +0x00 h_name  +0x04 h_aliases  +0x08 h_addrtype  +0x0c h_length
    //   +0x10 h_addr_list
    // One reusable block, matching the real function's static-storage contract.
    t["gethostbyname"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string host = m.cstr(arg(m, esp, 0));
        uint32_t ip = INADDR_NONE;   // in_addr_t is POSIX-only; the value is a u32
        if (host == "localhost" || host.empty()) {
            ip = htonl(INADDR_LOOPBACK);
        } else {
            ip = inet_addr(host.c_str());
            if (ip == INADDR_NONE) {
                addrinfo hints{}, *res = nullptr;
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
                    ip = ((sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
                    freeaddrinfo(res);
                } else {
                    std::fprintf(stderr, "  [net] gethostbyname('%s') failed\n", host.c_str());
                    return 0;   // guest prints perror() and gives up, as designed
                }
            }
        }
        if (!hostent_buf_) hostent_buf_ = guest_alloc(0x80);
        if (!hostent_buf_) return 0;
        const uint32_t he      = hostent_buf_;
        const uint32_t addrlst = he + 0x14;   // char*[2]
        const uint32_t aliases = he + 0x1c;   // char*[1] = {NULL}
        const uint32_t addr    = he + 0x20;   // in_addr
        const uint32_t name    = he + 0x24;
        std::string nm = host.substr(0, 0x58);
        m.write(name, nm.c_str(), (uint32_t)nm.size() + 1);
        m.w32(addr, (uint32_t)ip);
        m.w32(addrlst, addr);
        m.w32(addrlst + 4, 0);
        m.w32(aliases, 0);
        m.w32(he + 0x00, name);
        m.w32(he + 0x04, aliases);
        m.w32(he + 0x08, kGuestAfInet);   // h_addrtype = AF_INET
        m.w32(he + 0x0c, 4);              // h_length
        m.w32(he + 0x10, addrlst);
        std::fprintf(stderr, "  [net] gethostbyname('%s') -> %s\n", host.c_str(),
                    inet_ntoa(in_addr{ip}));
        return he;
    };
    for (const char* nm : {"sem_init", "sem_destroy", "sem_post", "sem_wait",
                           "sem_trywait", "sem_getvalue",
                           "sigemptyset", "sigaddset", "signal", "kill", "waitpid",
                           "fork", "execlp"})
        t[nm] = [](Machine&, uint32_t) -> uint32_t { return 0; };

    // signal() was in that stub list, which was harmless only while sockets were
    // fake. libmvos main()'s FIRST act is signal(SIGPIPE, SIG_IGN) — the IPC layer
    // depends on it — and swallowing that meant the HOST kept the default
    // disposition. The moment a real peer disconnected mid-write the whole process
    // died with SIGPIPE (observed: server exited 141 = 128+13 when a test client
    // dropped). Honour the dispositions the guest actually asks for.
    t["signal"] = [](Machine& m, uint32_t esp) -> uint32_t {
        int sig = (int)arg(m, esp, 0);
        uint32_t h = arg(m, esp, 1);       // 0 = SIG_DFL, 1 = SIG_IGN, else handler
        if (sig == 13 /*SIGPIPE on both Linux and BSD*/ && h == 1) {
#if defined(SIGPIPE)
            ::signal(SIGPIPE, SIG_IGN);
            std::fprintf(stderr, "  [net] SIGPIPE ignored (guest requested)\n");
#else
            // Windows has no SIGPIPE: a send to a dead peer returns
            // WSAECONNRESET/WSAESHUTDOWN rather than raising anything, so the
            // request is already satisfied by the platform.
            std::fprintf(stderr, "  [net] SIGPIPE ignore requested; no-op on this host\n");
#endif
        }
        // Other signals stay stubbed: the guest's SIGALRM timer is delivered by
        // our own scheduler, not by real host signals.
        return 0;
    };

    // setitimer(ITIMER_REAL) — guest cLinuxTimer. Host polls and calls
    // TimerSystem::Proc (normally SIGALRM → _TimerFunction).
    t["setitimer"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        // int setitimer(int which, const struct itimerval *new, struct itimerval *old);
        uint32_t which = arg(m, esp, 0);
        uint32_t newp  = arg(m, esp, 1);
        uint32_t oldp  = arg(m, esp, 2);
        (void)which;  // only ITIMER_REAL (0) used
        auto write_old = [&]() {
            if (!oldp) return;
            // itimerval: interval {sec,usec}, value {sec,usec} — i386 16 bytes
            auto us = timer_interval_.count();
            uint32_t isec = (uint32_t)(us / 1'000'000), iusec = (uint32_t)(us % 1'000'000);
            us = timer_value_.count();
            uint32_t vsec = (uint32_t)(us / 1'000'000), vusec = (uint32_t)(us % 1'000'000);
            m.w32(oldp + 0, isec); m.w32(oldp + 4, iusec);
            m.w32(oldp + 8, vsec); m.w32(oldp + 12, vusec);
        };
        write_old();
        if (!newp) return 0;
        uint32_t isec = m.r32(newp + 0), iusec = m.r32(newp + 4);
        uint32_t vsec = m.r32(newp + 8), vusec = m.r32(newp + 12);
        timer_interval_ = std::chrono::microseconds(
            (int64_t)isec * 1'000'000LL + (int64_t)iusec);
        timer_value_ = std::chrono::microseconds(
            (int64_t)vsec * 1'000'000LL + (int64_t)vusec);
        if (timer_value_.count() == 0 && timer_interval_.count() == 0) {
            timer_armed_ = false;
            static int n;
            if (n++ < 4) std::fprintf(stderr, "  [timer] setitimer disarmed\n");
            return 0;
        }
        // One-shot value; reload from interval after each fire (POSIX).
        auto period = timer_value_.count() ? timer_value_ : timer_interval_;
        if (period.count() <= 0) period = std::chrono::milliseconds(20);
        if (timer_interval_.count() <= 0) timer_interval_ = period;
        timer_next_ = std::chrono::steady_clock::now() + period;
        timer_armed_ = true;
        if (!sigalrm_handler_ && mvos_base_)
            sigalrm_handler_ = mvos_base_ + 0x922e0;  // _TimerFunction__Fi
        static int n;
        if (n++ < 6)
            std::fprintf(stderr, "  [timer] setitimer value=%lld us interval=%lld us handler=%#x\n",
                        (long long)timer_value_.count(),
                        (long long)timer_interval_.count(),
                        sigalrm_handler_);
        return 0;
    };

    t["sigaction"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        // int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
        uint32_t sig = arg(m, esp, 0);
        uint32_t act = arg(m, esp, 1);
        // Linux SIGALRM = 14. sa_handler is first word of struct sigaction.
        if (sig == 14 && act) {
            uint32_t h = m.r32(act);
            if (h && h != 0 && h != 1) {  // not SIG_DFL/SIG_IGN
                sigalrm_handler_ = h;
                static int n;
                if (n++ < 4)
                    std::fprintf(stderr, "  [timer] sigaction SIGALRM handler=%#x\n", h);
            }
        }
        return 0;
    };

    // pthread_create: queue as a soft thread. cSoundCard_Linux::Launch relies on
    // this to start Main (mixer → write /dev/dsp). We don't spawn host threads
    // (Unicorn is single-threaded); HLE_SwapBuffers green-runs Entry each frame.
    t["pthread_create"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        // int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
        //                    void *(*start_routine)(void *), void *arg);
        uint32_t thread_out = arg(m, esp, 0);
        uint32_t entry = arg(m, esp, 2);
        uint32_t targ = arg(m, esp, 3);
        if (thread_out) m.w32(thread_out, 0x70000000u + (uint32_t)soft_threads_.size());
        if (entry && targ) {
            soft_threads_.push_back({entry, targ});
            patch_sound_main_oneshot(m);
            static int n;
            if (n++ < 6)
                std::fprintf(stderr, "  [thread] soft-thread entry=%#x arg=%#x (n=%zu)\n",
                            entry, targ, soft_threads_.size());
        }
        return 0;  // success — Launch keeps cThread.running = 1
    };
    t["pthread_join"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["pthread_detach"] = [](Machine&, uint32_t) -> uint32_t { return 0; };

    // ---- SMPEG (Philos/Loki MPEG) — real decode via libav, or skip ----------
    // SMPEGstatus: ERROR=-1, STOPPED=0, PLAYING=1. THEOC_SKIP_MOVIES=1 forces
    // open-ok + immediate STOPPED (no decode). Guest handle: +0 magic, +4 status.
    t["SMPEG_new"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string file = m.cstr(arg(m, esp, 0));
        uint32_t info = arg(m, esp, 1);
        std::string host = resolve_path(file);
        bool skip = std::getenv("THEOC_SKIP_MOVIES") != nullptr;
        bool exists = false;
        {
            FILE* fp = std::fopen(host.c_str(), "rb");
            if (fp) { std::fclose(fp); exists = true; }
            else {
                fp = std::fopen(file.c_str(), "rb");
                if (fp) { std::fclose(fp); exists = true; host = file; }
            }
        }
        if (!exists && !skip) {
            smpeg_error_ = "no movie found " + file + " (host " + host + ")";
            std::fprintf(stderr, "  [smpeg] SMPEG_new FAIL '%s' -> %s\n",
                        file.c_str(), host.c_str());
            return 0;
        }
        smpeg_error_.clear();
        uint32_t h = bump_alloc(32);
        if (!h) return 0;
        m.w32(h + 0, 1);   // magic
        m.w32(h + 4, 0);   // STOPPED until startplayvideo
        int w = 640, ht = 480;
        double fps = 15.0;
        if (!skip && exists) {
            if (mpeg_.load(h, host)) {
                if (auto* mov = mpeg_.get(h)) {
                    w = mov->width; ht = mov->height; fps = mov->fps;
                }
            } else {
                std::fprintf(stderr, "  [smpeg] decode failed, will skip frames\n");
            }
        }
        if (info) {
            m.w32(info + 0, 1);
            m.w32(info + 4, 2);
            m.w32(info + 8, 22050);
            m.w32(info + 12, 1);
            m.w32(info + 16, (uint32_t)w);
            m.w32(info + 20, (uint32_t)ht);
            m.w32(info + 24, 0);
            m.write(info + 28, &fps, 8);
        }
        std::fprintf(stderr, "  [smpeg] SMPEG_new OK '%s'%s\n",
                    file.c_str(), skip ? " [THEOC_SKIP_MOVIES]" : "");
        return h;
    };
    t["SMPEG_error"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t mpeg = arg(m, esp, 0);
        if (!mpeg || !smpeg_error_.empty()) {
            if (smpeg_error_.empty()) smpeg_error_ = "SMPEG null";
            uint32_t p = bump_alloc((uint32_t)smpeg_error_.size() + 1);
            if (p) m.write(p, smpeg_error_.c_str(),
                           (uint32_t)smpeg_error_.size() + 1);
            return p;
        }
        return 0;
    };
    t["SMPEG_status"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        if (!h) return (uint32_t)-1;
        auto* mov = mpeg_.get(h);
        if (!mov || !mov->playing) { movie_playing_ = false; return 0; }  // STOPPED
        if (mov->frame_i >= mov->frames.size()) {
            mov->playing = false;
            movie_playing_ = false;
            m.w32(h + 4, 0);
            return 0;
        }
        movie_playing_ = true;
        return 1;  // PLAYING
    };
    t["SMPEG_delete"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        if (h) mpeg_.erase(h);
        movie_playing_ = false;
        // Back to pixel-exact for the UI. This is the reliable end-of-movie hook:
        // every exit path reaches it, including a keypress skip (SMPEG_status goes
        // STOPPED, External_PlayAnim leaves its loop, and it deletes the stream).
        video_.set_crisp(true);
        if (key_mailbox_) { m.w32(key_mailbox_, 0); m.w32(key_mailbox_ + 4, 0); }
        return 0;
    };
    t["SMPEG_enablevideo"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["SMPEG_enableaudio"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["SMPEG_setvolume"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["SMPEG_setdisplay"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0), disp = arg(m, esp, 1);
        if (auto* mov = mpeg_.get(h)) mov->display = disp;
        return 0;
    };
    t["SMPEG_move"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        int x = (int)arg(m, esp, 1), y = (int)arg(m, esp, 2);
        if (auto* mov = mpeg_.get(h)) { mov->move_x = x; mov->move_y = y; }
        return 0;
    };
    t["SMPEG_startplayvideo"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        auto* mov = mpeg_.get(h);
        if (!mov || mov->frames.empty()) return 0;
        mov->playing = true;
        mov->frame_i = 0;
        mov->audio_pos = 0;
        if (mov->has_audio) ensure_audio();   // open the SDL device for cutscene sound
        // First frame shows immediately; subsequent frames wait on next_frame_at.
        mov->next_frame_at = std::chrono::steady_clock::now();
        m.w32(h + 4, 1);
        return 1;
    };
    t["SMPEG_playvideoframe"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        auto* mov = mpeg_.get(h);
        if (!mov || !mov->playing || mov->frame_i >= mov->frames.size())
            return 0;

        // Pace to movie fps. External_PlayAnim's loop is free-running
        // (playvideoframe + status only) — without a host delay cutscenes run
        // as fast as Unicorn can blit (~10×). Hold until next_frame_at, then
        // schedule the following frame.
        {
            double fps = mov->fps > 1.0 ? mov->fps : 15.0;
            using clock = std::chrono::steady_clock;
            auto frame_dt = std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(1.0 / fps));
            auto now = clock::now();
            if (mov->frame_i > 0 && now < mov->next_frame_at) {
                while (clock::now() < mov->next_frame_at) {
                    auto left = mov->next_frame_at - clock::now();
                    int ms = (int)std::chrono::duration_cast<
                        std::chrono::milliseconds>(left).count();
                    if (ms > 16) ms = 16;
                    if (ms < 1) ms = 1;
                    SDL_Delay((Uint32)ms);
                    video_.pump();  // keep window responsive during hold
                }
            }
            now = clock::now();
            if (mov->frame_i == 0)
                mov->next_frame_at = now + frame_dt;
            else {
                mov->next_frame_at += frame_dt;
                // If we fell more than 2 frames behind, resync (avoid spiral).
                if (now > mov->next_frame_at + 2 * frame_dt)
                    mov->next_frame_at = now + frame_dt;
            }
        }

        // Blit RGB565 frame into cDisplay::Address (or guest FB fallback).
        uint32_t dst = 0;
        uint16_t pitch = 0, dw = 0, dh = 0;
        if (mov->display) {
            dst = m.r32(mov->display + 0x00);
            m.read(mov->display + 0x04, &dw, 2);
            m.read(mov->display + 0x06, &dh, 2);
            m.read(mov->display + 0x08, &pitch, 2);
        }
        if (!dst) {
            dst = GUEST_FB_BASE;
            pitch = (uint16_t)(mov->width * 2);
            dw = (uint16_t)mov->width;
            dh = (uint16_t)mov->height;
        }
        const auto& fr = mov->frames[mov->frame_i++];

        // Aspect-fit into the destination instead of the old top-left 1:1 copy.
        //
        // The target is the VIDEO MODE, not cDisplay's W/H. The game constructs
        // cDisplay with the *movie's* dimensions (verified: 608x300 and 480x360)
        // while setting its pitch to the mode's (1280 = 640*2) — so dw/dh describe
        // the source, and the old `copy_w > dw` clamp could never fire. The frame
        // landed top-left and everything around it kept the previous screen's
        // pixels, which is the "all over the place" symptom: the two shipped
        // shapes (4:3 480x360, 2.03:1 608x300) each left a different stale margin.
        //
        // Only trust the mode when we are painting the presented framebuffer, whose
        // extent we know. For any other Address the surface size is unknown and
        // cDisplay's own W/H is the sole hint, so fitting to it degrades to the
        // previous 1:1 behaviour rather than risking a write past the end.
        int target_w, target_h;
        if (dst == GUEST_FB_BASE && video_.is_open()) {
            target_w = video_.width();
            target_h = video_.height();
            pitch = (uint16_t)(target_w * 2);  // authoritative stride for the FB
        } else {
            target_w = dw ? dw : mov->width;
            target_h = dh ? dh : mov->height;
            if (pitch < target_w * 2) pitch = (uint16_t)(target_w * 2);
        }

        if (const uint16_t* fitted = mov->fit_frame(fr, target_w, target_h)) {
            // Whole target every frame, bars included, so no stale pixels survive.
            for (int y = 0; y < target_h; ++y) {
                m.write(dst + (uint32_t)y * pitch,
                        fitted + (size_t)y * (size_t)target_w,
                        (uint32_t)target_w * 2);
            }
        } else {
            // Degenerate geometry — fall back to the raw top-left copy.
            int copy_w = std::min(mov->width, target_w);
            int copy_h = std::min(mov->height, target_h);
            for (int y = 0; y < copy_h; ++y) {
                m.write(dst + (uint32_t)y * pitch,
                        fr.data() + (size_t)y * (size_t)mov->width,
                        (uint32_t)copy_w * 2);
            }
        }
        // Feed this frame's audio into the host mixer. Video is paced to fps
        // (the hold loop above sleeps in real time), so pushing one frame's
        // worth of samples per frame tracks the SDL callback's 22050 Hz drain.
        // Keep ~2 frames of lead so the callback never underruns mid-cutscene.
        if (mov->has_audio && !mov->audio.empty()) {
            double fps = mov->fps > 1.0 ? mov->fps : 15.0;
            size_t chans = (size_t)mov->channels;              // 2 (stereo)
            size_t per_frame = (size_t)(mov->samplerate / fps + 0.5) * chans;
            size_t frames_shown = mov->frame_i;                // already advanced
            size_t target = (frames_shown + 2) * per_frame;    // 2-frame lead
            if (target > mov->audio.size()) target = mov->audio.size();
            if (target > mov->audio_pos) {
                size_t n = target - mov->audio_pos;
                audio_push(mov->audio.data() + mov->audio_pos, n * sizeof(int16_t));
                if (mov->audio_pos == 0)
                    std::fprintf(stderr, "  [audio] cutscene sound: %zu samp @ %d Hz\n",
                                mov->audio.size() / chans, mov->samplerate);
                mov->audio_pos = target;
            }
        }

        // Present so the user sees the cutscene (play loop may not call
        // SwapBuffers between frames).
        if (video_.is_open()) {
            // Video wants the fractional fit, not the integer one: the movie mode is
            // 640×480, where flooring 3.85x to 3.00x would throw away 39% of the
            // image area — and this is already-resampled video, so the pixel-exact
            // scale buys nothing. Restored to crisp in SMPEG_delete. No-op after
            // the first frame of a cutscene.
            video_.set_crisp(false);
            uint32_t nbytes = video_.fb_bytes();
            if (nbytes > GUEST_FB_SIZE) nbytes = GUEST_FB_SIZE;
            m.read(GUEST_FB_BASE, video_.fb(), nbytes);
            video_.present();
            present_seq_.fetch_add(1, std::memory_order_relaxed);
            start_watchdog(m); // cutscenes present here, not via SwapBuffers
            shot_tick();       // ...so frame capture has to be driven here too
            auto_keys_tick();  // may skip this cutscene
        }
        if (mov->frame_i >= mov->frames.size()) {
            mov->playing = false;
            m.w32(h + 4, 0);
        }
        return 1;
    };
    t["SMPEG_startplayaudio"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["SMPEG_playAudio"] = [](Machine&, uint32_t) -> uint32_t { return 0; };

    // ---- cDisplay (Philos SMPEG framebuffer descriptor) --------------------
    // Defined in smpeg-philos/MPEGextra.{h,cpp}; provided by libsmpeg on Linux.
    // libmvos External_PlayAnim constructs one for the current LFB and passes
    // it to SMPEG_setdisplay. Layout (g++ 2.95, no vtable):
    //   +0x00 Address (void*)  +0x04 Width (u16)  +0x06 Height (u16)
    //   +0x08 Pitch (u16)      +0x0c RedMask (u32) +0x10 GreenMask
    //   +0x14 BlueMask         +0x18 BitsPerPixel (u8)
    // Without this, a zeroed object → pitch 0 → MemBlock.Alloc(0) Fatal, and
    // Start's recovery re-opens subsystems in a loop (looks like a freeze).
    t["__8cDisplayPvUsUsUsUcUiUlUl"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t self  = arg(m, esp, 0);
        uint32_t addr  = arg(m, esp, 1);
        uint16_t w     = (uint16_t)arg(m, esp, 2);
        uint16_t h     = (uint16_t)arg(m, esp, 3);
        uint16_t pitch = (uint16_t)arg(m, esp, 4);
        uint8_t  bpp   = (uint8_t)arg(m, esp, 5);
        uint32_t red   = arg(m, esp, 6);
        uint32_t green = arg(m, esp, 7);
        uint32_t blue  = arg(m, esp, 8);
        if (self) {
            m.w32(self + 0x00, addr);
            m.write(self + 0x04, &w, 2);
            m.write(self + 0x06, &h, 2);
            m.write(self + 0x08, &pitch, 2);
            m.w32(self + 0x0c, red);
            m.w32(self + 0x10, green);
            m.w32(self + 0x14, blue);
            m.write(self + 0x18, &bpp, 1);
        }
        static int n;
        if (n++ < 6)
            std::fprintf(stderr, "  [HLE] cDisplay @%#x fb=%#x %ux%u pitch=%u bpp=%u\n",
                        self, addr, w, h, pitch, bpp);
        return self;  // g++ 2.95 ctor returns this
    };

    // ---- libdl: synthetic plugins (no real X11 .so load) --------------------
    t["dlopen"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 0));
        // Accept any libmvos_*_x.so / glide name.
        if (path.find("libmvos_") == std::string::npos && path.find("vvc") == std::string::npos) {
            last_dlerror_ = "synthetic dlopen: unknown " + path;
            static int n;
            if (n++ < 8) std::fprintf(stderr, "  [dlopen] reject '%s'\n", path.c_str());
            return 0;
        }
        uint32_t h = next_dl_handle_++;
        dl_handles_[h] = path;
        last_dlerror_.clear();
        std::fprintf(stderr, "  [dlopen] '%s' -> handle %#x (synthetic)\n", path.c_str(), h);
        return h;
    };
    t["dlclose"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        dl_handles_.erase(arg(m, esp, 0));
        return 0;
    };
    t["dlerror"] = [this](Machine& m, uint32_t) -> uint32_t {
        if (last_dlerror_.empty()) return 0;
        // Return a stable guest string.
        uint32_t p = bump_alloc((uint32_t)last_dlerror_.size() + 1);
        if (p) m.write(p, last_dlerror_.c_str(), (uint32_t)last_dlerror_.size() + 1);
        return p;
    };
    t["dlsym"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        std::string name = m.cstr(arg(m, esp, 1));
        if (!dl_handles_.count(h) && h != 0) {
            last_dlerror_ = "dlsym: bad handle";
            return 0;
        }
        // Map export name → plugin trap slot.
        for (uint32_t i = 0; i < plugin_exports_.size(); ++i) {
            if (plugin_exports_[i] == name)
                return plugin_trap_base_ + i;
        }
        last_dlerror_ = "dlsym: " + name;
        static int n;
        if (n++ < 12)
            std::fprintf(stderr, "  [dlsym] miss '%s'\n", name.c_str());
        return 0;
    };

    // ---- FILE* stdio --------------------------------------------------------
    t["fopen"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 0));
        std::string mode = m.cstr(arg(m, esp, 1));
        if (path.rfind("/dev/", 0) == 0) {
            // Stub device: opaque FILE*, all I/O no-ops / zeros.
            uint32_t gp = bump_alloc(128);
            if (gp) { std::vector<uint8_t> z(128, 0); m.write(gp, z.data(), 128); }
            files_[gp] = HostFile{nullptr, -1, true, false, false};
            return gp;
        }
        std::string host = resolve_path(path);
        // Ensure parent dirs for writes (save/saveN.tsg, etc.).
        bool writing = mode.find('w') != std::string::npos ||
                       mode.find('a') != std::string::npos ||
                       mode.find('+') != std::string::npos;
        if (writing) {
            auto slash = host.find_last_of('/');
            if (slash != std::string::npos && slash > 0) {
                std::string dir = host.substr(0, slash), cur;
                for (size_t i = 0; i < dir.size(); ++i) {
                    cur.push_back(dir[i]);
                    if (dir[i] == '/' && cur.size() > 1)
                        theoc_mkdir(cur.c_str());
                }
                if (!dir.empty() && dir.back() != '/')
                    theoc_mkdir(dir.c_str());
            }
        }
        // Force binary mode. The guest is a Linux binary, so its mode strings
        // never carry 'b' — on Windows that would mean text translation on the
        // saves, the .pck archives and the cutscenes. On POSIX 'b' is ignored,
        // so this is unconditional rather than another #ifdef.
        if (mode.find('b') == std::string::npos) mode.push_back('b');
        FILE* fp = std::fopen(host.c_str(), mode.c_str());
        if (!fp) {
            // Fallback: try path as-is relative to cwd.
            fp = std::fopen(path.c_str(), mode.c_str());
        }
        if (!fp) {
            set_errno(m, ENOENT);
            static int nmiss;
            if (nmiss++ < 20)
                std::fprintf(stderr, "  [fopen] miss '%s' (host '%s')\n",
                             path.c_str(), host.c_str());
            return 0;
        }
        uint32_t gp = bump_alloc(128);
        if (gp) { std::vector<uint8_t> z(128, 0); m.write(gp, z.data(), 128); }
        files_[gp] = HostFile{fp, -1, false, false, false, host,
                              mode.find('w') != std::string::npos ||
                              mode.find('a') != std::string::npos ||
                              mode.find('+') != std::string::npos};
        return gp;
    };

    t["fclose"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        auto it = files_.find(gp);
        if (it == files_.end()) return (uint32_t)-1;
        std::string path = it->second.host_path;
        bool wrote = it->second.wrote;
        if (it->second.fp) std::fclose(it->second.fp);
        files_.erase(it);
        // Normalise the save the moment it is complete and closed — see
        // collapse_save_file. Done here rather than at write time because the
        // fix needs the whole file, and here it is on disk and consistent.
        if (wrote && path.size() > 4 &&
            path.compare(path.size() - 4, 4, ".tsg") == 0)
            collapse_save_file(path);
        return 0;
    };

    t["fread"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t ptr = arg(m, esp, 0), size = arg(m, esp, 1),
                 nmemb = arg(m, esp, 2), gp = arg(m, esp, 3);
        auto it = files_.find(gp);
        if (it == files_.end()) return 0;
        if (it->second.stub) {
            // Zero-fill for device stubs.
            uint32_t n = size * nmemb;
            if (n) { std::vector<uint8_t> z(n, 0); m.write(ptr, z.data(), n); }
            return nmemb;
        }
        if (!it->second.fp) return 0;
        uint32_t n = size * nmemb;
        std::vector<uint8_t> buf(n);
        size_t got = std::fread(buf.data(), size, nmemb, it->second.fp);
        if (got) m.write(ptr, buf.data(), (uint32_t)(got * size));
        if (std::feof(it->second.fp)) it->second.eof = true;
        return (uint32_t)got;
    };

    t["fwrite"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t ptr = arg(m, esp, 0), size = arg(m, esp, 1),
                 nmemb = arg(m, esp, 2), gp = arg(m, esp, 3);
        auto it = files_.find(gp);
        if (it == files_.end()) return 0;
        if (it->second.stub || !it->second.fp) return nmemb;  // pretend OK
        uint32_t n = size * nmemb;
        std::vector<uint8_t> buf(n);
        if (n) m.read(ptr, buf.data(), n);
        return (uint32_t)std::fwrite(buf.data(), size, nmemb, it->second.fp);
    };

    t["fseek"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        int32_t off = (int32_t)arg(m, esp, 1);
        int whence = (int)arg(m, esp, 2);
        auto it = files_.find(gp);
        if (it == files_.end() || it->second.stub || !it->second.fp) return 0;
        return (uint32_t)std::fseek(it->second.fp, off, whence);
    };

    t["ftell"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        auto it = files_.find(gp);
        if (it == files_.end() || it->second.stub || !it->second.fp) return 0;
        long p = std::ftell(it->second.fp);
        return p < 0 ? (uint32_t)-1 : (uint32_t)p;
    };

    t["feof"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        auto it = files_.find(gp);
        if (it == files_.end()) return 1;
        if (it->second.stub) return it->second.eof ? 1 : 0;
        if (!it->second.fp) return 1;
        return std::feof(it->second.fp) ? 1 : 0;
    };

    t["fflush"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        if (!gp) { std::fflush(nullptr); return 0; }
        auto it = files_.find(gp);
        if (it == files_.end() || it->second.stub || !it->second.fp) return 0;
        return (uint32_t)std::fflush(it->second.fp);
    };

    t["_IO_getc"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        auto it = files_.find(gp);
        if (it == files_.end() || it->second.stub || !it->second.fp) return (uint32_t)EOF;
        int c = std::fgetc(it->second.fp);
        return (uint32_t)c;
    };

    // ---- POSIX open/read/write/close ----------------------------------------
    t["open"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 0));
        int flags = (int)arg(m, esp, 1);
        if (path.rfind("/dev/", 0) == 0) {
            int gfd = next_fd_++;
            bool is_dsp = (path.find("dsp") != std::string::npos ||
                           path.find("audio") != std::string::npos);
            if (is_dsp) ensure_audio();
            fds_[gfd] = HostFile{nullptr, -1, true, is_dsp, false};
            if (is_dsp)
                std::fprintf(stderr, "  [audio] open '%s' -> guest fd %d\n", path.c_str(), gfd);
            return (uint32_t)gfd;
        }
        std::string host = resolve_path(path);
        int hflags = O_RDONLY;
        if (flags & 1) hflags = O_WRONLY;       // O_WRONLY=1 on Linux i386
        if (flags & 2) hflags = O_RDWR;         // O_RDWR=2
        // O_CREAT etc. ignored for bring-up unless needed.
        hflags |= O_BINARY;   // no-op on POSIX; prevents CRLF mangling on Windows
        int hfd = ::open(host.c_str(), hflags);
        if (hfd < 0) hfd = ::open(path.c_str(), hflags);
        if (hfd < 0) { set_errno(m, to_linux_errno(errno)); return (uint32_t)-1; }
        int gfd = next_fd_++;
        fds_[gfd] = HostFile{nullptr, hfd, false, false, false};
        return (uint32_t)gfd;
    };

    t["close"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int gfd = (int)arg(m, esp, 0);
        auto it = fds_.find(gfd);
        if (it == fds_.end()) return (uint32_t)-1;
        if (it->second.host_fd >= 0) {
            if (it->second.sock) THEOC_CLOSESOCKET(it->second.host_fd);
            else                 ::close(it->second.host_fd);
        }
        fds_.erase(it);
        return 0;
    };

    // read() is not only for files: cIPCO_TCPIP::Read/Write poll the *socket*
    // through plain read/write, not recv/send, so these two traps are on the
    // hot path of every netgame frame and must translate errno exactly like the
    // socket traps do. libmvos switches on errno 4..22 and maps anything outside
    // that window to its generic "unknown error" state (5); a non-blocking read
    // with no data pending is the common case, so leaking BSD EAGAIN=35 instead
    // of Linux 11 turned "no data yet" into a hard error on every poll.
    t["read"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int gfd = (int)arg(m, esp, 0);
        uint32_t buf = arg(m, esp, 1), n = arg(m, esp, 2);
        auto it = fds_.find(gfd);
        if (it == fds_.end()) { set_errno(m, 9 /*EBADF*/); return (uint32_t)-1; }
        if (it->second.stub) {
            if (n) { std::vector<uint8_t> z(n, 0); m.write(buf, z.data(), n); }
            return n;                             // pretend device has data
        }
        if (it->second.host_fd < 0) { set_errno(m, 9 /*EBADF*/); return (uint32_t)-1; }
        std::vector<uint8_t> b(n);
        // A socket cannot be ::read on Windows (separate handle namespace), and
        // its error does not land in errno either — dispatch on the kind.
        ssize_t got;
        if (it->second.sock) {
            got = ::recv(it->second.host_fd, THEOC_SOCK_CAST(b.data()), n, 0);
            if (got < 0) { set_errno(m, last_socket_errno()); return (uint32_t)-1; }
        } else {
            got = ::read(it->second.host_fd, b.data(), n);
            if (got < 0) { set_errno(m, to_linux_errno(errno)); return (uint32_t)-1; }
        }
        if (got) m.write(buf, b.data(), (uint32_t)got);
        return (uint32_t)got;
    };

    t["write"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int gfd = (int)arg(m, esp, 0);
        uint32_t buf = arg(m, esp, 1), n = arg(m, esp, 2);
        if (gfd == 1 || gfd == 2) {
            std::vector<uint8_t> b(n);
            if (n) m.read(buf, b.data(), n);
            std::fwrite(b.data(), 1, n, gfd == 2 ? stderr : stdout);
            return n;
        }
        auto it = fds_.find(gfd);
        if (it == fds_.end()) { set_errno(m, 9 /*EBADF*/); return (uint32_t)-1; }
        if (it->second.audio) {
            std::vector<uint8_t> b(n);
            if (n) m.read(buf, b.data(), n);
            audio_push(b.data(), n);
            static int nlog;
            if (nlog++ < 8)
                std::fprintf(stderr, "  [audio] write %u bytes to dsp (q≈%zu samp)\n",
                            n, audio_q_.size());
            return n;
        }
        if (it->second.stub) return n;
        if (it->second.host_fd < 0) { set_errno(m, 9 /*EBADF*/); return (uint32_t)-1; }
        std::vector<uint8_t> b(n);
        if (n) m.read(buf, b.data(), n);
        ssize_t w;
        if (it->second.sock) {
            w = ::send(it->second.host_fd, THEOC_SOCK_CAST(b.data()), n, 0);
            if (w < 0) { set_errno(m, last_socket_errno()); return (uint32_t)-1; }
        } else {
            w = ::write(it->second.host_fd, b.data(), n);
            if (w < 0) { set_errno(m, to_linux_errno(errno)); return (uint32_t)-1; }
        }
        return (uint32_t)w;
    };

    t["remove"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 0));
        std::string host = resolve_path(path);
        if (::unlink(host.c_str()) == 0) return 0;
        if (::unlink(path.c_str()) == 0) return 0;
        set_errno(m, to_linux_errno(errno));
        return (uint32_t)-1;
    };

    // Minimal stat for existence checks. __xstat(ver, path, statbuf).
    // __xstat(ver, path, buf) — glibc's stat() wrapper.
    //
    // THE BUFFER SIZE IS LOAD-BEARING. Linux/i386 `struct stat` (_STAT_VER_LINUX)
    // is exactly **88 bytes**, and callers put it on the stack. This used to write
    // 96 zeroed bytes with a guessed layout, so it ran 8 bytes past the end of the
    // caller's local and zeroed the saved EBP and return address sitting there.
    // The victim was cDirent::cDirent (mvos+0x4c030), which calls __xstat twice:
    // it completed normally and then `ret`-ed to 0 with EBP popped as 0 — a fault
    // at eip=0 with no frame pointer, several frames away from the actual damage.
    // Only the netgame map dialog constructs a cDirent, which is why nothing else
    // ever tripped it.
    //
    // Layout (i386, glibc 2.x, 64-bit dev/rdev, 32-bit ino/off):
    //   +0x00 dev(8)  +0x08 pad(2/2)  +0x0c ino(4)   +0x10 mode(4)  +0x14 nlink(4)
    //   +0x18 uid(4)  +0x1c gid(4)    +0x20 rdev(8)  +0x28 pad(2/2) +0x2c size(4)
    //   +0x30 blksize(4)  +0x34 blocks(4)
    //   +0x38 atim(8) +0x40 mtim(8)   +0x48 ctim(8)  +0x50 unused(8)   = 88
    t["__xstat"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 1));
        uint32_t sbuf = arg(m, esp, 2);
        constexpr uint32_t kLinuxStatSize = 88;

        struct stat st{};
        bool ok;
        if (path.rfind("/dev/", 0) == 0) {
            std::memset(&st, 0, sizeof st);
            st.st_mode = S_IFCHR | 0666;   // faked char device
            ok = true;
        } else {
            std::string host = resolve_path(path);
            ok = (::stat(host.c_str(), &st) == 0) || (::stat(path.c_str(), &st) == 0);
        }
        if (!ok) { set_errno(m, 2 /*ENOENT*/); return (uint32_t)-1; }
        if (!sbuf) return 0;

        uint8_t z[kLinuxStatSize];
        std::memset(z, 0, sizeof z);
        auto put32 = [&](uint32_t off, uint32_t v) { std::memcpy(z + off, &v, 4); };
        auto put64 = [&](uint32_t off, uint64_t v) { std::memcpy(z + off, &v, 8); };
        put64(0x00, (uint64_t)st.st_dev);
        put32(0x0c, (uint32_t)st.st_ino);
        // S_IFMT bits agree between Linux and BSD, so the mode passes through --
        // and passing the REAL mode matters: the old code hardcoded S_IFREG, which
        // would have reported every directory as a regular file.
        put32(0x10, (uint32_t)st.st_mode);
        put32(0x14, (uint32_t)st.st_nlink);
        put32(0x18, (uint32_t)st.st_uid);
        put32(0x1c, (uint32_t)st.st_gid);
        put64(0x20, (uint64_t)st.st_rdev);
        put32(0x2c, (uint32_t)st.st_size);
#if defined(_WIN32)
        // mingw's struct stat has neither field. The Linux/i386 layout the guest
        // expects still has the slots, so fill them consistently with st_size
        // rather than leaving zeros: a 0 block size has tripped buffered-IO code
        // before, and nothing here needs them to be the truth.
        put32(0x30, 512u);
        put32(0x34, (uint32_t)((st.st_size + 511) / 512));
#else
        put32(0x30, (uint32_t)st.st_blksize);
        put32(0x34, (uint32_t)st.st_blocks);
#endif
        put32(0x38, (uint32_t)st.st_atime);
        put32(0x40, (uint32_t)st.st_mtime);
        put32(0x48, (uint32_t)st.st_ctime);
        m.write(sbuf, z, kLinuxStatSize);
        return 0;
    };

    // Harmless startup / teardown / pthread stubs (pthread_create is above —
    // soft-thread for the sound mixer).
    for (const char* nm : {"__libc_init_first", "pthread_key_delete",
                           "pthread_key_create", "pthread_getspecific",
                           "pthread_setspecific", "pthread_once",
                           "pthread_mutex_lock", "pthread_mutex_unlock",
                           "pthread_mutex_trylock", "pthread_cancel",
                           "monstartup", "_mcleanup", "atexit",
                           "__deregister_frame_info", "__register_frame_info",
                           "sigemptyset", "sigaddset", "signal",
                           "perror"})
        t[nm] = [](Machine&, uint32_t) -> uint32_t { return 0; };
}

// The present-to-present cap, in ms (THEOC_FRAME_MS; 0 = uncapped, the default
// since 2026-08-03). Read once. Also reported on the [health] line, because
// growth rates are only comparable between runs at the same cap.
//
// This used to default to 83 (the province limiter's own period) and applied to
// *every* screen. It was compensation for our own defect: the usleep handler
// truncated the guest's frame-limiter sleep at the first due tick, so
// cProvince::Do returned early and the sim ran ~2.5x fast, and a global present
// clamp was what held it back. The usleep handler now honours the full
// requested duration (see t["usleep"]), so the guest limits itself:
//   - province self-limits to its designed 12fps via its own Sleep(0x14585-el);
//   - the realm screen, which has nothing frame-tied in its loop at all
//     (SimulationUpdate self-clocks, UpdateProvincePaletteEffects is pure
//     wall-clock, and the step-9 call is an idempotent CD-state setter), is no
//     longer held to 12fps for a frame-tiedness it does not have.
// Set THEOC_FRAME_MS=83 to restore the old global clamp for A/B.
//
// The default is now **16ms (~60fps) as a ceiling**, not a pacing clamp. The
// first uncapped run showed why: RealmGameLoop calls usleep *zero* times — it
// has no frame limiter of its own at all — so with nothing capping it, realm
// free-ran to ~100fps at 0.01-0.04M guest blocks/frame. Correct, but it is
// burning host CPU to present frames nothing asked for, and it would only climb
// on faster hardware. 16ms never fires in province (which self-limits to 83ms
// through its own code), so it caps the unlimited screen without touching the
// limited one. 0 = genuinely uncapped.
int TrapLayer::frame_cap_ms() {
    static const int ms = []{
        const char* e = std::getenv("THEOC_FRAME_MS");
        return e ? std::atoi(e) : 16;
    }();
    return ms;
}

// ---- long-session harness (THEOC_LONGRUN) ----------------------------------
//
// The 20-cycle soak covers one scripted path in ~9 minutes. A multi-hour human
// session is a different test: nothing drives it, the interesting failures are
// slow (drift, leak, a stall hours in), and the only artefact afterwards is the
// log. That imposes two requirements the existing instruments do not meet.
//
// 1. The log must stay small. Anything repeatable has to be rate-limited, or a
//    single stuck condition writes gigabytes overnight and buries the cause.
//    rl_allow() keeps a burst then throttles, and counts what it dropped — a
//    silenced spammer stays visible as a number rather than vanishing.
// 2. A fault hours in must be diagnosable from the log alone, which means
//    periodic state, not just events. [health] is that: one dense line whose
//    deltas answer "was it already going wrong an hour ago?".
//
// On rates: the engine is frame-tied, so wall-clock rates are only comparable
// between runs at the same frame cap. A session at THEOC_FRAME_MS=50 (20fps)
// steps the simulation ~1.67x faster than the 83ms default, and therefore
// allocates ~1.67x as much per hour while being no less correct. [health]
// prints growth per 1000 frames alongside per hour, and states the cap, so runs
// at different speeds can be compared directly.
void TrapLayer::enable_longrun() {
    longrun_ = true;
    if (const char* v = std::getenv("THEOC_LONGRUN")) {
        int n = std::atoi(v);
        if (n > 0) longrun_every_ = std::chrono::seconds(n);
    }
    longrun_t0_ = longrun_last_ = std::chrono::steady_clock::now();
    longrun_live_base_ = longrun_live_start_ = heap_live_;
    longrun_heap_start_ = heap_next_;
    std::fprintf(stderr,
                 "  [longrun] session harness armed — [health] every %llds, "
                 "logs rate-limited\n", (long long)longrun_every_.count());
}

bool TrapLayer::rl_allow(const char* key, uint32_t burst,
                         std::chrono::seconds interval) {
    if (!longrun_) return true;                 // untouched outside long runs
    RateLimit& r = rl_[key];
    ++r.seen;
    auto now = std::chrono::steady_clock::now();
    if (r.emitted < burst) { ++r.emitted; r.last = now; return true; }
    if (now - r.last >= interval) {
        r.last = now;
        ++r.emitted;
        return true;
    }
    ++r.suppressed;
    ++rl_suppressed_total_;
    return false;
}

void TrapLayer::longrun_tick(Machine& m) {
    if (!longrun_) return;
    if (!longrun_blocks_init_) {
        longrun_blocks_init_ = true;
        longrun_blocks_base_ = m.exec_blocks();
    }
    ++longrun_frames_;
    ++longrun_frames_total_;
    auto now = std::chrono::steady_clock::now();
    // A marker forces the sample out early. That is the point of it: the
    // interval boundary then lands on the event the operator just marked, so
    // the samples either side of it each cover one activity instead of
    // straddling the transition. A trial's first interval after a marker is
    // short — read its per-1k-frames figure, not its per-hour one.
    if (!mark_pending_ && now - longrun_last_ < longrun_every_) return;

    double secs = std::chrono::duration<double>(now - longrun_last_).count();
    double uptime = std::chrono::duration<double>(now - longrun_t0_).count();

    size_t rss = 0;
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t cnt = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &cnt) == KERN_SUCCESS)
        rss = info.resident_size;
#endif
    if (!longrun_rss_base_) longrun_rss_base_ = rss;
    uint32_t esp = 0;
    if (machine_) { try { esp = machine_->reg(UC_X86_REG_ESP); } catch (...) {} }

    double frontier_mb = (heap_next_ - guestmap::HEAP_BASE) / 1048576.0;
    // Signed: the live set *falls* when a scenario is torn down or a save
    // reloaded, and that drop is a real, informative event. The frontier can
    // only ever rise, which is the other half of why it made a bad leak signal.
    double grown_mb    = ((double)heap_live_ - (double)longrun_live_base_) / 1048576.0;
    double per_hour    = secs > 0 ? grown_mb * 3600.0 / secs : 0.0;
    double per_kframe  = longrun_frames_ ? grown_mb * 1000.0 / longrun_frames_ : 0.0;
    double fps         = secs > 0 ? longrun_frames_ / secs : 0.0;
    // Guest work, so a slow interval can be classified rather than guessed at.
    // Reads 0 if the block counter was never armed (see main.cpp).
    uint64_t blocks_now = m.exec_blocks();
    uint64_t blocks     = blocks_now - longrun_blocks_base_;
    double blk_per_s    = secs > 0 ? blocks / secs : 0.0;
    double blk_per_f    = longrun_frames_ ? (double)blocks / longrun_frames_ : 0.0;
    // Interval rates are wild during a scenario load (tens of MB in one
    // interval extrapolates to thousands of MB/h). The since-start figures are
    // the ones to read for a slow leak; the interval ones catch a sudden onset.
    double total_mb    = ((double)heap_live_ - (double)longrun_live_start_) / 1048576.0;
    double avg_per_hour = uptime > 0 ? total_mb * 3600.0 / uptime : 0.0;
    // Headroom is a frontier question — it is the bump frontier that runs into
    // the end of the arena — so report it from the frontier, not from live.
    double front_grown  = (double)(heap_next_ - longrun_heap_start_) / 1048576.0;
    double front_per_h  = uptime > 0 ? front_grown * 3600.0 / uptime : 0.0;
    // Both since-start figures above are dominated by the one-time ~27 MB
    // scenario load, and on a ten-minute trial that never washes out: the first
    // samples of trial 1 (2026-08-01) reported "+6498 MB/h -> 0.0 h headroom"
    // while the live set was in fact perfectly flat. An alarming number that is
    // pure warm-up is worse than no number, so headroom is withheld until there
    // is enough uptime for the load to stop dominating.
    const double kSettled = 1800.0;    // 0.5 h, the figure diagnostics.md gives
    bool settled        = uptime >= kSettled;
    double headroom_h   = (settled && front_per_h > 0.01)
                        ? (guestmap::HEAP_SIZE / 1048576.0 - frontier_mb) / front_per_h
                        : 0.0;

    // audio_underrun_ counts interleaved stereo *samples* and is reset by the
    // THEOC_FPS line, so we cannot reset it too without the two instruments
    // stealing from each other. Track our own baseline and report the delta as
    // frames, which is what the number means.
    uint64_t underrun_raw; size_t qdepth;
    { std::lock_guard<std::mutex> lock(audio_mu_);
      underrun_raw = audio_underrun_; qdepth = audio_q_.size(); }
    uint64_t under_frames = (underrun_raw > longrun_underrun_base_
                             ? underrun_raw - longrun_underrun_base_ : 0) / 2;
    longrun_underrun_base_ = underrun_raw;

    // Wall-clock stamp so an out-of-band note ("reloaded a save at 21:44") can
    // be lined up against the samples. Uptime alone cannot do that.
    char clock[16] = "--:--:--";
    { std::time_t t = std::time(nullptr); std::tm tmv{};
#if defined(_WIN32)
      // Note the reversed argument order vs localtime_r, and 0 means success.
      if (::localtime_s(&tmv, &t) == 0)
#else
      if (localtime_r(&t, &tmv))
#endif
          std::strftime(clock, sizeof clock, "%H:%M:%S", &tmv); }

    char headroom[32];
    if (settled && headroom_h > 0.0)
        std::snprintf(headroom, sizeof headroom, "%.1f h headroom", headroom_h);
    else
        std::snprintf(headroom, sizeof headroom, "headroom n/a (warm-up)");

    // The marker goes out first and blank-line-separated, so a trial boundary is
    // findable by eye in a two-hour log, and the [health] sample it forced sits
    // directly under it.
    if (mark_pending_) {
        mark_pending_ = false;
        std::fprintf(stderr, "\n[mark] #%u %s | up %.2fh | frames %llu\n",
                     mark_seq_, clock, uptime / 3600.0,
                     (unsigned long long)longrun_frames_total_);
    }

    std::fprintf(stderr,
        "[health] %s | up %.2fh | %.1f fps (cap %dms) | frames %llu | "
        "%.2fM blk/s (%.3fM/frame)\n"
        "         heap live %.2f MB | grew %+.3f MB total (avg %+.3f MB/h) | "
        "interval %+.3f MB/h, %+.4f MB/1k frames\n"
        "         frontier %.2f MB of %.0f MB arena (+%.3f MB/h -> %s) | "
        "rss %.1f MB (%+.1f since start)\n"
        "         esp %#x | stubs %u B | fds %zu | audio q=%.2fs "
        "underrun-frames %llu | logs suppressed %llu\n",
        clock, uptime / 3600.0, fps, frame_cap_ms(),
        (unsigned long long)longrun_frames_total_,
        blk_per_s / 1e6, blk_per_f / 1e6,
        heap_live_ / 1048576.0, total_mb, avg_per_hour, per_hour, per_kframe,
        frontier_mb, guestmap::HEAP_SIZE / 1048576.0, front_per_h, headroom,
        // Signed subtraction, in double. As size_t this underflowed to
        // +17592186044408.9 the moment RSS dipped below its first sample, which
        // is exactly what a ten-minute trial does after the load settles.
        rss / 1048576.0, ((double)rss - (double)longrun_rss_base_) / 1048576.0,
        esp, stub_next_ ? stub_next_ - guestmap::STUB_CODE : 0u, fds_.size(),
        qdepth / 44100.0, (unsigned long long)under_frames,
        (unsigned long long)rl_suppressed_total_);
    std::fflush(stderr);

    longrun_last_ = now;
    longrun_frames_ = 0;
    longrun_live_base_ = heap_live_;
    longrun_blocks_base_ = blocks_now;
}

void TrapLayer::fps_tick(Machine& m) {
    if (!fps_init_) {
        fps_init_ = true;
        fps_on_ = std::getenv("THEOC_FPS") != nullptr;
        fps_last_ = std::chrono::steady_clock::now();
        fps_blocks_base_ = m.exec_blocks();
    }
    longrun_tick(m);
    if (!fps_on_) return;
    ++fps_frames_;
    auto now = std::chrono::steady_clock::now();
    auto dt = now - fps_last_;
    if (dt < std::chrono::seconds(1)) return;

    double secs = std::chrono::duration<double>(dt).count();
    uint64_t blocks = m.exec_blocks() - fps_blocks_base_;
    double fps = fps_frames_ / secs;
    double bps = blocks / secs;                       // guest blocks / sec
    double bpf = fps_frames_ ? (double)blocks / fps_frames_ : 0;
    uint64_t underrun; size_t qdepth;
    { std::lock_guard<std::mutex> lock(audio_mu_);
      underrun = audio_underrun_; audio_underrun_ = 0; qdepth = audio_q_.size(); }
    std::fprintf(stderr,
        "[fps] %.1f fps | guest %.1fM blk/s (%.2fM/frame) | "
        "heartbeat %.0f/s mixer %.0f/s\n"
        "      sleep %.0fms/s in %d usleep | gettimeofday %d/s | select %d/s | "
        "audio q=%.2fs underrun=%llu/s | heap %.1fMB live (+%.2fMB/s frontier)\n",
        fps, bps / 1e6, bpf / 1e6,
        fps_timer_fires_ / secs, fps_sound_fires_ / secs,
        (fps_usleep_us_ / 1000.0) / secs, fps_usleep_calls_,
        (int)(fps_gettime_calls_ / secs), (int)(fps_select_calls_ / secs),
        qdepth / 44100.0, (unsigned long long)(underrun / 2 / (uint64_t)(secs < 1 ? 1 : secs)),
        heap_live_ / 1048576.0,
        ((heap_next_ - fps_heap_base_) / 1048576.0) / secs);

    fps_last_ = now;
    fps_frames_ = 0;
    fps_blocks_base_ = m.exec_blocks();
    fps_heap_base_ = heap_next_;
    fps_timer_fires_ = 0;
    fps_sound_fires_ = 0;
    fps_usleep_us_ = 0;
    fps_usleep_calls_ = 0;
    fps_gettime_calls_ = 0;
    fps_select_calls_ = 0;
}

// Collapse the tick schedule up to now so a slow frame doesn't storm. Returns
// the number of intervals skipped, or -1 if the timer disarmed itself.
int TrapLayer::advance_timer_schedule() {
    if (timer_interval_.count() <= 0) {
        timer_armed_ = false;
        return -1;
    }
    auto now = std::chrono::steady_clock::now();
    int skipped = 0;
    while (timer_next_ <= now && skipped < 16) {
        timer_next_ += timer_interval_;
        skipped++;
    }
    if (timer_next_ < now)
        timer_next_ = now + timer_interval_;
    return skipped;
}

// The stock handler is _TimerFunction (mvos+0x922e0), which takes an int and
// never reads it — verified in the disassembly: it loads TimerSystem and tail-
// calls cTimerSystem_Linux::Proc without touching [ebp+8]. That is what makes
// the re-entrant frame in redirect_timer_reentrant safe, so anything else must
// take the old truncating path.
bool TrapLayer::timer_handler_ignores_signo() const {
    if (!mvos_base_) return false;
    return sigalrm_handler_ == 0 || sigalrm_handler_ == mvos_base_ + 0x922e0;
}

// Splice the tick so that when it returns, control lands back in the *usleep
// trap* rather than in usleep's caller — the host keeps the unslept remainder
// and finishes the sleep on re-entry. This is what lets a long guest sleep
// behave like a real one: full duration, with ticks delivered during it.
//
// Frame layout, and why signo aliases the return address:
//
//   esp   → [ret_original][orig_arg]      the trap's own frame, left intact
//   esp-4 → [usleep_trap]                 _TimerFunction returns through this
//
// _TimerFunction runs with ESP = esp-4, so it reads signo at [esp] — the same
// dword the re-entered trap will read as its return address. A `ret` pops 4 and
// a cdecl arg sits at +4, so those two slots are necessarily the same one and
// cannot be separated without a trampoline. Harmless here only because the
// stock handler ignores signo; the caller checks that first.
//
// After its `ret`: EIP = usleep_trap, ESP = esp — exactly the frame the trap
// was entered with, so the eventual normal return pops ret_original as usual.
bool TrapLayer::redirect_timer_reentrant(Machine& m, uint32_t esp, uint32_t remaining) {
    uint32_t usleep_trap = trap_addr("usleep");
    if (!usleep_trap) return false;
    if (advance_timer_schedule() < 0) return false;

    uint32_t fn = sigalrm_handler_;
    if (!fn) fn = mvos_base_ + 0x922e0;  // _TimerFunction__Fi
    sleep_remaining_us_ = remaining;
    sleep_resume_ret_   = m.r32(esp);
    sleep_resuming_     = true;
    uint32_t sp = esp - 4;
    m.w32(sp, usleep_trap);
    m.redirect_guest(fn, sp);
    fps_timer_fires_++;
    return true;
}

// libmvos file offset of cGD_LFB16::Refresh(const cRectangle&). Confirmed as
// vtable slot +0x14 of __vt_9cGD_LFB16 (0xa2820) with relocations applied —
// `tools/elfq.py mvos vtable 0xa2820`, since libmvos keeps its vtable relocs in
// .rel.rodata and the raw words read as zeros (re-methodology §10).
static constexpr uint32_t OFF_GD_LFB16_Refresh = 0x6bae0;

// THEOC_PROVINCE_MS — retune the province frame limiter.
//
// cProvince_Do builds its target period with
//     Set__8cDayTimell(&target, 0, 0x14585)      // 83,333 µs == 12fps
// and then sleeps the remainder of it. The literal is one `push imm32`:
//
//   0x81da529:  68 85 45 01 00   push 0x14585    <- the 4 bytes we rewrite
//   0x81da52e:  31 db            xor  ebx,ebx
//   0x81da530:  6a 00            push 0          ; high dword of the long long
//   0x81da532:  56               push esi        ; &target
//   0x81da533:  e8 2c 56 e7 ff   call Set__8cDayTimell@plt
//
// `68 85 45 01 00` occurs exactly **once** in the whole 24 MB image, so the site
// is unambiguous — but we verify the operand before writing anyway, because a
// silent mismatch here would retune something else entirely.
//
// This is a *game speed* control, not a smoothness control: province steps its
// simulation once per frame, with no wall-clock input anywhere in cMan::Do, so
// halving the period doubles the frame rate **and** doubles the game speed. That
// coupling is the engine's, not ours (frame-timing.md, "Why province stays at
// 12fps"). Unset leaves the shipped 12fps alone.
void TrapLayer::install_province_rate(Machine& m) {
    const char* e = std::getenv("THEOC_PROVINCE_MS");
    if (!e || !*e) return;

    constexpr uint32_t kSite     = 0x81da52a;  // the imm32, i.e. the push + 1
    constexpr uint32_t kStock    = 0x14585;    // 83,333 µs
    constexpr int      kMinMs    = 10;         // 100fps — past this it is silly
    constexpr int      kMaxMs    = 1000;       // 1fps

    int ms = std::atoi(e);
    if (ms < kMinMs || ms > kMaxMs) {
        std::fprintf(stderr, "  [province] THEOC_PROVINCE_MS=%s out of range "
                             "(%d-%d ms); ignored\n", e, kMinMs, kMaxMs);
        return;
    }
    uint32_t found = 0;
    try { found = m.r32(kSite); } catch (...) {
        std::fprintf(stderr, "  [province] THEOC_PROVINCE_MS: cannot read %#x; ignored\n", kSite);
        return;
    }
    if (found != kStock) {
        std::fprintf(stderr, "  [province] THEOC_PROVINCE_MS: operand at %#x is %#x, "
                             "expected %#x — refusing to patch\n", kSite, found, kStock);
        return;
    }
    uint32_t us = (uint32_t)ms * 1000;
    try { m.w32(kSite, us); } catch (...) {
        std::fprintf(stderr, "  [province] THEOC_PROVINCE_MS: write to %#x failed\n", kSite);
        return;
    }
    std::fprintf(stderr, "  [province] frame limiter %u -> %u us (%d ms, ~%.1f fps). "
                         "Sim speed scales with it: %.2fx normal.\n",
                 kStock, us, ms, 1000.0 / ms, (double)kStock / us);
}

void TrapLayer::install_gd_refresh(Machine& m, uint32_t mvos_base) {
    if (std::getenv("THEOC_LEGACY_CURSOR")) {
        std::fprintf(stderr, "  [cursor] async refresh DISABLED "
                             "(THEOC_LEGACY_CURSOR=1); pointer follows the frame rate\n");
        return;
    }
    // Entry-point override, same seam as blit.cpp: the handler runs instead of
    // the real body. Bypassing it costs nothing — the real cGD_LFB16::Refresh
    // is literally `{ return; }`.
    m.add_code_traps(mvos_base + OFF_GD_LFB16_Refresh, 1,
                     [this](Machine& mm, uint32_t, uint32_t) -> uint32_t {
                         (void)mm;
                         gd_refresh_dirty_ = true;
                         return 0;
                     }, false);
    std::fprintf(stderr, "  [cursor] cGD_LFB16::Refresh implemented "
                         "(async pointer updates; THEOC_LEGACY_CURSOR=1 reverts)\n");
}

void TrapLayer::present_async_cursor(Machine& m) {
    gd_refresh_dirty_ = false;
    if (!video_.is_open() || !gd_) return;
    uint32_t nbytes = video_.fb_bytes();
    if (nbytes > GUEST_FB_SIZE) nbytes = GUEST_FB_SIZE;
    try { m.read(GUEST_FB_BASE, video_.fb(), nbytes); } catch (...) { return; }
    video_.present();
    present_seq_.fetch_add(1, std::memory_order_relaxed);
    last_present_ = std::chrono::steady_clock::now();
}

bool TrapLayer::maybe_redirect_timer(Machine& m, uint32_t esp) {
    // Host-side SIGALRM without nested uc_emu_start (that crashes Unicorn).
    // Same pattern as sound: rewrite trap return into _TimerFunction(signo);
    // it returns into SwapBuffers__Fv after VVC present → AfterSwapBuffer.
    if (!timer_armed_ || !mvos_base_) return false;
    auto now = std::chrono::steady_clock::now();
    if (now < timer_next_) return false;

    int skipped = advance_timer_schedule();
    if (skipped < 0) return false;

    uint32_t fn = sigalrm_handler_;
    if (!fn) fn = mvos_base_ + 0x922e0;  // _TimerFunction__Fi
    uint32_t ret = m.r32(esp);
    // cdecl: [ret][signo]
    uint32_t sp = esp;
    sp -= 4; m.w32(sp, 14);
    sp -= 4; m.w32(sp, ret);
    m.redirect_guest(fn, sp);
    fps_timer_fires_++;
    static int nlog;
    if (nlog++ < 8)
        std::fprintf(stderr, "  [timer] redirect _TimerFunction (skipped schedule %d)\n", skipped);
    return true;
}

void TrapLayer::patch_sound_main_oneshot(Machine& m) {
    if (sound_main_patched_ || !mvos_base_) return;
    // Main__16cSoundCard_Linux @ 0x92b30 is a while(running) loop:
    //   92b3c: jmp 92b81   (test first)
    //   92b40: body (Do mix + write)
    //   92b81: test running; 92b8e: jne 92b40
    // For green-thread slices we need do { body } while(0):
    //   92b3c: jmp 92b40   (enter body once)
    //   92b8e: nop nop     (don't loop back)
    try {
        uint8_t enter_body[2] = {0xEB, 0x02};  // jmp +2 → 92b40
        m.write(mvos_base_ + 0x92b3c, enter_body, 2);
        uint8_t nops[2] = {0x90, 0x90};
        m.write(mvos_base_ + 0x92b8e, nops, 2);
        sound_main_patched_ = true;
        std::fprintf(stderr, "  [audio] SoundCard Main patched to one-shot mix\n");
    } catch (...) {
        std::fprintf(stderr, "  [audio] failed to patch SoundCard Main\n");
    }
}

bool TrapLayer::maybe_redirect_sound(Machine& m, uint32_t esp) {
    // Green-run Entry → Main (one-shot after patch) → write /dev/dsp, producing
    // one OSS fragment (~91ms of audio). This is BUFFER-DRIVEN, not clock-driven:
    // a fixed ~one-fragment-per-90ms gate produces ~11/s == the exact 44.1k int16/s
    // drain, leaving zero margin, so any jitter underruns → audio stutter. Instead
    // we top the queue up to a small target (~120ms, THEOC_AUDIO_MS) whenever it
    // drains below (guard further down), with only a light floor to avoid re-firing
    // every yield. Serviced from both present AND usleep so throughput is decoupled
    // from the frame rate; the target depth is the audio latency.
    if (redirecting_sound_ || soft_threads_.empty()) return false;
    auto now = std::chrono::steady_clock::now();
    if (next_sound_slice_.time_since_epoch().count() != 0 && now < next_sound_slice_)
        return false;

    SoftThread* pick = nullptr;
    for (auto& t : soft_threads_) {
        if (!t.entry || !t.arg) continue;
        uint8_t run = 0;
        try { m.read(t.arg + 0x10, &run, 1); } catch (...) { continue; }
        if (run) { pick = &t; break; }
    }
    if (!pick) return false;

    // Keep the queue topped to a small target: enough to absorb inter-yield
    // jitter (steady-state yields are ~16ms apart) but low enough that queued
    // audio isn't heard late. This depth IS the audio latency; too high delays
    // SFX, too low re-introduces underrun stutter. THEOC_AUDIO_MS tunes it.
    static const size_t audio_target = []{
        const char* e = std::getenv("THEOC_AUDIO_MS");
        int ms = e ? atoi(e) : 120;              // ~120ms buffer / latency
        return (size_t)(44100.0 * ms / 1000.0);  // int16 entries @ 22050Hz stereo
    }();
    {
        std::lock_guard<std::mutex> lock(audio_mu_);
        if (audio_q_.size() > audio_target)
            return false;
    }

    uint32_t ret = m.r32(esp);
    uint32_t sp = esp;
    sp -= 4;
    m.w32(sp, pick->arg);
    sp -= 4;
    m.w32(sp, ret);
    next_sound_slice_ = now + std::chrono::milliseconds(15);  // light floor; buffer guard is the real limit
    fps_sound_fires_++;
    static int nred;
    if (nred++ < 4)
        std::fprintf(stderr, "  [audio] green-run Entry=%#x arg=%#x\n", pick->entry, pick->arg);
    m.redirect_guest(pick->entry, sp);
    return true;
}

uint32_t TrapLayer::dispatch(Machine& m, uint32_t slot, uint32_t esp) {
    if (slot >= names_.size()) return 0;
    hits_[slot]++;
    // Watchdog breadcrumbs (relaxed; names_ is fixed after construction).
    trap_seq_.fetch_add(1, std::memory_order_relaxed);
    last_trap_.store(names_[slot].c_str(), std::memory_order_relaxed);
    auto it = table_.find(names_[slot]);
    if (it != table_.end()) {
        SlowSection slow(this, names_[slot].c_str());
        return it->second(m, esp);
    }
    if (hits_[slot] == 1)   // first hit only, to keep the log readable
        std::fprintf(stderr, "  [trap] TODO %s\n", names_[slot].c_str());
    return 0;
}

void TrapLayer::stop_watchdog() {
    // Called once the run is winding down. Past this point frames legitimately
    // stop (window hold, teardown), and Video::keep_open_for presents without
    // going through our counter — so leaving the watchdog armed manufactures a
    // "host-side stall" that is really just the process exiting.
    if (!wd_thread_.joinable()) return;
    wd_stop_.store(true, std::memory_order_relaxed);
    wd_thread_.join();
}

void TrapLayer::report() const {
    uint32_t impl = 0, todo = 0; uint64_t impl_hits = 0, todo_hits = 0;
    std::vector<std::pair<std::string, uint64_t>> todos;
    for (uint32_t i = 0; i < names_.size(); ++i) {
        if (!hits_[i]) continue;
        bool have = table_.count(names_[i]) != 0;
        if (have) { impl++; impl_hits += hits_[i]; }
        else      { todo++; todo_hits += hits_[i]; todos.push_back({names_[i], hits_[i]}); }
    }
    // stderr, not stdout: this is the end-of-run allocator state, and the
    // documented capture recipe for a session is `2>session.log`. On stdout it
    // went to the terminal and was gone with the scrollback — the 2026-07-31
    // two-hour session ended normally and still has no final heap figure.
    // Everything else diagnostic ([health], [fps], [watchdog], faults) is
    // already on stderr; this was the one instrument that was not.
    std::fprintf(stderr, "\n=== trap report ===\n");
    std::fprintf(stderr, "  implemented imports hit: %u  (%llu calls)\n", impl,
                 (unsigned long long)impl_hits);
    std::fprintf(stderr, "  UNIMPLEMENTED hit:       %u  (%llu calls)\n", todo,
                 (unsigned long long)todo_hits);
    std::fprintf(stderr, "  guest heap:              %.1f MB live, %.1f MB frontier, "
                 "%.0f MB arena (%zu free blocks)\n",
                 heap_live_ / 1048576.0, (heap_next_ - HEAP_BASE) / 1048576.0,
                 HEAP_SIZE / 1048576.0, free_addr_.size());
    std::sort(todos.begin(), todos.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (auto& [nm, n] : todos)
        std::fprintf(stderr, "    %6llu  %s\n", (unsigned long long)n, nm.c_str());
    std::fflush(stderr);
}

uint32_t TrapLayer::make_device(Machine& m, const char* kind) {
    // Video device: 0x40-byte cVVC_Linux_X shell (vtable @ +0x28).
    // Mouse/pointer: full cMouse/cPointer layout so EVENT_Move/Buttons work.
    // Keyboard: large enough for key matrix + queue (PushKey layout).
    uint32_t obj_size = 0x40;
    if (std::strcmp(kind, "mouse") == 0 || std::strcmp(kind, "pointer") == 0)
        obj_size = 0x30;
    else if (std::strcmp(kind, "keyboard") == 0)
        obj_size = 0x90;
    else if (std::strcmp(kind, "video") == 0)
        obj_size = 0x40;

    uint32_t obj = bump_alloc(obj_size);
    if (!obj) return 0;
    {
        std::vector<uint8_t> z(obj_size, 0);
        m.write(obj, z.data(), obj_size);
    }

    if (std::strcmp(kind, "mouse") == 0 || std::strcmp(kind, "pointer") == 0) {
        // Match __6cMouse / __8cPointer: ring of 0x100 × 12-byte events.
        // +0x08 = read index, +0x0c = write index (empty when equal). See
        // EVENT_Move / GetNextEvent in libmvos.
        uint32_t buf = bump_alloc(0xc00);
        {
            std::vector<uint8_t> z(0xc00, 0);
            m.write(buf, z.data(), 0xc00);
        }
        m.w32(obj + 0x00, buf);
        m.w32(obj + 0x04, 0x100);       // capacity
        m.w32(obj + 0x08, 0);           // read idx
        m.w32(obj + 0x0c, 0);           // write idx
        // +0x14/+0x18 pos, +0x1c buttons — zeroed
        //
        // +0x20 = driver/backend object with a vtable. Real X plugin fills this;
        // GameSession_LoadSettings calls VMouse[+0x20]->vtbl[+0x14/+0x18]
        // (grab/ungrab style). GetNextEvent also does vtbl[+0xc](mouse).
        // Synthetic: a small object whose vtable is all Plugin_NoopOK.
        uint32_t noop = 0;
        for (uint32_t i = 0; i < plugin_exports_.size(); ++i)
            if (plugin_exports_[i] == "Plugin_NoopOK") noop = plugin_trap_base_ + i;
        {
            // Driver vtable at +0x20. GetNextEvent calls [vt+0xc](mouse) as
            // cdecl (caller add $4). Just return 0 — then GetNextEvent drains
            // the ring itself. Plugin traps are also fine but guest stubs are
            // lighter and avoid trap re-entry during present.
            uint32_t stub0 = stub_alloc(m, 8);
            const uint8_t zero_stub[] = {0x31, 0xc0, 0xc3}; // xor eax,eax; ret
            if (stub0) m.write(stub0, zero_stub, sizeof zero_stub);
            uint32_t dvt = bump_alloc(16 * 4);
            for (uint32_t i = 0; i < 16; ++i) m.w32(dvt + 4 * i, stub0);
            m.w32(obj + 0x20, dvt);
            (void)noop;
        }
    } else if (std::strcmp(kind, "keyboard") == 0) {
        // cKeyboard layout (from __9cKeyboardPCl):
        //   +0x0c..+0x6f key-state matrix (PushKey writes here)
        //   +0x70..+0x7c event ring (buf/cap/rd/wr)
        //   +0x84 driver/function table pointer
        // cVOEditRow::Process probes shift via:
        //   call (*([VKeyboard+0x84]+0x10))(VKeyboard, keycode)  // 0x37/0x38
        // Our shell left +0x84 null → fault accessing 0x10 on save-name popup.
        uint32_t kbuf = bump_alloc(0x800);
        {
            std::vector<uint8_t> z(0x800, 0);
            m.write(kbuf, z.data(), 0x800);
        }
        m.w32(obj + 0x70, kbuf);
        m.w32(obj + 0x74, 0x100);
        m.w32(obj + 0x78, 0);
        m.w32(obj + 0x7c, 0);
        // Driver function table at +0x84. Conventions (from disasm):
        //   Process shift probe [vt+0x10](this, code) — cdecl, caller add $8
        //   PushKeyInput      [vt+0x0c](sret, this)  — callee ret $4, caller add $4
        uint32_t stub0 = stub_alloc(m, 8);
        const uint8_t zero_cdecl[] = {0x31, 0xc0, 0xc3}; // xor eax,eax; ret
        if (stub0) m.write(stub0, zero_cdecl, sizeof zero_cdecl);
        uint32_t stub_key = stub_alloc(m, 32);
        const uint8_t key_stub[] = {
            0x8B, 0x44, 0x24, 0x08,       // mov eax, [esp+8] ; code
            0x8B, 0x4C, 0x24, 0x04,       // mov ecx, [esp+4] ; this
            0x0F, 0xB6, 0x44, 0x01, 0x0C, // movzx eax, byte [ecx+eax+0x0c]
            0x85, 0xC0,                   // test eax, eax
            0x0F, 0x95, 0xC0,             // setne al
            0x0F, 0xB6, 0xC0,             // movzx eax, al
            0xC3,                         // ret (cdecl)
        };
        if (stub_key) m.write(stub_key, key_stub, sizeof key_stub);
        // Driver slot +0x0c "next key event" [vt+0x0c](sret, this). The sret
        // buffer is {int keycode; int flags} — NOT {count, key}:
        //   keycode == 0            → queue empty
        //   (char)flags  < 0        → press   (bit 7 set)
        //   (char)flags >= 0        → release
        //   flags & 1               → "clear the key matrix" request
        // Two guest consumers, and they disagree about what matters:
        //   cIntuition::PushKeyInput (mvos+0x8e670) drains it into the Intuition
        //     ring, and re-polls while (flags & 1) BEFORE testing keycode == 0.
        //     So a stale odd flags word here is an infinite loop in the guest.
        //   External_PlayAnim's cutscene loop (mvos+0xa1850) breaks on
        //     keycode == 1 && (char)flags >= 0 — that is how intros are skipped;
        //     it bypasses the Intuition ring entirely.
        // We keep the mailbox EMPTY except while a cutscene is on screen, so the
        // normal input path is untouched (keys already reach the game through
        // the Intuition ring and the cKeyboard matrix — feeding them here too
        // would deliver every keystroke twice).
        key_mailbox_ = bump_alloc(8);
        if (key_mailbox_) { m.w32(key_mailbox_, 0); m.w32(key_mailbox_ + 4, 0); }
        uint32_t stub_next = stub_alloc(m, 64);
        const uint32_t mb = key_mailbox_;
        const uint8_t next_stub[] = {
            0x8B, 0x44, 0x24, 0x04,                   // mov eax, [esp+4]  sret
            0x8B, 0x0D, (uint8_t)mb, (uint8_t)(mb >> 8),
                        (uint8_t)(mb >> 16), (uint8_t)(mb >> 24),   // mov ecx,[mb]
            0x89, 0x08,                               // mov [eax], ecx    count
            0x8B, 0x0D, (uint8_t)(mb + 4), (uint8_t)((mb + 4) >> 8),
                        (uint8_t)((mb + 4) >> 16), (uint8_t)((mb + 4) >> 24),
            0x89, 0x48, 0x04,                         // mov [eax+4], ecx  flags
            // Consume: BOTH words. Clearing only the keycode leaves the flags
            // word set, and PushKeyInput spins on (flags & 1) forever.
            0xC7, 0x05, (uint8_t)mb, (uint8_t)(mb >> 8),
                        (uint8_t)(mb >> 16), (uint8_t)(mb >> 24),
                        0x00, 0x00, 0x00, 0x00,       // mov dword [mb], 0
            0xC7, 0x05, (uint8_t)(mb + 4), (uint8_t)((mb + 4) >> 8),
                        (uint8_t)((mb + 4) >> 16), (uint8_t)((mb + 4) >> 24),
                        0x00, 0x00, 0x00, 0x00,       // mov dword [mb+4], 0
            0xC2, 0x04, 0x00,                         // ret $4 (eax = sret)
        };
        if (stub_next && key_mailbox_) m.write(stub_next, next_stub, sizeof next_stub);
        uint32_t kvt = bump_alloc(16 * 4);
        for (uint32_t i = 0; i < 16; ++i) m.w32(kvt + 4 * i, stub0);
        m.w32(kvt + 0x0c, stub_next);
        m.w32(kvt + 0x10, stub_key);
        m.w32(obj + 0x84, kvt);
    } else if (std::strcmp(kind, "video") == 0) {
        uint32_t vt = bump_alloc(16 * 4);
        uint32_t noop = 0, setmode = 0;
        for (uint32_t i = 0; i < plugin_exports_.size(); ++i) {
            if (plugin_exports_[i] == "Plugin_NoopOK") noop = plugin_trap_base_ + i;
            if (plugin_exports_[i] == "Plugin_SetVideoMode") setmode = plugin_trap_base_ + i;
        }
        for (uint32_t i = 0; i < 16; ++i) m.w32(vt + 4 * i, noop);
        if (setmode) m.w32(vt + 0xc, setmode);
        m.w32(obj + 0x28, vt);
        std::fprintf(stderr, "  [plugin] Create*%sDevice -> obj %#x vt %#x\n", kind, obj, vt);
        return obj;
    }
    std::fprintf(stderr, "  [plugin] Create*%sDevice -> obj %#x (input shell)\n", kind, obj);
    return obj;
}

// Host-side ring write matching EVENT_Move / EVENT_Buttons (libmvos cMouse).
// +0x08 = read idx, +0x0c = write idx (empty when equal). Slot size 12 bytes.
void TrapLayer::mouse_event_move(uint32_t dev, int32_t x, int32_t y) {
    if (!dev || !machine_) return;
    Machine& m = *machine_;
    m.w32(dev + 0x14, (uint32_t)x);
    m.w32(dev + 0x18, (uint32_t)y);
    uint32_t buf = m.r32(dev);
    uint32_t cap = m.r32(dev + 0x04);
    if (!buf || !cap) return;
    uint32_t wr = m.r32(dev + 0x0c);
    uint32_t off = wr * 12;
    m.w32(buf + off, 1);              // type = move
    m.w32(buf + off + 4, (uint32_t)x);
    m.w32(buf + off + 8, (uint32_t)y);
    wr++;
    if (wr >= cap) wr = 0;
    m.w32(dev + 0x0c, wr);
}

void TrapLayer::mouse_event_buttons(uint32_t dev, uint8_t buttons) {
    if (!dev || !machine_) return;
    Machine& m = *machine_;
    m.write(dev + 0x1c, &buttons, 1);
    uint32_t buf = m.r32(dev);
    uint32_t cap = m.r32(dev + 0x04);
    if (!buf || !cap) return;
    uint32_t wr = m.r32(dev + 0x0c);
    uint32_t off = wr * 12;
    m.w32(buf + off, 2);  // type = buttons
    m.w32(buf + off + 4, buttons);
    m.w32(buf + off + 8, 0);
    wr++;
    if (wr >= cap) wr = 0;
    m.w32(dev + 0x0c, wr);
}

uint32_t TrapLayer::pointer_sprite() const {
    if (!machine_) return 0;
    Machine& m = *machine_;
    uint32_t intu = intuition_obj();
    if (!intu) return 0;
    uint32_t scr = m.r32(intu + 0x24);
    if (!scr) return 0;
    return m.r32(scr + 0x24);
}

// Host stand-in for Process__9cSprClick (TimerProc → vt+0x10).
// Real path: setitimer(SIGALRM) → cTimerSystem_Linux::Proc → TimerProc__10cIntuition
// → sprite->Process(). We have no host signal timer, so advance from present.
// Layout: +0x58 cAnimBitmap*, +0x5c frame index; anim +0x38 = frame count.
// Throttle: every 3rd tick (matches mvos global counter @ aefda).
// Buttons held (Intuition+0xa8): frame++ toward last; else frame-- to 0 (idle).
void TrapLayer::tick_pointer_click_anim() {
    if (!machine_ || !mvos_base_) return;
    Machine& m = *machine_;
    uint32_t spr = pointer_sprite();
    if (!spr) return;

    // Only cSprClick-like sprites have a useful Process at vt+0x10.
    uint32_t vt = m.r32(spr + 0x08);
    if (!vt) return;
    uint32_t proc = m.r32(vt + 0x10);
    // Process__9cSprClick @ mvos 0x8ba70; Process__14cSprABitmapAdd @ 0x8c2f0;
    // weak Process__7cSprite @ 0x987e0 is a no-op we can skip.
    uint32_t click_proc = mvos_base_ + 0x8ba70;
    uint32_t abm_proc = mvos_base_ + 0x8c2f0;
    if (proc != click_proc && proc != abm_proc) return;

    // Shared throttle word in mvos .data (same as guest Process).
    uint32_t thr_addr = mvos_base_ + (proc == click_proc ? 0xaefda : 0xaefdc);
    uint16_t thr = 0;
    m.read(thr_addr, &thr, 2);
    thr = (uint16_t)(thr + 1);
    uint16_t limit = (proc == click_proc) ? 2 : 5;  // every 3rd / 6th call
    if (thr <= limit) {
        m.write(thr_addr, &thr, 2);
        return;
    }
    thr = 0;
    m.write(thr_addr, &thr, 2);

    uint32_t anim = m.r32(spr + 0x58);
    if (!anim) return;
    uint32_t nframes = m.r32(anim + 0x38);
    if (nframes < 1) return;

    if (proc == click_proc) {
        // Click hand: advance while any mouse button held, else retreat to 0.
        uint32_t intu = intuition_obj();
        uint16_t buttons = 0;
        if (intu) m.read(intu + 0xa8, &buttons, 2);
        uint32_t frame = m.r32(spr + 0x5c);
        if (buttons) {
            if (frame + 1 < nframes) frame++;
        } else {
            if (frame > 0) frame--;
        }
        m.w32(spr + 0x5c, frame);
    } else {
        // Idle multi-frame pointer: cycle +0x60.
        uint32_t frame = m.r32(spr + 0x60) + 1;
        if (frame >= nframes) frame = 0;
        m.w32(spr + 0x60, frame);
    }
}

// Intuition stores the pointer the game actually polls:
//   GetIPointerPos → this+0xa0 (tPoint {x,y})
//   GetIMouseButtons → *(uint16*)(this+0xa8)
// And ProcessInputs drains the 8-byte event ring at this+0x28 (fed by
// PushMouseInput in real SwapBuffers — which our HLE skips).
uint32_t TrapLayer::intuition_obj() const {
    if (!machine_) return 0;
    Machine& m = *machine_;
    uint32_t intu = 0;
    if (uint32_t g = game_glob("Intuition")) intu = m.r32(g);
    if (!intu && mvos_base_) intu = m.r32(mvos_base_ + 0xaefe4);
    return intu;
}

void TrapLayer::push_intuition_event(uint32_t type, uint32_t payload) {
    if (!machine_) return;
    Machine& m = *machine_;
    uint32_t intu = intuition_obj();
    if (!intu) return;
    // cIntuition ctor: ring object embedded at +0x28
    //   +0x28 buf*, +0x2c cap, +0x30 read, +0x34 write; slots are 8 bytes.
    uint32_t ring = intu + 0x28;
    uint32_t buf = m.r32(ring + 0x00);
    uint32_t cap = m.r32(ring + 0x04);
    if (!buf || !cap) return;
    int32_t wr = (int32_t)m.r32(ring + 0x0c);
    // write idx < 0 means "use read-idx path" in EVENT_*; normal is >= 0.
    if (wr < 0) wr = (int32_t)m.r32(ring + 0x08);
    uint32_t slot = (uint32_t)wr;
    if (slot >= cap) slot = 0;
    m.w32(buf + slot * 8, type);
    m.w32(buf + slot * 8 + 4, payload);
    slot++;
    if (slot >= cap) slot = 0;
    m.w32(ring + 0x0c, slot);
    // Overflow: write caught up with read → mark full as -1 (libmvos convention).
    if (slot == m.r32(ring + 0x08))
        m.w32(ring + 0x0c, 0xffffffffu);
}

void TrapLayer::push_intuition_move(int x, int y) {
    // type 1: payload = two packed uint16 {x,y} (see PushMouseInput).
    uint32_t payload = (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16);
    push_intuition_event(1, payload);
}

void TrapLayer::push_intuition_button_edges(uint8_t prev, uint8_t now) {
    // type 4: subcode 0=L↓ 1=L↑ 2=R↓ 3=R↑ 4=M↓ 5=M↑ (ProcessInputs jump table).
    auto edge = [&](uint8_t bit, uint32_t down_code, uint32_t up_code) {
        bool was = (prev & bit) != 0, is = (now & bit) != 0;
        if (!was && is) push_intuition_event(4, down_code);
        if (was && !is) push_intuition_event(4, up_code);
    };
    edge(1, 0, 1);
    edge(2, 2, 3);
    edge(4, 4, 5);
}

void TrapLayer::update_intuition_pointer(int x, int y, uint8_t buttons) {
    if (!machine_) return;
    Machine& m = *machine_;
    uint32_t intu = intuition_obj();
    if (!intu) return;
    m.w32(intu + 0xa0, (uint32_t)x);
    m.w32(intu + 0xa4, (uint32_t)y);
    m.w32(intu + 0x14, (uint32_t)x);   // SetPointerPos fields too
    m.w32(intu + 0x18, (uint32_t)y);
    uint16_t bw = buttons;
    m.write(intu + 0xa8, &bw, 2);
    // Keep active-screen pointer sprite at the mouse (MoveTo is gated on
    // Intuition_Mode which we leave 0 to avoid input-recording Fatals).
    uint32_t scr = m.r32(intu + 0x24);
    uint32_t spr = scr ? m.r32(scr + 0x24) : 0;
    if (spr) {
        uint8_t busy = 0;
        m.read(spr + 0x4c, &busy, 1);
        if (!busy) {
            m.w32(spr + 0x4d, (uint32_t)x);
            m.w32(spr + 0x51, (uint32_t)y);
        }
    }
}

// Map SDL scancodes → libmvos eKeyCode (KeyTableConvert in keyboard_x plugin).
// Dense enum 1..0x63; NOT IBM XT scancodes. 0 = unmapped.
static uint32_t sdl_scancode_to_ekey(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_1: return 0x02;
    case SDL_SCANCODE_2: return 0x03;
    case SDL_SCANCODE_3: return 0x04;
    case SDL_SCANCODE_4: return 0x05;
    case SDL_SCANCODE_5: return 0x06;
    case SDL_SCANCODE_6: return 0x07;
    case SDL_SCANCODE_7: return 0x08;
    case SDL_SCANCODE_8: return 0x09;
    case SDL_SCANCODE_9: return 0x0a;
    case SDL_SCANCODE_0: return 0x0b;
    case SDL_SCANCODE_A: return 0x0c;
    case SDL_SCANCODE_B: return 0x0d;
    case SDL_SCANCODE_C: return 0x0e;
    case SDL_SCANCODE_D: return 0x0f;
    case SDL_SCANCODE_E: return 0x10;
    case SDL_SCANCODE_F: return 0x11;
    case SDL_SCANCODE_G: return 0x12;
    case SDL_SCANCODE_H: return 0x13;
    case SDL_SCANCODE_I: return 0x14;
    case SDL_SCANCODE_J: return 0x15;
    case SDL_SCANCODE_K: return 0x16;
    case SDL_SCANCODE_L: return 0x17;
    case SDL_SCANCODE_M: return 0x18;
    case SDL_SCANCODE_N: return 0x19;
    case SDL_SCANCODE_O: return 0x1a;
    case SDL_SCANCODE_P: return 0x1b;
    case SDL_SCANCODE_Q: return 0x1c;
    case SDL_SCANCODE_R: return 0x1d;
    case SDL_SCANCODE_S: return 0x1e;
    case SDL_SCANCODE_T: return 0x1f;
    case SDL_SCANCODE_U: return 0x20;
    case SDL_SCANCODE_V: return 0x21;
    case SDL_SCANCODE_W: return 0x22;
    case SDL_SCANCODE_X: return 0x23;
    case SDL_SCANCODE_Y: return 0x24;
    case SDL_SCANCODE_Z: return 0x25;
    case SDL_SCANCODE_F1:  return 0x26;
    case SDL_SCANCODE_F2:  return 0x27;
    case SDL_SCANCODE_F3:  return 0x28;
    case SDL_SCANCODE_F4:  return 0x29;
    case SDL_SCANCODE_F5:  return 0x2a;
    case SDL_SCANCODE_F6:  return 0x2b;
    case SDL_SCANCODE_F7:  return 0x2c;
    case SDL_SCANCODE_F8:  return 0x2d;
    case SDL_SCANCODE_F9:  return 0x2e;
    case SDL_SCANCODE_F10: return 0x2f;
    case SDL_SCANCODE_F11: return 0x30;
    case SDL_SCANCODE_F12: return 0x31;
    case SDL_SCANCODE_UP:        return 0x32;
    case SDL_SCANCODE_RIGHT:     return 0x33;
    case SDL_SCANCODE_DOWN:      return 0x34;
    case SDL_SCANCODE_LEFT:      return 0x35;
    case SDL_SCANCODE_BACKSPACE: return 0x36;
    case SDL_SCANCODE_LSHIFT:    return 0x37;
    case SDL_SCANCODE_RSHIFT:    return 0x38;
    case SDL_SCANCODE_LCTRL:     return 0x39;
    case SDL_SCANCODE_RCTRL:     return 0x3a;
    case SDL_SCANCODE_LALT:      return 0x3b;
    case SDL_SCANCODE_RALT:      return 0x3c;
    case SDL_SCANCODE_CAPSLOCK:  return 0x3d;
    case SDL_SCANCODE_LGUI:      return 0x3e;
    case SDL_SCANCODE_RGUI:      return 0x40;
    case SDL_SCANCODE_APPLICATION: return 0x42;
    case SDL_SCANCODE_MINUS:     return 0x43;
    case SDL_SCANCODE_EQUALS:    return 0x44;
    case SDL_SCANCODE_TAB:       return 0x45;
    case SDL_SCANCODE_HOME:      return 0x46;
    case SDL_SCANCODE_END:       return 0x47;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:  return 0x48;
    case SDL_SCANCODE_SEMICOLON: return 0x49;
    case SDL_SCANCODE_APOSTROPHE: return 0x4a;
    case SDL_SCANCODE_GRAVE:     return 0x4b;
    case SDL_SCANCODE_BACKSLASH: return 0x4c;
    case SDL_SCANCODE_COMMA:     return 0x4d;
    case SDL_SCANCODE_PERIOD:    return 0x4e;
    case SDL_SCANCODE_SLASH:     return 0x4f;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x50;
    case SDL_SCANCODE_SPACE:     return 0x51;
    case SDL_SCANCODE_KP_7:      return 0x52;
    case SDL_SCANCODE_KP_8:      return 0x53;
    case SDL_SCANCODE_KP_9:      return 0x54;
    case SDL_SCANCODE_KP_MINUS:  return 0x55;
    case SDL_SCANCODE_KP_4:      return 0x56;
    case SDL_SCANCODE_KP_6:      return 0x57;
    case SDL_SCANCODE_KP_PLUS:   return 0x58;
    case SDL_SCANCODE_KP_1:      return 0x59;
    case SDL_SCANCODE_KP_2:      return 0x5a;
    case SDL_SCANCODE_KP_3:      return 0x5b;
    case SDL_SCANCODE_KP_0:      return 0x5c;
    case SDL_SCANCODE_KP_PERIOD: return 0x5d;
    case SDL_SCANCODE_PAGEUP:    return 0x5f;
    case SDL_SCANCODE_PAGEDOWN:  return 0x60;
    case SDL_SCANCODE_INSERT:    return 0x61;
    case SDL_SCANCODE_DELETE:    return 0x62;
    case SDL_SCANCODE_PAUSE:     return 0x63;
    default: return 0;
    }
}

void TrapLayer::on_sdl_event(const SDL_Event& e) {
    if (!machine_) return;
    Machine& m = *machine_;
    uint32_t vmouse = game_glob("VMouse") ? m.r32(game_glob("VMouse")) : 0;
    uint32_t vptr   = m.r32(mvos_base_ + 0xaef9c);

    auto clamp = [&](int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };

    if (e.type == SDL_MOUSEMOTION) {
        int x = e.motion.x, y = e.motion.y;
        if (video_.is_open()) {
            x = clamp(x, 0, video_.width() - 1);
            y = clamp(y, 0, video_.height() - 1);
        }
        mouse_x_ = x; mouse_y_ = y;
        if (vmouse) mouse_event_move(vmouse, x, y);
        if (vptr)   mouse_event_move(vptr, x, y);
        update_intuition_pointer(x, y, mouse_buttons_);
        push_intuition_move(x, y);
    } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        uint8_t bit = 0;
        if (e.button.button == SDL_BUTTON_LEFT)   bit = 1;
        if (e.button.button == SDL_BUTTON_RIGHT)  bit = 2;
        if (e.button.button == SDL_BUTTON_MIDDLE) bit = 4;
        uint8_t prev = mouse_buttons_;
        if (e.type == SDL_MOUSEBUTTONDOWN) mouse_buttons_ = (uint8_t)(mouse_buttons_ | bit);
        else                              mouse_buttons_ = (uint8_t)(mouse_buttons_ & ~bit);
        // Also update position from the button event.
        if (video_.is_open()) {
            mouse_x_ = clamp(e.button.x, 0, video_.width() - 1);
            mouse_y_ = clamp(e.button.y, 0, video_.height() - 1);
        }
        if (vmouse) {
            mouse_event_move(vmouse, mouse_x_, mouse_y_);
            mouse_event_buttons(vmouse, mouse_buttons_);
        }
        if (vptr) {
            mouse_event_move(vptr, mouse_x_, mouse_y_);
            mouse_event_buttons(vptr, mouse_buttons_);
        }
        update_intuition_pointer(mouse_x_, mouse_y_, mouse_buttons_);
        push_intuition_move(mouse_x_, mouse_y_);
        push_intuition_button_edges(prev, mouse_buttons_);
        // THEOC_REPORT_CLICKS=1: log every click, in a form that can be pasted
        // straight into a self-driver script. The active cScreen* changes on
        // every screen transition, so it doubles as a screen identity — clicks
        // sharing a screen value belong to the same screen.
        static const bool report_clicks = std::getenv("THEOC_REPORT_CLICKS") != nullptr;
        if (report_clicks && e.type == SDL_MOUSEBUTTONDOWN) {
            uint32_t intu = intuition_obj();
            uint32_t scr = 0;
            if (intu && machine_) { try { scr = machine_->r32(intu + 0x24); } catch (...) {} }
            // stderr, for the reason given at the Alt+Enter handler: operator
            // actions have to line up against the [health] samples, and a
            // session is captured with `2>`.
            std::fprintf(stderr, "  [click] %d,%d  btn=%u  win=%dx%d  screen=%#x\n",
                         mouse_x_, mouse_y_, (unsigned)bit,
                         video_.width(), video_.height(), scr);
            std::fflush(stderr);
        }
        static int nlog;
        if (nlog++ < 16)
            std::fprintf(stderr, "  [input] mouse btn mask=%u→%u at %d,%d (Intuition pipe)\n",
                        prev, mouse_buttons_, mouse_x_, mouse_y_);
    } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        // Alt+Enter (⌥Return on macOS — SDL maps Option to KMOD_ALT) toggles
        // fullscreen. Handled here, ahead of every guest write below, and the
        // Return is SWALLOWED: eKey 0x48 is a live game key, so leaking it would
        // confirm whatever dialog is focused while the window changes under it.
        //
        // The matching release is swallowed too. Forwarding a release for a key the
        // guest never saw pressed is the stale-key-state class that wedged the menu
        // in G16, and the key matrix at Intuition+0x3c is exactly that kind of
        // sticky state. Alt itself keeps forwarding normally (eKey 0x3b/0x3c), so
        // the qualifier byte stays honest.
        //
        // Repeats ignored: holding the combo would otherwise thrash the mode.
        // Not F11 — the game uses it. The synthetic AUTO_KEYS/soak events never set
        // .mod, so no self-driver can trip this.
        if (e.key.keysym.scancode == SDL_SCANCODE_RETURN ||
            e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
            if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & KMOD_ALT) && !e.key.repeat) {
                fs_toggle_swallow_ = true;
                // stderr: this is a record of what the operator did, so it
                // belongs in the same stream as the [health] samples it has to
                // be lined up against. On stdout a `2>trial.log` capture kept
                // the allocation step and lost the toggle that caused it.
                std::fprintf(stderr, "  [video] Alt+Enter → %s\n",
                             video_.is_fullscreen() ? "windowed" : "fullscreen");
                std::fflush(stderr);
                video_.toggle_fullscreen();
                return;
            }
            if (e.type == SDL_KEYUP && fs_toggle_swallow_) {
                fs_toggle_swallow_ = false;
                return;
            }
        }
        // THEOC_CONSOLE: Alt+V opens the dev console. Swallowed for the same
        // reason as Alt+Enter — eKey 0x21 is 'V', a live game key, and leaking
        // it would also type a stray 'v' into the console we are about to open.
        // The actual open happens at present time (maybe_redirect_console); it
        // cannot be done here because calling guest code from an SDL callback
        // would nest uc_emu_start.
        if (console_enabled_ && e.key.keysym.scancode == SDL_SCANCODE_V) {
            if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & KMOD_ALT) && !e.key.repeat) {
                console_open_pending_ = true;
                console_key_swallow_ = true;
                return;
            }
            if (e.type == SDL_KEYUP && console_key_swallow_) {
                console_key_swallow_ = false;
                return;
            }
        }
        // Alt+M stamps a numbered marker into the log. Both long sessions so far
        // were confounded the same way: the activities were in a paper notebook
        // and the samples were in the log, and lining them up after the fact
        // meant guessing which 60s interval a battle started in. A marker puts
        // the boundary in the data. It also forces the next [health] out
        // immediately (longrun_tick), so the interval boundary is the event.
        //
        // Live only while the long-session harness is armed — outside a measured
        // run Alt+M is the game's, and Alt is a modifier the game itself uses
        // (Alt+A selects all units), so claiming a key unconditionally is not
        // free. Swallowed down and up for the Alt+Enter reason: eKey for 'M' is
        // a live game key, and an unpaired release is the G16 stale-key class.
        if (longrun_ && e.key.keysym.scancode == SDL_SCANCODE_M) {
            if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & KMOD_ALT) && !e.key.repeat) {
                mark_pending_ = true;
                mark_key_swallow_ = true;
                ++mark_seq_;
                return;
            }
            if (e.type == SDL_KEYUP && mark_key_swallow_) {
                mark_key_swallow_ = false;
                return;
            }
        }
        // eKeyCode is *not* a PC scancode. Table is KeyTableConvert in
        // libmvos_keyboard_x (XKeysym → dense enum). ProcessInputs drains
        // Intuition ring types 8 (down) / 0x10 (up); cVOEditRow only reacts
        // to type 8. Shift qualifiers are eKey 0x37/0x38 (Intuition+0x73/74).
        uint32_t code = sdl_scancode_to_ekey(e.key.keysym.scancode);
        uint32_t vkey = game_glob("VKeyboard") ? m.r32(game_glob("VKeyboard")) : 0;
        uint32_t intu = intuition_obj();
        bool down = (e.type == SDL_KEYDOWN);
        if (code && code <= 0x63) {
            uint8_t b = down ? 1 : 0;
            // Direct matrix write: KeyMatrix / shift probes between ProcessInputs.
            if (intu) m.write(intu + 0x3c + code, &b, 1);
            if (vkey) {
                m.write(vkey + 0x0c + code, &b, 1);
                // PushKey ring: +0x70 buf, +0x74 cap, +0x78 read, +0x7c write.
                uint32_t kbuf = m.r32(vkey + 0x70);
                uint32_t cap  = m.r32(vkey + 0x74);
                int32_t wr    = (int32_t)m.r32(vkey + 0x7c);
                if (kbuf && cap) {
                    if (wr < 0) wr = (int32_t)m.r32(vkey + 0x78);
                    uint32_t slot = (uint32_t)wr;
                    if (slot >= cap) slot = 0;
                    m.w32(kbuf + slot * 8, code);
                    m.w32(kbuf + slot * 8 + 4, down ? 0x80u : 0u);
                    slot++;
                    if (slot >= cap) slot = 0;
                    m.w32(vkey + 0x7c, slot);
                    if (slot == m.r32(vkey + 0x78))
                        m.w32(vkey + 0x7c, 0xffffffffu);
                }
            }
            // Cutscene skip: External_PlayAnim polls [VKeyboard+0x84][+0x0c]
            // directly (outside the Intuition ring) and breaks on
            //   keycode == 1 && (char)flags >= 0.
            // Only post while a movie is actually on screen — outside one this
            // mailbox must stay empty or cIntuition::PushKeyInput picks it up
            // (double input at best, and it hangs on an odd flags word).
            // Deviation from the original, deliberate: the guest condition is
            // keycode 1 specifically, so stock only skips on that one key. We
            // report keycode 1 for ANY key, making every key a skip key.
            // flags = 0: release-shaped and, critically, bit 0 clear.
            // THEOC_LEGACY_KEYMB=1 reverts to never posting (unskippable).
            static const bool legacy_keymb = std::getenv("THEOC_LEGACY_KEYMB") != nullptr;
            if (down && key_mailbox_ && movie_playing_ && !legacy_keymb) {
                m.w32(key_mailbox_, 1);
                m.w32(key_mailbox_ + 4, 0);
            }
            // ProcessInputs → ProcessTree (UI / edit rows / hotkeys).
            push_intuition_event(down ? 8u : 0x10u, code);
            // Mirror SetQualifierState for polls that skip ProcessInputs.
            if (intu) {
                uint8_t q = 0;
                auto held = [&](uint32_t c) {
                    uint8_t v = 0;
                    m.read(intu + 0x3c + c, &v, 1);
                    return v != 0;
                };
                if (held(0x37) || held(0x38)) q |= 1;   // shift
                if (held(0x3b) || held(0x3c)) q |= 2;   // alt
                if (held(0x39) || held(0x3a)) q |= 4;   // ctrl
                if (held(0x3f) || held(0x41)) q |= 8;   // meta
                m.write(intu + 0xb0, &q, 1);
            }
            // 24 events is enough to see the input path come up at boot, and
            // useless for "does this one chord reach the guest?" — the budget
            // is gone long before you get in-game. THEOC_KEYLOG=1 logs every
            // key for the whole session, with the qualifier byte, so a chord
            // that never arrives can be told from one that arrives and is
            // ignored. Those need opposite fixes and look identical on screen.
            static const bool keylog = std::getenv("THEOC_KEYLOG") != nullptr;
            static int klog;
            if (keylog || klog++ < 24)
                std::fprintf(stderr, "  [input] key eKey=%#x %s sc=%d quals=%#x\n",
                            code, down ? "down" : "up",
                            (int)e.key.keysym.scancode,
                            (unsigned)(e.key.keysym.mod & (KMOD_ALT | KMOD_SHIFT | KMOD_CTRL)));
        }
    } else if (e.type == SDL_WINDOWEVENT &&
               e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        mouse_buttons_ = 0;
        if (vmouse) mouse_event_buttons(vmouse, 0);
        if (vptr)   mouse_event_buttons(vptr, 0);
        update_intuition_pointer(mouse_x_, mouse_y_, 0);
        // ReleaseAll: clear key matrices so nothing sticks after alt-tab.
        uint32_t intu = intuition_obj();
        uint32_t vkey = game_glob("VKeyboard") ? m.r32(game_glob("VKeyboard")) : 0;
        std::vector<uint8_t> z(0x64, 0);
        if (intu) {
            m.write(intu + 0x3c, z.data(), 0x64);
            uint8_t q = 0;
            m.write(intu + 0xb0, &q, 1);
        }
        if (vkey) m.write(vkey + 0x0c, z.data(), 0x64);
    }
}

uint32_t TrapLayer::dispatch_plugin(Machine& m, uint32_t slot, uint32_t esp) {
    if (slot >= plugin_exports_.size()) return 0;
    const std::string& name = plugin_exports_[slot];
    // Watchdog breadcrumbs. These handlers (OpenDisplay, SwapBuffers/present)
    // do the heaviest host-side work in the whole port, so leaving them out of
    // the counters made "stuck inside present" look identical to "stuck
    // nowhere" — a stall reported +0 traps while sitting in OpenDisplay.
    trap_seq_.fetch_add(1, std::memory_order_relaxed);
    last_trap_.store(name.c_str(), std::memory_order_relaxed);
    SlowSection slow(this, name.c_str());
    if (name == "QueryDevice") return 1;
    if (name == "CreateVideoDevice")    return make_device(m, "video");
    if (name == "CreateKeyboardDevice") return make_device(m, "keyboard");
    if (name == "CreateMouseDevice")    return make_device(m, "mouse");
    if (name == "CreatePointerDevice")  return make_device(m, "pointer");
    if (name == "Plugin_NoopOK")        return 1;
    if (name == "Plugin_Return0")       return 0;
    // bool KeyMatrix-ish(cKeyboard* this, eKeyCode code) — matrix @ this+0x0c.
    if (name == "Plugin_KeyMatrix") {
        uint32_t self = arg(m, esp, 0);
        uint32_t code = arg(m, esp, 1);
        if (!self || code > 0x63) return 0;
        uint8_t b = 0;
        m.read(self + 0x0c + code, &b, 1);
        return b ? 1u : 0u;
    }
    if (name == "Plugin_SetVideoMode") {
        // cdecl: this, cVModeRequest*
        uint32_t self = arg(m, esp, 0), req = arg(m, esp, 1);
        int w = req ? (int)m.r32(req + 0) : 800;
        int h = req ? (int)m.r32(req + 4) : 600;
        int d = req ? (int)m.r32(req + 8) : 5;
        if (w <= 0) w = 800;
        if (h <= 0) h = 600;
        bool ok = video_.open(w, h, d);
        if (self) {
            m.w32(self + 0x20, (uint32_t)w);
            m.w32(self + 0x24, (uint32_t)h);
            m.w32(self + 0x1c, (uint32_t)d);
        }
        std::fprintf(stderr, "  [plugin] SetVideoMode %dx%d depth %d -> %s\n",
                    w, h, d, ok ? "ok" : "FAIL");
        return ok ? 1 : 0;
    }
    if (name == "HLE_OpenDisplay") {
        // cdecl OpenDisplay(cVVC* this, cVModeRequest& req)
        // Build a real guest cGD_LFB16 over GUEST_FB_BASE so PaintTree/Paint
        // can blit; SDL presents that buffer on SwapBuffers.
        uint32_t self = arg(m, esp, 0), req = arg(m, esp, 1);
        int w = req ? (int)m.r32(req + 0) : 800;
        int h = req ? (int)m.r32(req + 4) : 600;
        int d = req ? (int)m.r32(req + 8) : 5;
        if (w <= 0) w = 800;
        if (h <= 0) h = 600;
        bool ok;
        { SlowSection s1(this, "OpenDisplay:video_.open");
          ok = video_.open(w, h, d); }
        if (!ok) {
            std::fprintf(stderr, "  [HLE] OpenDisplay %dx%d depth %d -> FAIL (SDL)\n", w, h, d);
            return 0;
        }
        uint32_t pitch = (uint32_t)w * 2;  // RGB565
        uint32_t nbytes = pitch * (uint32_t)h;
        if (nbytes > GUEST_FB_SIZE) nbytes = GUEST_FB_SIZE;
        // Clear guest FB (and mirror into SDL).
        {
            SlowSection s2(this, "OpenDisplay:clear-fb");
            std::vector<uint8_t> z(nbytes, 0);
            m.write(GUEST_FB_BASE, z.data(), nbytes);
            if (video_.fb_bytes() >= nbytes)
                std::memcpy(video_.fb(), z.data(), nbytes);
        }

        // cDimension {w,h} on the guest stack/scratch area of the object.
        uint32_t dim = bump_alloc(8);
        m.w32(dim, (uint32_t)w);
        m.w32(dim + 4, (uint32_t)h);

        // Build cGD_LFB16 layout by hand (same fields as __9cGD_LFB16 ctor
        // @0x6bb30). Nested Machine::call from inside a trap is unreliable, so
        // we don't invoke the guest ctor.
        //   +0 w, +4 h, +8 fb*, +0xc depthCode(5), +0x10 pitch, +0x14 vtable
        uint32_t gd = bump_alloc(0x80);
        {
            std::vector<uint8_t> gz(0x80, 0);
            m.write(gd, gz.data(), 0x80);
        }
        uint32_t vt = mvos_base_ + 0xa2820;  // __vt_9cGD_LFB16 (relocated)
        m.w32(gd + 0x00, (uint32_t)w);
        m.w32(gd + 0x04, (uint32_t)h);
        m.w32(gd + 0x08, GUEST_FB_BASE);
        m.w32(gd + 0x0c, 5);                 // eBMType / depth code for LFB16
        m.w32(gd + 0x10, pitch);
        m.w32(gd + 0x14, vt);
        (void)dim;  // kept for future real-ctor path
        std::fprintf(stderr, "  [HLE] cGD_LFB16 @%#x fb=%#x %dx%d pitch=%u vt=%#x\n",
                    gd, GUEST_FB_BASE, w, h, pitch, vt);

        if (self) {
            m.w32(self + 0x20, (uint32_t)w);
            m.w32(self + 0x24, (uint32_t)h);
            m.w32(self + 0x1c, (uint32_t)d);
            // Single-buffer LFB: every GD slot points at the same cGD_LFB16.
            // Slot roles re-read off the libmvos disassembly 2026-08-03:
            //   +0x00/+0x04  the two real buffers. cVVC::SetBuffers copies one
            //                of them into +0x10 and the other into +0x14,
            //                chosen by the parity byte at +0x18.
            //   +0x10        the "current" GD — cSprite::Refresh reads it.
            //   +0x14        the paint GD — cScreen::EndRefresh and
            //                cSprite::BeforeSwapBuffer read it.
            //   +0x08        NOT a GD, despite what this comment used to say:
            //                cVVC::SwapBuffers treats it as an optional
            //                memblock-backed overlay bitmap (+0x0c cMemBlock,
            //                +0x14 refcount, +0x20/+0x24 w/h) and takes a
            //                completely different path when it is non-null.
            //                Writing gd here is meaningless — but harmless,
            //                because its only two readers are cVVC::SwapBuffers
            //                and cVVC::SetBuffers and we replace both. Left as
            //                it is rather than "fixed" blind: a guessed struct
            //                layout is this port's dominant bug class, and
            //                nothing observable depends on the value.
            m.w32(self + 0x00, gd);
            m.w32(self + 0x04, gd);
            m.w32(self + 0x08, gd);
            m.w32(self + 0x10, gd);
            m.w32(self + 0x14, gd);
            uint8_t one = 1, zero = 0;
            m.write(self + 0x18, &zero, 1);
            m.write(self + 0x19, &one, 1);
        }
        gd_ = gd;
        std::fprintf(stderr, "  [HLE] OpenDisplay %dx%d depth %d -> ok\n", w, h, d);
        return 1;
    }
    if (name == "HLE_SwapBuffers") {
        // Present only. We replace cVVC::SwapBuffers, which is the *innermost*
        // link of a three-function chain that is otherwise entirely guest code.
        // Each link has exactly one call site (libmvos, checked 2026-08-03):
        //
        //   cScreen::EndRefresh
        //     └─ SwapBuffers__Fv                    (mvos+0x8e820)
        //          ├─ rolling-demo record/compare   (only if Intuition_Mode!=0;
        //          │                                 never, for us)
        //          ├─ Frame_Counter++
        //          ├─ PushKeyInput                  (the G16 spin site)
        //          ├─ MouseRefresh
        //          │    ├─ PushMouseInput           (we nop this to a bare ret)
        //          │    └─ cSprite::MoveTo          (if Intuition_Mode==0, i.e.
        //          │                                 always here — this is what
        //          │                                 tracks the pointer)
        //          ├─ push a type-0x20 event into the Intuition+0x28 ring
        //          ├─ VBlankInProgress = 1
        //          ├─ cSprite::BeforeSwapBuffer     (SaveBg + paint pointer)
        //          ├─ cVVC::SwapBuffers             ← us, present only
        //          ├─ cSprite::AfterSwapBuffer      (restore under-cursor;
        //          │                                 patched single-buffer, G17)
        //          └─ VBlankInProgress = 0
        //
        // So the real cSprite is already composited onto the LFB by the time we
        // are called, and the guest owns cursor tracking, key input and the
        // frame counter. This is the opposite of the "abandoned guest
        // SwapBuffers path" the task list claimed for a year — the guest path is
        // load-bearing, and G17's fix is a patch *inside* it.
        if (video_.is_open()) {
            apply_edit_mode(m);   // THEOC_EDIT (no-op unless armed)
            // (A per-frame re-stamp of the VVC GD slots used to sit here. It
            // was bring-up-era defence: nothing in the guest can move those
            // slots, because cVVC::SetBuffers is their only writer and both of
            // its callers are functions we replace. Deleted 2026-08-03 after a
            // session covering every transition — movies, save/load, quit, new
            // game — measured zero drift over 466 presents. HLE_OpenDisplay is
            // now the sole writer of the slots.)
            uint32_t nbytes = video_.fb_bytes();
            if (gd_) {
                uint32_t gw = m.r32(gd_ + 0x00);
                uint32_t gh = m.r32(gd_ + 0x04);
                uint32_t pitch = m.r32(gd_ + 0x10);
                if (gw && gh && pitch) {
                    uint32_t want = pitch * gh;
                    if (want > 0 && want <= GUEST_FB_SIZE) nbytes = want;
                    if ((int)gw != video_.width() || (int)gh != video_.height())
                        video_.open((int)gw, (int)gh, video_.depth_code());
                    nbytes = video_.fb_bytes();
                    if (pitch * gh < nbytes) nbytes = pitch * gh;
                }
            }
            if (nbytes > GUEST_FB_SIZE) nbytes = GUEST_FB_SIZE;
            if (nbytes > video_.fb_bytes()) nbytes = video_.fb_bytes();
            m.read(GUEST_FB_BASE, video_.fb(), nbytes);
            // (A fallback magenta crosshair used to be drawn here whenever the
            // active screen had no pointer sprite — the G5 cursor, from before
            // the guest cSprite was composited at all. Deleted 2026-08-03: it
            // fired on 0 of 466 presents across every transition, and the guest
            // null-checks the same pointer either side of us, so a frame
            // without a sprite simply presents without a cursor.)
            { SlowSection s3(this, "present");
              video_.present(); }  // pumps SDL
            present_seq_.fetch_add(1, std::memory_order_relaxed);
            start_watchdog(m); // THEOC_WATCHDOG: arms on the first frame
            auto_keys_tick();
            soak_tick();       // THEOC_SOAK: load/unload cycle driver
            render_probe_tick();  // THEOC_CLICKS/SWEEP/SHOT: render-bug harness
            fps_tick(m);       // THEOC_FPS: per-second frame/throughput report

            // Click-hand frame advance (also done by TimerProc when timer runs).
            tick_pointer_click_anim();

            // THEOC_AUTO_MENU=1: after the 800×600 menu has presented a few
            // frames, synthesize a left-click on the Single Player button
            // (menu.cfg: "single 20 250"). Used for automated G8 bring-up.
            if (std::getenv("THEOC_AUTO_MENU") && video_.width() == 800) {
                static int menu_frames = 0;
                menu_frames++;
                if (menu_frames == 45) {
                    const int ax = 80, ay = 260;
                    mouse_x_ = ax; mouse_y_ = ay;
                    update_intuition_pointer(ax, ay, 0);
                    push_intuition_move(ax, ay);
                    std::fprintf(stderr, "  [input] AUTO_MENU aim %d,%d\n", ax, ay);
                } else if (menu_frames == 50) {
                    push_intuition_button_edges(0, 1);  // L down
                    update_intuition_pointer(mouse_x_, mouse_y_, 1);
                    mouse_buttons_ = 1;
                    std::fprintf(stderr, "  [input] AUTO_MENU L-down\n");
                } else if (menu_frames == 55) {
                    push_intuition_button_edges(1, 0);  // L up
                    update_intuition_pointer(mouse_x_, mouse_y_, 0);
                    mouse_buttons_ = 0;
                    std::fprintf(stderr, "  [input] AUTO_MENU L-up\n");
                }
            }

            // THEOC_AUTO_PROVINCE=1: self-drive menu → Prophecy → OK into the
            // province view for unattended timing tests. Wall-clock scheduled
            // (fps varies wildly across screens; frame counts are unreliable).
            //   (80,260)  = Prophecy / new game button
            //   (466,537) = "OK" that starts the game in province view
            if (std::getenv("THEOC_AUTO_PROVINCE")) {
                using clock = std::chrono::steady_clock;
                if (auto_prov_t0_.time_since_epoch().count() == 0)
                    auto_prov_t0_ = clock::now();
                double t = std::chrono::duration<double>(clock::now() - auto_prov_t0_).count();
                struct Step { double t; int x, y, act; };  // act 0=aim 1=down 2=up
                static const Step steps[] = {
                    {1.5,  80, 260, 0}, {1.7,  80, 260, 1}, {1.9,  80, 260, 2},
                    {3.5, 466, 537, 0}, {3.7, 466, 537, 1}, {3.9, 466, 537, 2},
                };
                const int nsteps = (int)(sizeof steps / sizeof steps[0]);
                if (auto_prov_stage_ < nsteps && t >= steps[auto_prov_stage_].t) {
                    const Step& s = steps[auto_prov_stage_];
                    if (s.act == 0) {
                        mouse_x_ = s.x; mouse_y_ = s.y;
                        update_intuition_pointer(s.x, s.y, 0);
                        push_intuition_move(s.x, s.y);
                    } else if (s.act == 1) {
                        push_intuition_button_edges(0, 1);
                        update_intuition_pointer(mouse_x_, mouse_y_, 1);
                        mouse_buttons_ = 1;
                    } else {
                        push_intuition_button_edges(1, 0);
                        update_intuition_pointer(mouse_x_, mouse_y_, 0);
                        mouse_buttons_ = 0;
                    }
                    std::fprintf(stderr, "  [auto-prov] stage %d act %d at %d,%d (t=%.1fs)\n",
                                auto_prov_stage_, s.act, s.x, s.y, t);
                    auto_prov_stage_++;
                }
            }

            // Frame-rate cap: physics/animation is frame-tied (engine quirk), so
            // uncapped render = turbo sim. Clamp the minimum present interval to
            // the game's own designed province cadence: cProvince::Do's frame
            // limiter targets 0x14585 µs == 83.3ms == exactly 12fps (default;
            // THEOC_FRAME_MS overrides, 0 disables). Present-to-present timing so
            // only too-fast frames are slowed — the game's usleep pacing already
            // counts toward the interval and is not double-limited.
            const int frame_ms = frame_cap_ms();
            if (frame_ms > 0) {
                auto now = std::chrono::steady_clock::now();
                if (last_present_.time_since_epoch().count()) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                       now - last_present_).count();
                    int64_t target = (int64_t)frame_ms * 1000;
                    if (elapsed < target) {
                        // Deliberate wait — discount it from THEOC_SLOWLOG, or
                        // every capped frame reports as an 83ms "slow" section
                        // and buries the real ones.
                        slow_credit_ms_ += (double)(target - elapsed) / 1000.0;
                        theoc_sleep_us((uint32_t)(target - elapsed));
                    }
                }
            }
            // Tracked unconditionally, not just when the cap is armed: the
            // async-cursor path uses it to tell a slow scene (worth an extra
            // present) from one already outrunning the 30Hz heartbeat.
            last_present_ = std::chrono::steady_clock::now();
            // A full frame just went out, so any pending cursor rect is on
            // screen already.
            gd_refresh_dirty_ = false;

            // One guest redirect per present (no nested uc_emu_start). Prefer
            // sound when its ~90ms slice is due so the 33ms timer cannot starve
            // the mixer; otherwise fire SIGALRM → TimerSystem::Proc.
            // The console open goes first: it is a one-shot user action, so it
            // can never starve either of the periodic ones.
            if (maybe_redirect_console(m, esp))
                return 0;
            if (maybe_redirect_sound(m, esp))
                return 0;
            if (maybe_redirect_timer(m, esp))
                return 0;
        }
        return 0;
    }
    return 0;
}

void TrapLayer::install_plugins_and_video(Machine& m, uint32_t mvos_base) {
    mvos_base_ = mvos_base;
    machine_ = &m;
    video_.set_event_hook([this](const SDL_Event& e) { on_sdl_event(e); });
    plugin_exports_ = {
        "QueryDevice",
        "CreateVideoDevice",
        "CreateKeyboardDevice",
        "CreateMouseDevice",
        "CreatePointerDevice",
        "Plugin_NoopOK",
        "Plugin_Return0",
        "Plugin_KeyMatrix",
        "Plugin_SetVideoMode",
        "HLE_OpenDisplay",
        "HLE_SwapBuffers",
    };
    plugin_trap_base_ = PLUGIN_TRAP_BASE;
    m.add_code_traps(PLUGIN_TRAP_BASE, (uint32_t)plugin_exports_.size(),
                     [this](Machine& mm, uint32_t slot, uint32_t esp) {
                         return dispatch_plugin(mm, slot, esp);
                     });

    try {
        m.map(GUEST_FB_BASE, GUEST_FB_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    } catch (...) {}

    auto patch_jmp = [&](uint32_t file_va, const char* export_name, const char* tag) {
        uint32_t hle = 0;
        for (uint32_t i = 0; i < plugin_exports_.size(); ++i)
            if (plugin_exports_[i] == export_name) hle = plugin_trap_base_ + i;
        if (!hle) return;
        uint32_t at = mvos_base + file_va;
        uint8_t stub[7] = {
            0xB8,
            (uint8_t)hle, (uint8_t)(hle >> 8), (uint8_t)(hle >> 16), (uint8_t)(hle >> 24),
            0xFF, 0xE0
        };
        m.write(at, stub, sizeof stub);
        std::fprintf(stderr, "  [HLE] patched %s @%#x -> trap %#x\n", tag, at, hle);
    };
    // OpenDisplay: skip plugin SetVideoMode / cGD_X; install cGD_LFB16 ourselves.
    patch_jmp(0x85ce0, "HLE_OpenDisplay", "OpenDisplay");
    // Present only (SwapBuffers__4cVVC). Leave SwapBuffers__Fv intact so it:
    //   MouseRefresh → MoveTo(sprite), BeforeSwapBuffer (paint cSprite on LFB),
    //   VVC present (us), AfterSwapBuffer (restore under-cursor for next frame).
    patch_jmp(0x85e20, "HLE_SwapBuffers", "SwapBuffers__4cVVC");
    // cSprite::AfterSwapBuffer (mvos+0x8b690) — make it single-buffer correct.
    //
    // cSprite keeps TWO saved-background slots, one per buffer:
    //   BeforeSwapBuffer: SaveBg(this, gd, this+0x24); paint at that rect
    //   AfterSwapBuffer:  swap slots {+0x24..+0x38} <-> {+0x0c..+0x20},
    //                     then RestoreBg(this, gd, this+0x24)
    // i.e. it restores the *other* buffer's background, which is right when
    // front and back really are different memory: each buffer's save is taken
    // while that buffer is clean.
    //
    // Our OpenDisplay points every VVC GD slot at one cGD_LFB16 — a single
    // buffer — which breaks that invariant. SaveBg then runs over a buffer that
    // still carries the previous frame's cursor (it is not erased until later
    // in the same frame), captures those pixels into the backup, and re-stamps
    // them every frame after. On a screen that repaints fully each frame the
    // repaint hides it; on static ones (Credits, Load Game) the pointer smears
    // its whole path across the background.
    //
    // Single-buffer correct is save -> paint -> present -> restore the SAME
    // rect, so the buffer is clean again before the next SaveBg. That is this
    // function minus the slot swap. Patch: jump the swap block, and NOP the
    // three stores after it that would otherwise write uninitialised regs.
    //   0x8b69c  swap begins            -> JMP 0x8b6e7 (load VVC, call RestoreBg)
    //   0x8b6ec..0x8b6f4  MOV [EBX+0x10/0x14/0x18], EDI/ESI/ECX  -> NOP
    {
        uint32_t swap_at = mvos_base + 0x8b69c;
        uint32_t resume  = mvos_base + 0x8b6e7;
        int32_t rel = (int32_t)(resume - (swap_at + 5));
        uint8_t jmp[5] = {0xE9, (uint8_t)rel, (uint8_t)(rel >> 8),
                          (uint8_t)(rel >> 16), (uint8_t)(rel >> 24)};
        uint8_t nops[9];
        std::memset(nops, 0x90, sizeof nops);
        if (!std::getenv("THEOC_LEGACY_SPRITE")) {
            m.write(swap_at, jmp, sizeof jmp);
            m.write(mvos_base + 0x8b6ec, nops, sizeof nops);
            std::fprintf(stderr, "  [HLE] cSprite::AfterSwapBuffer -> single-buffer restore "
                        "(no slot swap; THEOC_LEGACY_SPRITE=1 to revert)\n");
        }
    }
    // SDL already injects Intuition ring events; skip PushMouseInput so we do
    // not double-feed type 1/4 from the VMouse ring. MouseRefresh still MoveTo's.
    {
        uint32_t at = mvos_base + 0x8df10;  // PushMouseInput__Fv
        const uint8_t ret = 0xC3;
        m.write(at, &ret, 1);
        std::fprintf(stderr, "  [HLE] nop'd PushMouseInput @%#x (SDL owns Intuition pipe)\n", at);
    }
    // Implement cGD_LFB16::Refresh so the engine's 30Hz between-frame pointer
    // repaints actually reach the screen. See install_gd_refresh().
    install_gd_refresh(m, mvos_base);
}

// THEOC_CONSOLE=1 — open the in-game developer console on demand (Alt+V).
//
// The console was never compiled out. It is fully linked and both cVOConsoles
// are Setup on every realm and province screen; what is missing is a way in.
//
// The shipped opener is InGame_HandleKeyCommand case 0x21, gated on
// g_GameSession+0x2c (the multiplayer battle flag, which single-player always
// clears). Patching that branch works — but only on the *province* screen,
// because InGame_HandleKeyCommand is not a global hotkey handler: it is reached
// from per-widget cVObject key callbacks (vtable+0x10), and only the province
// view's widget class routes Alt+key to it. The realm map view's widget has a
// different handler entirely, so on realm there is no branch to patch. Its
// event drain in RealmGameLoop only deletes type -1 and dispatches nothing.
//
// So we do not patch the game at all. We call the opener ourselves:
//
//   Alt+V (SDL hook)  ->  console_open_pending_
//   next present      ->  guest call Edit__10cVOConsole(g_LogConsole)
//
// which is screen-independent and needs no patch site or byte signature.
//
// Why the *log* console and not g_CmdConsole (the bottom input strip the
// shipped call opens): g_CmdConsole is a dead end. It is never given a cShell
// (both ChangeShell call sites in the whole game pass g_LogConsole), so
// cConsole::Process's null check drops the line; and every command's output
// goes to Print(shell->+0x44, …), which ChangeShell points at g_LogConsole — a
// console nothing ever shows. g_LogConsole is both the shell's owner and its
// print target, so input, echo and output all land in one visible box.
//
// Closing chord is the object's own: SetExitKey(0x0e, mask 2) = Alt+C.
// Collapse the duplicate per-province groups in a .tsg save, right after the
// game closes it.
//
// The defect is the game's, and it is fatal to long campaigns. Every save
// appends a **byte-identical copy** of a small group of records to a list at
// the end of each province's data. Each list stores its length in a *single
// byte*, counting 17-byte units: most provinces add 4 units (68 bytes) per
// save, one (map23) adds 5. So the counter climbs 4-5 per save and dies at 255
// — map23 first, at 51 saves. Past that the byte wraps, the loader reads the
// wrong length, and every byte after it is misparsed. That is the "save
// corrupts after ~50 saves" bug, and the reason hand-deleting the duplicates
// recovers a file: the copies carry nothing.
//
// We fix it at the file boundary rather than by patching the game, because the
// write site is behind two layers of virtual dispatch and a byte-patch into
// logic we do not fully understand is the riskier change. Here the whole file
// is on disk and consistent, and the edit is verifiable against real saves —
// tools/fix_save.py is the same algorithm and is what it was developed against.
//
// **Anchored on the counter, never on the repetition.** Once a list holds many
// identical groups the periodicity also holds at offsets *inside* a group, so
// scanning for the repeat can lock onto a shifted phase and write the counter
// over a data byte. That produces a save that does not load. Instead each byte
// is read as a candidate count and checked against the layout it claims — the
// count fixes the phase exactly.
//
// Refuses to touch anything it does not fully recognise: every province gets
// one group per save, so an intact file has the *same* group count in every
// list. Disagreement means the parse is wrong, and nothing is written.
// THEOC_NO_SAVE_FIX=1 disables the whole thing.
void TrapLayer::collapse_save_file(const std::string& path) {
    static const bool disabled = std::getenv("THEOC_NO_SAVE_FIX") != nullptr;
    if (disabled) return;

    constexpr size_t kUnit = 17;            // four LE u32 + one trailing byte
    static const size_t kGroups[] = {4, 5}; // units appended per save

    std::vector<uint8_t> d;
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n <= 0) { std::fclose(f); return; }
        d.resize((size_t)n);
        size_t got = std::fread(d.data(), 1, d.size(), f);
        std::fclose(f);
        if (got != d.size()) return;
    }

    struct Run { size_t off, group, reps; };
    std::vector<Run> runs;
    for (size_t i = 0; i + 1 < d.size();) {
        size_t count = d[i], hit_g = 0, hit_r = 0;
        for (size_t g : kGroups) {
            if (count < g * 2 || count % g) continue;
            size_t r = count / g, span = g * kUnit, body = i + 1;
            if (body + count * kUnit > d.size()) continue;
            // Reject flat fill: long zero runs repeat trivially and mean nothing.
            std::set<uint8_t> distinct(d.begin() + body, d.begin() + body + span);
            if (distinct.size() < 4) continue;
            bool same = true;
            for (size_t k = 1; k < r && same; ++k)
                same = std::equal(d.begin() + body, d.begin() + body + span,
                                  d.begin() + body + k * span);
            if (same) { hit_g = g; hit_r = r; break; }
        }
        if (hit_g) { runs.push_back({i, hit_g, hit_r}); i += 1 + hit_g * hit_r * kUnit; }
        else       { ++i; }
    }
    if (runs.empty()) return;

    std::set<size_t> reps;
    size_t worst = 0;
    for (const Run& r : runs) { reps.insert(r.reps); worst = std::max(worst, r.group * r.reps); }
    if (reps.size() != 1 || runs.size() < 30 || runs.size() > 60) {
        std::fprintf(stderr, "  [save] %s: not collapsing — %zu lists, %zu distinct "
                     "group counts (expected ~44 and 1). Left untouched.\n",
                     path.c_str(), runs.size(), reps.size());
        return;
    }
    if (*reps.begin() < 2) return;          // already one group each

    std::vector<uint8_t> out;
    out.reserve(d.size());
    size_t prev = 0;
    for (const Run& r : runs) {
        out.insert(out.end(), d.begin() + prev, d.begin() + r.off);
        out.push_back((uint8_t)r.group);    // counter = exactly one group
        out.insert(out.end(), d.begin() + r.off + 1,
                   d.begin() + r.off + 1 + r.group * kUnit);
        prev = r.off + 1 + r.group * r.reps * kUnit;
    }
    out.insert(out.end(), d.begin() + prev, d.end());

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    std::fprintf(stderr, "  [save] collapsed %zu province lists in %s: "
                 "%zu -> %zu bytes, counter %zu -> %zu of 255%s\n",
                 runs.size(), path.c_str(), d.size(), out.size(),
                 worst, *kGroups, ok ? "" : "  (WRITE FAILED)");
}

void TrapLayer::enable_dev_console() {
    console_enabled_ = true;
    std::fprintf(stderr, "  [console] THEOC_CONSOLE: dev console armed "
                "(Alt+V opens, Alt+C closes; realm and province)\n");
}

// THEOC_EDIT=1 — force the game's own edit mode on.
//
// Edit mode is g_GameSession+0x50. The game sets it once, at session
// construction, from LoadGame's editFlag: SetupGame(1) -> edit, SetupGame(2) ->
// normal. Every shipped call site passes normal, so the mode exists in the
// binary but nothing ever selects it.
//
// 58 sites read that flag and none writes it through g_GameSession, so simply
// stamping it is enough — no game code will clear it back.
//
// Re-applied per present rather than once, because the game builds a *new*
// cGameSession on every scenario load and re-initialises +0x50 from the editFlag;
// a one-shot stamp would survive only until the next load. Cost is one read and
// a conditional byte write per frame.
//
// This FREEZES THE SIMULATION: RealmGameLoop steps SimulationUpdate only while
// +0x50 == 0. That is not a side effect to be fixed — a frozen world is what
// edit mode *is*, and it is what makes the console `save` command legal.
void TrapLayer::enable_edit_mode() {
    edit_mode_ = true;
    std::fprintf(stderr, "  [edit] THEOC_EDIT: forcing g_GameSession+0x50 = 1 "
                "(edit mode — the simulation stays frozen)\n");
}

void TrapLayer::apply_edit_mode(Machine& m) {
    if (!edit_mode_) return;
    constexpr uint32_t kGameSession = 0x084c9610;  // cGameSession* (game .bss)
    constexpr uint32_t kEditField   = 0x50;        // cGameSession+0x50 bEditMode
    uint32_t sess = 0;
    try { sess = m.r32(kGameSession); } catch (...) { return; }
    if (!sess) return;                             // no session yet (menus)
    uint8_t v = 0;
    try { m.read(sess + kEditField, &v, 1); } catch (...) { return; }
    if (v) { edit_applied_to_ = sess; return; }    // already set (or we set it)
    v = 1;
    m.write(sess + kEditField, &v, 1);
    if (edit_applied_to_ != sess) {
        edit_applied_to_ = sess;
        std::fprintf(stderr, "  [edit] edit mode on for session %#x "
                    "(sim frozen; console `save` now allowed)\n", sess);
    }
}

// Service a pending Alt+V by rewriting the trap return into a guest call of
// Edit__10cVOConsole(g_LogConsole), the same one-redirect-per-present trick the
// timer and sound slices use (a nested uc_emu_start crashes Unicorn).
//
// Guarded on the console actually having a shell attached: ChangeShell runs at
// realm/province entry and RestoreShell at exit, so +0x38 is non-zero exactly
// while a game screen is live. Outside that window the cConsoleVO at +0x44 is
// stale from a previous screen, and Edit would link a dead widget.
bool TrapLayer::maybe_redirect_console(Machine& m, uint32_t esp) {
    if (!console_open_pending_ || !mvos_base_) return false;
    console_open_pending_ = false;

    constexpr uint32_t kLogConsole = 0x085c0fe0;  // g_LogConsole (game .bss)
    constexpr uint32_t kShellField = 0x38;        // cConsole+0x38 = cShell*
    uint32_t shell = 0;
    try { shell = m.r32(kLogConsole + kShellField); } catch (...) { return false; }
    if (!shell) {
        std::fprintf(stderr, "  [console] Alt+V ignored — no shell attached "
                    "(not in a realm/province game screen)\n");
        return false;
    }

    uint32_t fn  = mvos_base_ + 0x43670;  // Edit__10cVOConsole (libmvos file off)
    uint32_t ret = m.r32(esp);
    uint32_t sp  = esp;
    sp -= 4; m.w32(sp, kLogConsole);      // cdecl arg0 = this
    sp -= 4; m.w32(sp, ret);              // return address
    m.redirect_guest(fn, sp);
    std::fprintf(stderr, "  [console] Alt+V → Edit(g_LogConsole) shell=%#x\n", shell);
    return true;
}
