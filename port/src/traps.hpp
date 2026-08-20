// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
// HLE trap layer: maps each imported symbol (by slot) to a native handler.
// Unimplemented imports log once and return 0 — that log IS the worklist.
// Guest-libmvos path: also hosts FS (fopen/open/…) against $THEOC_DATA and
// tame abort/exit that request_stop() the current Machine::call.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "machine.hpp"
#include "video.hpp"
#include "mpeg.hpp"
#include "cdaudio.hpp"
#include <SDL2/SDL.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

class TrapLayer {
public:
    // names[slot] = imported symbol name in trap-slot order.
    explicit TrapLayer(std::vector<std::string> names);
    ~TrapLayer();

    // TrapFn entry point (bind to Machine::install_traps).
    uint32_t dispatch(Machine& m, uint32_t slot, uint32_t esp);

    // Print the tally of which imports were hit (implemented vs. TODO).
    void report() const;

    // Collapse the duplicate per-province groups in a .tsg save, in place, and
    // normalise the save's uninitialised 72-byte header.
    // Called after the game closes a save it wrote; also reachable standalone
    // via THEOC_FIX_SAVE=<path> so it can be tested and used without a display.
    // Refuses to write anything it does not fully recognise. See the definition
    // for the format and why it is anchored on the counter byte.
    // THEOC_NO_SAVE_FIX=1 disables it on the save path.
    static void collapse_save_file(const std::string& path);

    // Build identity written into save headers: `stamp_date` is YYMMdd and
    // `commit` is the 7-char hash plus its dirty flag. See the definition for
    // the field layout and why it is 22 bytes wide.
    // Handed over from main() rather than compiled in, so that changing the
    // version does not rebuild this (very large) translation unit — see the
    // stamping block in CMakeLists.txt. Call before any save is closed.
    static void set_build_identity(const std::string& stamp_date,
                                   const std::string& commit);

    // Disarm the stall watchdog before the run winds down (see definition).
    void stop_watchdog();

    uint32_t nslots() const { return (uint32_t)names_.size(); }

    Video& video() { return video_; }

    // After guest link: synthetic dlopen plugins + HLE OpenDisplay patch.
    // mvos_base is guestlink::MVOS_BASE. Maps plugin trap window + guest FB.
    void install_plugins_and_video(Machine& m, uint32_t mvos_base);

    // THEOC_PROVINCE_MS — retune the province frame limiter (a *game* patch, so
    // it is separate from the libmvos patches above). cProvince_Do targets
    // 0x14585 µs == 83.3ms == 12fps. Because one cProvince_Do call is one sim
    // step *and* one frame, this moves render rate and simulation speed together
    // — that is what the engine's design allows and all it allows. See
    // docs/porting/frame-timing.md, "Why province stays at 12fps".
    void install_province_rate(Machine& m);

    // THEOC_DUMP_WORLD — read the starting world out of the game's own loader.
    // Passive watches on three game addresses; changes nothing, answers "which
    // heroes and magic items does a given init.dat actually contain".
    // See docs/subsystems/starting-world.md.
    void install_world_dump(Machine& m);

    // THEOC_NEW_WORLD — select the game's own "init mode" instead of "normal
    // mode" at the two game launchers, so the world is generated rather than
    // loaded from init.dat. Recovers the developers' campaign builder.
    void install_world_gen(Machine& m);
    // True once install_world_gen has armed; gates the two helpers below.
    bool world_gen_ = false;

    // hero.cfg and mitem.cfg are the only two config files in the tree that
    // ship as plain text, and cTextFile accepts only `RSA4096`-wrapped files —
    // so the campaign builder cannot read its own inputs. Returns a read-only
    // fd serving an encrypted copy built in memory (anonymous temp file, never
    // a named path), or -1 when the file needs no help. The shipped tree is
    // left exactly as it is, which is the point: players edit the plain text.
    int open_converted_config(const std::string& guest_path);

