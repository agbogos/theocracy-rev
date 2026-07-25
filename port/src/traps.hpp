// HLE trap layer: maps each imported symbol (by slot) to a native handler.
// Unimplemented imports log once and return 0 — that log IS the worklist.
// Guest-libmvos path: also hosts FS (fopen/open/…) against $THEOC_DATA and
// tame abort/exit that request_stop() the current Machine::call.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "machine.hpp"
#include "video.hpp"
#include "mpeg.hpp"
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

    // Disarm the stall watchdog before the run winds down (see definition).
    void stop_watchdog();

    uint32_t nslots() const { return (uint32_t)names_.size(); }

    Video& video() { return video_; }

    // After guest link: synthetic dlopen plugins + HLE OpenDisplay patch.
    // mvos_base is guestlink::MVOS_BASE. Maps plugin trap window + guest FB.
    void install_plugins_and_video(Machine& m, uint32_t mvos_base);

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
            if (ms >= lim)
                std::fprintf(stderr, "  [slow] %s took %.0f ms\n", what, ms);
        }
    };
    // THEOC_AUTO_KEYS=1: unattended SPACE taps through the SDL event path.
    void auto_keys_tick();
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
    };
    std::unordered_map<uint32_t, HostFile> files_;   // guest FILE*
    std::unordered_map<int, HostFile> fds_;          // guest fd → host
    int next_fd_ = 3;
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
    // Green-thread stand-ins for pthread_create (sound mixer Main loop).
    // Each present redirects guest into Entry once (Main patched to one-shot).
    struct SoftThread {
        uint32_t entry = 0;
        uint32_t arg = 0;  // cThread*
    };
    std::vector<SoftThread> soft_threads_;
    bool sound_main_patched_ = false;
    bool redirecting_sound_ = false;
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
    void draw_software_cursor();  // fallback if no game pointer sprite
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
    uint64_t fps_usleep_us_ = 0;       // host µs slept in usleep() this window
    int      fps_usleep_calls_ = 0;
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