    // The console's `save` writes <mapdir>/init.dat — straight over the shipped
    // world. Redirects that one write; THEOC_WORLD_OUT names the target,
    // default `init.generated.dat` beside the original. Returns `host`
    // unchanged for everything else.
    std::string redirect_world_out(const std::string& guest,
                                   const std::string& host) const;
    // Prints the block for the last world loaded (nothing else flushes it).
    void flush_world_dump();
    // The dump's accumulator, behind a tiny interface so the concrete type can
    // stay local to install_world_dump. Held by this AND by every watch lambda
    // (which live inside the Machine), so it must be flushed explicitly rather
    // than from a destructor — see flush_world_dump.
    struct WorldDump {
        virtual ~WorldDump() = default;
        virtual void flush() = 0;
        virtual void detach() = 0;   // stop touching guest memory
    };
    std::shared_ptr<WorldDump> world_dump_;

    // Game-space singleton pointers, resolved BY NAME in main.cpp. These are
    // R_386_COPY globals, so the executable's dynamic symbol table names them
    // and `guestlink::abs_sym` can find them in whatever image was booted —
    // which is the point: a hardcoded game address silently points at nothing
    // in a differently built executable. See docs/porting/host-architecture.md.
    void set_game_globals(std::unordered_map<std::string, uint32_t> g) {
        game_globals_ = std::move(g);
    }

    // THEOC_CONSOLE=1: arm the in-game developer console (Alt+V opens it on
    // both the realm and province screens). No game-space patching — the open
    // is a host-driven guest call. See docs/subsystems/dev-console.md.
    void enable_dev_console();

    // THEOC_LONGRUN[=secs]: arm the multi-hour session harness — periodic
    // [health] snapshots and rate-limited logging. See
    // docs/porting/diagnostics.md, "Long-session harness".
    void enable_longrun();

    // THEOC_EDIT=1: force the game's edit mode on (g_GameSession+0x50). Freezes
    // the simulation by design — that is what edit mode is. Enables the console
    // `save` command. See docs/subsystems/dev-console.md#edit-mode.
    void enable_edit_mode();

    // Guest trap address for an imported symbol (TRAP_BASE + slot), or 0 if the
    // symbol isn't imported. Lets the host invoke an import handler directly.
    uint32_t trap_addr(const std::string& name) const {
        for (uint32_t i = 0; i < names_.size(); ++i)
            if (names_[i] == name) return guestmap::TRAP_BASE + i;
        return 0;
    }

    using Handler = std::function<uint32_t(Machine&, uint32_t esp)>;
    // Inject/override a native handler for an imported symbol (used by the MVOS
    // layer). cdecl arg i is at [esp + 4 + 4*i]; return value goes to EAX.
    void register_handler(const std::string& name, Handler fn) {
        table_[name] = std::move(fn);
    }
    static uint32_t arg_at(Machine& m, uint32_t esp, int i) {
        return m.r32(esp + 4 + 4u * i);
    }

    // Reserve guest memory from the same bump arena the guest's malloc uses.
    // Host-side objects planted in guest space MUST come from here — a fixed
    // address inside the arena is handed out again once cumulative allocation
    // reaches it, and the guest then writes over the live object.
    uint32_t guest_alloc(uint32_t size) { return bump_alloc(size); }
    void guest_release(uint32_t p) { guest_free(p); }
    // THEOC_HEAP_TEST=1: randomized alloc/free/realloc workload asserting the
    // allocator never hands out overlapping blocks and reclaims on free.
    // Leaves the arena fragmented, so it runs standalone and exits.
    bool heap_selftest();
    // Frontier = high-water of fresh memory; live = actually held right now.
    uint32_t heap_used() const { return heap_next_ - guestmap::HEAP_BASE; }
    uint32_t heap_live() const { return heap_live_; }

private:

    void register_builtins();
    // convenience: cdecl arg i (0-based), ESP points at return addr on entry.
    static uint32_t arg(Machine& m, uint32_t esp, int i) {
        return m.r32(esp + 4 + 4u * i);
    }
    std::string format(Machine& m, const std::string& fmt, uint32_t esp, int argidx);

    // Guest path "data/…" → host under $THEOC_DATA (default data/game).
    std::string resolve_path(const std::string& guest) const;
    void set_errno(Machine& m, int err);
    // Socket plumbing: sockets share the fd table with files (the guest tells us
    // which is which by calling send/recv vs read/write).
    int host_fd_of(int gfd);
    int adopt_host_fd(int hfd, bool is_socket = false);

    // name -> game-space address of the copy-reloc'd singleton pointer.
    std::unordered_map<std::string, uint32_t> game_globals_;
    uint32_t game_glob(const char* n) const {
        auto it = game_globals_.find(n);
        return it == game_globals_.end() ? 0 : it->second;
    }

    std::vector<std::string> names_;
    std::vector<uint64_t>    hits_;
    std::unordered_map<std::string, Handler> table_;

    // Guest heap over guestmap::HEAP_BASE: a bump frontier for fresh memory
    // plus a coalescing free list so freed blocks are genuinely reused.
    // free() used to be a no-op, which made every scenario load leak ~50 MB —
    // two loads in one session exhausted the 128 MB arena and malloc returned
    // 0, which the guest then wrote through (see G15).
    uint32_t heap_next_;
    uint32_t heap_live_ = 0;                        // bytes currently allocated
    std::unordered_map<uint32_t, uint32_t> alloc_sz_;  // live block -> block size
    std::map<uint32_t, uint32_t> free_addr_;        // addr -> size (coalescing)
    std::multimap<uint32_t, uint32_t> free_size_;   // size -> addr (best fit)
    void fl_insert(uint32_t addr, uint32_t size);
    void fl_erase(uint32_t addr, uint32_t size);
    void guest_free(uint32_t p);
    uint32_t bump_alloc(uint32_t size);
    // Stall watchdog (THEOC_WATCHDOG=secs, default 10 when set to 1). A host
    // thread that reports when presents stop, and — crucially — whether the
    // emulator is still executing guest code. blocks climbing = the guest is
    // spinning in a loop (the reported EIP is that loop); blocks frozen = we
    // are wedged host-side, in the last-named trap. Freezes are hard to catch
    // interactively, so this turns "it hung" into an address.
    // THEOC_SLOWLOG=ms (default 250): report any host-side section that blocks
    // the emulation thread for longer than the threshold. The stall watchdog
    // says *that* we are stuck host-side; this says *which handler*.
    static double slowlog_ms();
    // Wall-clock we intentionally slept (frame cap), excluded from [slow].
    double slow_credit_ms_ = 0;
    struct SlowSection {
        TrapLayer* self;
        const char* what;
        std::chrono::steady_clock::time_point t0;
        double credit0 = 0;
        SlowSection(TrapLayer* s, const char* w) : self(s), what(w) {
            if (slowlog_ms() > 0) {
                t0 = std::chrono::steady_clock::now();
                credit0 = s->slow_credit_ms_;
            }
        }
        ~SlowSection() {
            double lim = slowlog_ms();
            if (lim <= 0) return;
            double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - t0).count();
            ms -= self->slow_credit_ms_ - credit0;   // minus deliberate waits
            // Rate-limited: one wedged handler would otherwise emit a [slow]
            // line every frame for hours.
            if (ms >= lim && self->rl_allow(what, 5, std::chrono::seconds(60)))
                std::fprintf(stderr, "  [slow] %s took %.0f ms\n", what, ms);
        }
    };
    // THEOC_AUTO_KEYS=1: unattended SPACE taps through the SDL event path.
    void auto_keys_tick();
    // THEOC_SOAK=cycles: drive menu → campaign → province → map → quit → menu
    // repeatedly, snapshotting resource use at the same point each cycle.
    // THEOC_SOAK_PLAY=sec controls the province dwell (default 20).
    void soak_tick();
    // THEOC_CLICKS / THEOC_MOUSE_SWEEP / THEOC_SHOT_EVERY: drive to a screen,
    // sweep the pointer and capture frames (render-bug harness).
    void render_probe_tick();
    // Frame capture only (THEOC_SHOT_EVERY). Separate from render_probe_tick so
    // the cutscene present path can drive it without also driving click/sweep.
    void shot_tick();
    bool soak_click_step(int x, int y);
    void soak_snapshot(const char* tag);
    uint32_t active_screen() const;
    std::chrono::steady_clock::time_point soak_t0_{}, soak_step_started_{};
    int soak_click_phase_ = 0, soak_click_frames_ = 0;
    int  soak_cycle_ = 0, soak_step_ = 0;
    bool soak_clicked_ = false, soak_done_ = false;
    uint32_t soak_screen_before_ = 0;
    void start_watchdog(Machine& m);
    void watchdog_loop(double stall_sec);
    std::thread wd_thread_;
    std::atomic<bool> wd_stop_{false};
    Machine* wd_m_ = nullptr;
    std::chrono::steady_clock::time_point wd_t0_;
    std::atomic<uint64_t> present_seq_{0};
    std::atomic<uint64_t> trap_seq_{0};
    std::atomic<const char*> last_trap_{nullptr};
    // True while an SMPEG cutscene is on screen: gates the key mailbox below,
    // so it is only ever non-empty during a movie.
    bool movie_playing_ = false;
    // Alt+Enter consumed a Return-down; swallow its release too so the guest key
    // matrix never sees an unpaired release (the G16 stale-key-state class).
    bool fs_toggle_swallow_ = false;
    // 8-byte guest mailbox {int keycode; int flags} drained by the VKeyboard
    // driver-table slot +0x0c stub. Host posts a key-down here; the cutscene
    // loop in External_PlayAnim polls that slot to decide whether to skip.
    uint32_t key_mailbox_ = 0;
    // RX stub page for guest-callable driver methods (heap is RW only).
    uint32_t stub_next_ = 0;
    uint32_t stub_alloc(Machine& m, uint32_t size);

    // Host-side FILE* / fd table, keyed by the guest FILE* (or guest fd for open).
    struct HostFile {
        FILE* fp = nullptr;
        int   host_fd = -1;
        bool  stub = false;     // /dev/* that we fake
        bool  audio = false;    // /dev/dsp → host SDL mixer
        bool  eof = false;
        std::string host_path;  // resolved path, for post-close normalisation
        bool  wrote = false;    // opened for writing (see collapse_save_file)
        // Is host_fd a socket rather than a file descriptor? On POSIX this is
        // merely informative — one namespace, and read/write/close work on both.
        // On Windows it is *required*: Winsock SOCKETs are a separate namespace
        // from CRT fds, so ::read/::write/::close on one are silently wrong and
        // must become recv/send/closesocket. libmvos makes this unavoidable —
        // cIPCO_TCPIP::Read/Write poll the socket through plain read/write.
        bool  sock = false;
        // Remembered O_NONBLOCK state. Windows' ioctlsocket(FIONBIO) is
        // write-only — there is no F_GETFL — so fcntl(F_GETFL) is answered from
        // here. Exact, because the fcntl handler is its only writer.
        bool  nonblock = false;
        // /dev/cdrom → VirtualCD (docs/subsystems/music-and-redbook.md). Last on
        // purpose: several call sites brace-initialise this struct positionally,
        // so a field inserted mid-struct silently reassigns them.
        bool  cdrom = false;
    };
    std::unordered_map<uint32_t, HostFile> files_;   // guest FILE*
    std::unordered_map<int, HostFile> fds_;          // guest fd → host
    int next_fd_ = 3;
    uint32_t hostent_buf_ = 0;   // reusable guest `struct hostent` (see gethostbyname)
    // opendir/readdir: guest DIR* handle -> host DIR* plus a reusable guest
    // `struct dirent` (readdir returns a pointer to static storage, per contract).
    // `path` is the resolved host directory. Only Windows needs it — mingw's
    // struct dirent has no d_type, so readdir has to stat each entry to fill the
    // byte cDirent reads — but it is stored unconditionally rather than behind an
    // #ifdef, because a field that exists on one platform only is how struct
    // layouts quietly diverge.
    struct HostDir { void* d = nullptr; uint32_t ent = 0; std::string path; };
    std::unordered_map<uint32_t, HostDir> dirs_;
    uint32_t next_dir_ = 0x44495200;   // 'DIR\0' — distinctive in a fault dump
    std::string data_root_;
    Video video_;
    std::string smpeg_error_;   // last SMPEG_error message (host)
    MpegStore mpeg_;
    // SDL audio: OSS /dev/dsp writes are mixed into a ring and played out.
    SDL_AudioDeviceID audio_dev_ = 0;
    std::mutex audio_mu_;
    std::deque<int16_t> audio_q_;
    void ensure_audio();
    void audio_push(const void* data, size_t nbytes);
    static void audio_callback(void* userdata, Uint8* stream, int len);
    // Redbook CD audio (music). The guest's cCD_Linux runs unmodified and talks
    // to this through the seven CDROM ioctls — see docs/subsystems/
    // music-and-redbook.md. Empty unless a rip was found, in which case every CD
    // ioctl keeps the old blanket-success behaviour.
    VirtualCD cd_;
    bool cd_trace_ = false;
    uint32_t cd_ioctl(Machine& m, int gfd, uint32_t req, uint32_t argp);
    bool maybe_redirect_cd_advance(Machine& m, uint32_t esp);
    std::chrono::steady_clock::time_point cd_next_advance_{};
    // Green-thread stand-ins for pthread_create (sound mixer Main loop).
    // Each present redirects guest into Entry once (Main patched to one-shot).
    struct SoftThread {
        uint32_t entry = 0;
        uint32_t arg = 0;  // cThread*
    };
    std::vector<SoftThread> soft_threads_;
    bool sound_main_patched_ = false;
    bool redirecting_sound_ = false;
    // THEOC_CONSOLE: Alt+V is captured in the SDL hook and serviced at the next
    // present, because guest code cannot be called from an SDL callback.
    bool console_enabled_ = false;
    bool console_open_pending_ = false;
    bool console_key_swallow_ = false;
    bool maybe_redirect_console(Machine& m, uint32_t esp);
    // ---- long-session harness (THEOC_LONGRUN) --------------------------------
    // A multi-hour run has two failure modes a normal session does not: the log
    // grows without bound, and a fault arrives hours after the state that caused
    // it. So: rate-limit anything repeatable, and emit a periodic one-line
    // snapshot dense enough that the log alone explains a fault after the fact.
    bool longrun_ = false;
    std::chrono::seconds longrun_every_{60};
    std::chrono::steady_clock::time_point longrun_t0_{}, longrun_last_{};
    uint64_t longrun_frames_ = 0;          // frames since the last [health]
    uint64_t longrun_frames_total_ = 0;
    // Growth is measured on the LIVE set, not the frontier. The frontier is a
    // high-water mark: since the G15 allocator rewrite gave free() real
    // reclamation it stops moving as soon as freed blocks are reused, so a
    // frontier-based rate reads 0.000 through a session that is leaking. It is
    // still tracked, because headroom against the 128 MB arena is a frontier
    // question — but it is reported as a level, never as the leak signal.
    uint32_t longrun_live_base_ = 0;    // live set at the last [health]
    uint32_t longrun_live_start_ = 0;   // live set when the harness armed
    uint32_t longrun_heap_start_ = 0;   // frontier when the harness armed
    size_t   longrun_rss_base_ = 0;
    uint64_t longrun_underrun_base_ = 0;
    // Guest work per interval. Without it a slow sample is unattributable: 11
    // fps with the blocks/frame of a normal frame is the host falling behind,
    // the same 11 fps at three times the blocks/frame is the guest doing three
    // times the work (a crowded battle). Base is taken on the first tick, not in
    // enable_longrun(), which has no Machine to ask.
    uint64_t longrun_blocks_base_ = 0;
    bool     longrun_blocks_init_ = false;
    // Alt+M during a long run stamps a numbered marker into the log and forces
    // the [health] sample out early, so the interval boundary lands on the event
    // rather than wherever the timer happened to be. Same swallow discipline as
    // Alt+Enter / Alt+V: 'M' is a live game key.
    bool     mark_pending_ = false;
    bool     mark_key_swallow_ = false;
    uint32_t mark_seq_ = 0;
    void longrun_tick(Machine& m);
    static int frame_cap_ms();          // THEOC_FRAME_MS, read once

    // Rate limiter: allow `burst` messages per key, then at most one per
    // `interval`, counting what was dropped. Suppression totals surface in the
    // [health] line, so a silenced spammer is still visible as a number.
    struct RateLimit {
        uint32_t seen = 0, emitted = 0, suppressed = 0;
        std::chrono::steady_clock::time_point last{};
    };
    std::unordered_map<std::string, RateLimit> rl_;
    uint64_t rl_suppressed_total_ = 0;
public:
    // Returns true if this message should be printed. Cheap no-op when the
    // harness is off, so call sites can use it unconditionally.
    bool rl_allow(const char* key, uint32_t burst = 5,
                  std::chrono::seconds interval = std::chrono::seconds(30));
private:

    // THEOC_EDIT: re-applied per present, because the game builds a *new*
    // cGameSession on every load and writes the flag from LoadGame's editFlag.
    bool edit_mode_ = false;
    uint32_t edit_applied_to_ = 0;   // session we last stamped (log once each)
    void apply_edit_mode(Machine& m);
    std::chrono::steady_clock::time_point next_sound_slice_{};
    void patch_sound_main_oneshot(Machine& m);
    // If a soft thread needs a slice, rewrite trap return into Entry(arg).
    // Returns true if redirect_guest was used (caller must return 0 raw).
    bool maybe_redirect_sound(Machine& m, uint32_t esp);

    // Synthetic dlopen handles and dlsym names → guest trap addresses.
    std::unordered_map<uint32_t, std::string> dl_handles_;  // handle → so name
    uint32_t next_dl_handle_ = 0xD1000001;
    std::string last_dlerror_;
    uint32_t plugin_trap_base_ = 0;
    uint32_t mvos_base_ = 0;           // set in install_plugins_and_video
    uint32_t gd_ = 0;                  // last constructed cGD_LFB16
    Machine* machine_ = nullptr;       // for input injection during pump
    uint8_t mouse_buttons_ = 0;        // current button mask (bit0 L, bit1 R, bit2 M)
    int mouse_x_ = 0, mouse_y_ = 0;
    std::vector<std::string> plugin_exports_;  // slot → name
    uint32_t dispatch_plugin(Machine& m, uint32_t slot, uint32_t esp);
    uint32_t make_device(Machine& m, const char* kind);
    void on_sdl_event(const SDL_Event& e);
    // Host-side cMouse/cPointer EVENT_* (queue format from libmvos).
    void mouse_event_move(uint32_t dev, int32_t x, int32_t y);
    void mouse_event_buttons(uint32_t dev, uint8_t buttons);
    void update_intuition_pointer(int x, int y, uint8_t buttons);
    // Intuition+0x28 input ring (8-byte events) — what ProcessInputs drains.
    // Still written from SDL as a belt-and-braces path; guest MouseRefresh
    // (via real SwapBuffers__Fv) also fills it from VMouse.
    uint32_t intuition_obj() const;
    void push_intuition_event(uint32_t type, uint32_t payload);
    void push_intuition_move(int x, int y);
    void push_intuition_button_edges(uint8_t prev, uint8_t now);
    // Fallback click anim if timer not armed; primary path is guest TimerProc.
    void tick_pointer_click_anim();
    uint32_t pointer_sprite() const;  // Intuition→screen→sprite, or 0

    // setitimer(ITIMER_REAL) / SIGALRM — host-polled, fires guest TimerSystem::Proc.
    // (No real signals into Unicorn; we tick from present.)
    uint32_t sigalrm_handler_ = 0;  // guest sa_handler (_TimerFunction)
    bool     timer_armed_ = false;
    std::chrono::steady_clock::time_point timer_next_{};
    std::chrono::microseconds timer_interval_{0};
    std::chrono::microseconds timer_value_{0};  // last it_value (for get-old)
    // If due, redirect trap return → _TimerFunction (no nested uc_emu_start).
    // Returns true if redirect_guest was used.
    bool maybe_redirect_timer(Machine& m, uint32_t esp);
    // Collapse the tick schedule up to now; returns intervals skipped.
    int  advance_timer_schedule();

    // --- Re-entrant usleep ---------------------------------------------------
    // A real kernel delivers SIGALRM *during* a long sleep without shortening
    // it. We used to truncate instead — return from usleep at the first due
    // tick — which cut the game's own 83ms frame limiter to ~33ms and made the
    // province sim run ~2.5x fast (frame-timing.md, Bug 2). Now the usleep
    // handler splices _TimerFunction with its return address pointing back at
    // the usleep trap and keeps the unslept remainder here, so the sleep
    // resumes after the tick and the guest sees its full duration.
    uint32_t sleep_remaining_us_ = 0;  // unslept remainder, 0 = not mid-sleep
    uint32_t sleep_resume_ret_   = 0;  // original caller's return address
    bool     sleep_resuming_     = false;
    // True when the SIGALRM handler is the stock _TimerFunction, which ignores
    // its signo argument. The re-entry frame necessarily aliases signo onto the
    // return address (see redirect_timer_reentrant), so a custom handler that
    // reads signo must take the old truncating path instead.
    bool timer_handler_ignores_signo() const;
    bool redirect_timer_reentrant(Machine& m, uint32_t esp, uint32_t remaining);

    // --- Asynchronous cursor refresh (cGD_LFB16::Refresh) --------------------
    // The engine repaints the pointer *between* frames: cIntuition::TimerProc
    // runs at 30Hz and, when the GD reports IsAsyncRefreshCapable(), calls
    // MouseRefresh + cSprite::Refresh — which flushes the touched rectangle via
    // the GD vtable slot +0x14, cGD_LFB16::Refresh(const cRectangle&).
    //
    // On the original that flush is genuinely free: cGD_LFB16 is the *linear
    // framebuffer* GD, so writing to it is writing to the display, and both
    // IsAsyncRefreshCapable() = 1 and Refresh() = {} are correct. Our LFB is not
    // a display — it is a staging buffer copied to SDL only at present — so we
    // inherited a no-op that silently dropped every between-frame cursor update,
    // pinning the pointer to the scene's frame rate (12fps in province).
    //
    // We implement the method instead of patching anything: an entry-point
    // override marks the region dirty, and the present happens at the next safe
    // point. It is deliberately NOT presented from inside the override, because
    // cSprite::Refresh calls Refresh twice — once after RestoreBg erases the old
    // pointer, once after painting the new one — and presenting on the first
    // would show the erased state as its own frame.
    bool gd_refresh_dirty_ = false;
    void install_gd_refresh(Machine& m, uint32_t mvos_base);
    // Copy the guest LFB to SDL and present. Just the pixels — none of the
    // per-frame bookkeeping HLE_SwapBuffers does (fps/soak/auto-keys/edit mode),
    // because this is an out-of-band cursor update, not a frame.
    void present_async_cursor(Machine& m);

    // --- THEOC_FPS frame instrument (throughput-vs-timing diagnostic) ---------
    // Reports per second: FPS (presents), guest-work/frame (blocks), guest
    // blocks/sec (throughput ceiling → saturation check), and heartbeat/mixer
    // redirect rates. Distinguishes CPU-bound (low FPS + constant blocks/sec)
    // from timing-bound (low FPS + blocks/sec drops → emulator idling).
    void fps_tick(Machine& m);
    bool     fps_on_ = false;
    bool     fps_init_ = false;
    std::chrono::steady_clock::time_point fps_last_{};
    int      fps_frames_ = 0;
    uint64_t fps_blocks_base_ = 0;
    uint32_t fps_heap_base_ = guestmap::HEAP_BASE;  // heap growth per interval
    int      fps_timer_fires_ = 0;
    int      fps_sound_fires_ = 0;
    uint64_t fps_usleep_us_ = 0;       // host µs *requested* in usleep() this window
    int      fps_usleep_calls_ = 0;

    // Sleep-slice accounting. The frame model sleeps in tick-bounded slices, so
    // slices/frame — not fps — is the number that says whether the host's sleep
    // primitive is keeping up, and the requested-vs-elapsed gap is that
    // primitive's own floor. Measured rather than hand-derived from the line
    // above, because on Windows exactly that arithmetic was the whole residual.
    // See docs/porting/other-os-ports.md, "the fix lands, with a residual".
    void     sleep_accounted(uint32_t us);
    uint64_t fps_sleep_act_us_ = 0;    // µs actually elapsed in those sleeps
    int      fps_sleep_slices_ = 0;    // individual host sleeps this window
    int      fps_gettime_calls_ = 0;
    int      fps_select_calls_ = 0;

    // THEOC_AUTO_PROVINCE self-driver (unattended timing tests).
    std::chrono::steady_clock::time_point auto_prov_t0_{};
    int      auto_prov_stage_ = 0;

    uint64_t audio_underrun_ = 0;      // callback samples with empty queue (stutter)

    // Frame-rate cap (THEOC_FRAME_MS): the engine steps physics/animation once
    // per rendered frame, so uncapped render speed = turbo sim. Clamp the minimum
    // present interval to the design cadence so frame-tied sim runs at intended
    // speed. Measured present-to-present, so it only slows frames already faster
    // than target — the game's own usleep pacing is never double-counted.
    std::chrono::steady_clock::time_point last_present_{};
};
