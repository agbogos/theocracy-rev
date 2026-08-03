// Windows sleep-granularity probe — the port's #1 risk, measured rather than argued.
//
// WHY THIS EXISTS
// ---------------
// docs/porting/other-os-ports.md ranks "timing precision" as the biggest
// technical risk of a Windows host, on the grounds that Windows' default
// scheduler granularity is ~15.6 ms without timeBeginPeriod. That is a
// plausible-sounding argument, and this project has been burned by exactly one
// of those before: the claim that the BSD→Linux errno translations would be
// "actively wrong" on Linux survived until someone compiled a one-file probe,
// at which point it turned out to be backwards. The lesson written down at the
// time was "when a claim is about observable state, observe it". This is that
// probe, for the Windows claim, and it runs before traps.cpp is touched.
//
// WHAT THE PORT ACTUALLY ASKS FOR — and why the naive reading understates it
// -------------------------------------------------------------------------
// The interesting number is NOT the game's 83.3 ms province frame. Read the
// usleep handler in port/src/traps.cpp: it does not sleep 83 ms. It sleeps in
// slices bounded by the next 30 Hz heartbeat tick, delivering a tick at each
// boundary the way SIGALRM-interrupted usleep does on Linux:
//
//     for (;;) {
//         if (tick due) { deliver it; }
//         slice = min(remaining, time_until_next_tick);   // <-- the real request
//         if (!slice) continue;
//         usleep(slice);
//         remaining -= slice;
//     }
//
// So the requested duration is "however long until the next tick", which is
// uniform over (0, 33.3 ms] — it is routinely SUB-MILLISECOND, on every frame,
// as the tail slice before a tick comes due. A 15.6 ms floor does not merely
// add jitter to those; it overshoots them by 15x-150x and blows straight past
// the deadline the slice existed to stop at. That is the failure mode to look
// for, and it is why the sweep below spends most of its rows under 2 ms.
//
// The three tests answer three escalating questions:
//   1. sweep      — what does one sleep of length N actually cost?
//   2. heartbeat  — can a 30 Hz tick be held over 5 s of consecutive sleeps?
//   3. frame      — does the real slice pattern deliver ~3.5 ticks per 83 ms
//                   frame, which is what frame-timing.md says the model needs?
//
// BUILD (cross, from macOS — no Windows involved):
//   brew install mingw-w64
//   x86_64-w64-mingw32-g++ -O2 -std=c++17 -static -Wall -Wextra
//       -o win-timing-probe.exe tools/win_timing_probe.cpp -lwinmm
//
// (one line; split here only for width — a trailing backslash inside a // comment
//  splices the next line into it, which -Wcomment quite rightly complains about)
//
// RUN (on the target; no arguments needed):
//   win-timing-probe.exe [--seconds N] [--busy N]
//
//   --busy N spawns N spinning threads first. Use it: the multiplayer session
//   that prompted this runs three game instances on one host, and scheduler
//   granularity behaves differently under contention than on an idle box.
//
// SCOPE, stated so the numbers are not over-read: this measures the host sleep
// primitive in isolation, on whatever machine it is run on. It does not run any
// guest code and says nothing about Unicorn's throughput. Run it on the VM AND
// on bare metal — VM timer behaviour is not the host's, and knowing that gap is
// a prerequisite for trusting any later measurement taken in a VM.

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

// Win10 1803+. Older mingw headers may not carry it; the value is stable ABI.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// ---------------------------------------------------------------- clock ----

static double g_qpc_freq = 0.0;

static inline double now_ms() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / g_qpc_freq;
}

// --------------------------------------------------------------- sleeps ----
//
// The four candidate implementations of "sleep for us microseconds". These are
// exactly the choices the Windows port would have to pick between, so the probe
// measures the decision rather than a proxy for it.

enum Method { M_SLEEP, M_SLEEP_TBP, M_TIMER, M_TIMER_TBP, M_COUNT };

static const char* method_name(int m) {
    switch (m) {
        case M_SLEEP:     return "Sleep()";
        case M_SLEEP_TBP: return "Sleep() + timeBeginPeriod(1)";
        case M_TIMER:     return "waitable timer (HIGH_RESOLUTION)";
        case M_TIMER_TBP: return "waitable timer + timeBeginPeriod(1)";
    }
    return "?";
}

static HANDLE g_timer = NULL;       // high-resolution waitable timer, if available
static bool   g_timer_is_hires = false;

static void sleep_us(int method, unsigned us) {
    if (method == M_SLEEP || method == M_SLEEP_TBP) {
        // Sleep() takes whole milliseconds and cannot express sub-ms at all.
        // Round UP: Sleep(0) means "yield the rest of the quantum", which is a
        // different operation and would flatter the numbers dishonestly.
        DWORD ms = (DWORD)((us + 999) / 1000);
        if (ms == 0) ms = 1;
        Sleep(ms);
        return;
    }
    // Relative due time, in negative 100 ns units.
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)us * 10;
    SetWaitableTimer(g_timer, &due, 0, NULL, NULL, FALSE);
    WaitForSingleObject(g_timer, INFINITE);
}

static bool method_uses_tbp(int m) { return m == M_SLEEP_TBP || m == M_TIMER_TBP; }

// ------------------------------------------------------------ statistics ----

struct Stats { double min, med, p95, max, mean; };

static Stats summarize(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    Stats s{};
    s.min = v.front();
    s.max = v.back();
    s.med = v[(size_t)(0.50 * (v.size() - 1))];
    s.p95 = v[(size_t)(0.95 * (v.size() - 1))];
    double sum = 0;
    for (double d : v) sum += d;
    s.mean = sum / v.size();
    return s;
}

// ------------------------------------------------------- test 1: sweep ----
//
// One sleep of each length, many times, per method. The durations are chosen
// from the handler above: the sub-ms rows are the tail slices before a tick,
// 16.7/33.3 are whole heartbeat intervals, 83.3 is the province frame the
// slices add up to.

static const unsigned kSweep[] = {
    100, 250, 500, 1000, 2000, 5000, 10000, 16667, 33333, 83333
};

static void test_sweep(int method) {
    printf("\n  %-36s  %10s %8s %8s %8s %8s\n",
           method_name(method), "requested", "min", "median", "p95", "max");
    printf("  %-36s  %10s %8s %8s %8s %8s\n",
           "", "(ms)", "(ms)", "(ms)", "(ms)", "(ms)");

    for (unsigned us : kSweep) {
        // Keep each row near ~1 s of wall clock: short sleeps get more samples.
        int iters = (int)(1500000u / us);
        if (iters < 20)  iters = 20;
        if (iters > 300) iters = 300;

        std::vector<double> samples;
        samples.reserve(iters);
        for (int i = 0; i < iters; i++) {
            double t0 = now_ms();
            sleep_us(method, us);
            samples.push_back(now_ms() - t0);
        }
        Stats s = summarize(samples);

        // Flag rows where the median overshoot is worse than half the 33.3 ms
        // heartbeat interval: at that point the slice cannot do its job.
        const char* flag = (s.med - us / 1000.0) > 16.0 ? "  <-- overshoots the tick" : "";
        printf("  %-36s  %10.3f %8.3f %8.3f %8.3f %8.3f%s\n",
               "", us / 1000.0, s.min, s.med, s.p95, s.max, flag);
    }
}

// --------------------------------------------------- test 2: heartbeat ----
//
// Hold a 30 Hz tick across consecutive sleeps for N seconds. This is the test
// that matters most: single-shot jitter can average out, but the port's timer
// schedule is absolute (timer_next_ advances by a fixed interval), so a sleep
// primitive that consistently overshoots accumulates lateness instead.

static void test_heartbeat(int method, double seconds) {
    const double period = 1000.0 / 30.0;   // 33.333 ms

    double start = now_ms();
    double deadline = start + period;
    std::vector<double> lateness;
    int ticks = 0;

    while (now_ms() - start < seconds * 1000.0) {
        double t = now_ms();
        double wait = deadline - t;
        if (wait > 0) sleep_us(method, (unsigned)(wait * 1000.0));
        lateness.push_back(now_ms() - deadline);
        ticks++;
        deadline += period;
    }

    Stats s = summarize(lateness);
    double achieved = ticks / ((now_ms() - start) / 1000.0);
    printf("  %-36s  %6.2f Hz | late med %7.3f  p95 %7.3f  max %8.3f ms\n",
           method_name(method), achieved, s.med, s.p95, s.max);
}

// ------------------------------------------------- test 3: province frame ----
//
// The real pattern, transliterated from the usleep handler: an 83.333 ms frame
// slept as a sequence of tick-bounded slices. frame-timing.md says the model
// needs ~3.5 tick deliveries per frame; if this reports ~1, the heartbeat has
// collapsed back into the frame rate, which is the exact defect the re-entrant
// sleep was built to fix.

static void test_frame(int method, int frames) {
    const double tick   = 1000.0 / 30.0;   // heartbeat interval
    const double budget = 83.3333;         // cProvince_Do's limiter

    std::vector<double> durations;
    int total_ticks = 0;
    double next_tick = now_ms() + tick;

    for (int f = 0; f < frames; f++) {
        double t0 = now_ms();
        double remaining = budget;

        while (remaining > 0) {
            double t = now_ms();
            if (next_tick <= t) {          // a tick is due: "deliver" it
                total_ticks++;
                next_tick += tick;
                continue;
            }
            double slice = remaining;
            double until = next_tick - t;
            if (until < slice) slice = until;
            if (slice <= 0) continue;
            sleep_us(method, (unsigned)(slice * 1000.0));
            remaining -= (now_ms() - t);   // charge real elapsed, as the handler does
        }
        durations.push_back(now_ms() - t0);
    }

    Stats s = summarize(durations);
    printf("  %-36s  frame med %7.3f ms (target %.3f) | %.2f ticks/frame\n",
           method_name(method), s.med, budget, (double)total_ticks / frames);
}

// Sink for the --busy threads, so their spin loop cannot be optimised away.
static std::atomic<double> g_busy_sink{0.0};

// ------------------------------------------------------------ environment ----

static void report_environment() {
    printf("Environment\n");

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    printf("  QueryPerformanceCounter frequency : %.3f MHz\n", (double)f.QuadPart / 1e6);

    TIMECAPS tc;
    if (timeGetDevCaps(&tc, sizeof(tc)) == MMSYSERR_NOERROR)
        printf("  timeGetDevCaps period range       : %u .. %u ms\n",
               (unsigned)tc.wPeriodMin, (unsigned)tc.wPeriodMax);
    else
        printf("  timeGetDevCaps                    : FAILED\n");

    // The actual current timer resolution, which is what the scheduler uses.
    // Undocumented but stable since NT 3.1; units are 100 ns.
    typedef LONG (NTAPI *PFN_NtQueryTimerResolution)(PULONG, PULONG, PULONG);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (nt) {
        auto q = (PFN_NtQueryTimerResolution)(void*)GetProcAddress(nt, "NtQueryTimerResolution");
        if (q) {
            ULONG mn = 0, mx = 0, cur = 0;
            if (q(&mn, &mx, &cur) == 0)
                printf("  NtQueryTimerResolution current    : %.4f ms "
                       "(range %.4f .. %.4f)\n",
                       cur / 10000.0, mx / 10000.0, mn / 10000.0);
        }
    }

    printf("  high-resolution waitable timer    : %s\n",
           g_timer_is_hires ? "yes"
                            : "NO - fell back to a normal timer (pre-1803?)");
    printf("  logical processors                : %u\n",
           (unsigned)std::thread::hardware_concurrency());
}

// ------------------------------------------------------------------ main ----

int main(int argc, char** argv) {
    double seconds = 5.0;
    int busy = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc)   seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--busy") && i + 1 < argc) busy = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--seconds N] [--busy N]\n", argv[0]);
            return 2;
        }
    }

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpc_freq = (double)f.QuadPart;

    // Prefer the high-resolution timer; record whether we actually got one,
    // because silently falling back would make the last two methods a lie.
    g_timer = CreateWaitableTimerExW(NULL, NULL,
                                     CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS);
    if (g_timer) {
        g_timer_is_hires = true;
    } else {
        g_timer = CreateWaitableTimerW(NULL, FALSE, NULL);
        g_timer_is_hires = false;
    }
    if (!g_timer) {
        fprintf(stderr, "CreateWaitableTimer failed (%lu)\n", GetLastError());
        return 1;
    }

    printf("Theocracy port - Windows sleep-granularity probe\n");
    printf("================================================\n\n");
    report_environment();

    std::atomic<bool> stop_busy{false};
    std::vector<std::thread> load;
    if (busy > 0) {
        printf("\n  load: %d spinning thread(s) for the duration of this run\n", busy);
        for (int i = 0; i < busy; i++)
            load.emplace_back([&stop_busy] {
                // The store at the end is what keeps this loop alive: without a
                // visible use of x, the optimiser is free to delete the whole
                // thing and the "load" would be a thread that instantly exits.
                double x = 0;
                while (!stop_busy.load(std::memory_order_relaxed)) x += 1.0;
                g_busy_sink.store(x, std::memory_order_relaxed);
            });
    }

    printf("\n\nTest 1 - single-shot sweep\n");
    printf("--------------------------\n");
    printf("The sub-millisecond rows are the ones that decide this: they are the\n"
           "tail slice before each heartbeat tick, and the port issues them every frame.\n");
    for (int m = 0; m < M_COUNT; m++) {
        if (method_uses_tbp(m)) timeBeginPeriod(1);
        test_sweep(m);
        if (method_uses_tbp(m)) timeEndPeriod(1);
    }

    printf("\n\nTest 2 - sustained 30 Hz heartbeat (%.1f s each)\n", seconds);
    printf("-----------------------------------------------\n");
    printf("Lateness against an absolute schedule, which is how timer_next_ advances.\n"
           "Positive median = the tick is structurally late, not jittery.\n\n");
    for (int m = 0; m < M_COUNT; m++) {
        if (method_uses_tbp(m)) timeBeginPeriod(1);
        test_heartbeat(m, seconds);
        if (method_uses_tbp(m)) timeEndPeriod(1);
    }

    printf("\n\nTest 3 - province frame, sliced as traps.cpp slices it\n");
    printf("------------------------------------------------------\n");
    printf("Target is 83.333 ms/frame at ~3.5 ticks/frame (frame-timing.md).\n"
           "~1 tick/frame means the heartbeat has collapsed into the frame rate.\n\n");
    for (int m = 0; m < M_COUNT; m++) {
        if (method_uses_tbp(m)) timeBeginPeriod(1);
        test_frame(m, 60);
        if (method_uses_tbp(m)) timeEndPeriod(1);
    }

    stop_busy = true;
    for (auto& t : load) t.join();

    printf("\n\nHow to read this\n");
    printf("----------------\n");
    printf("  * Test 1, the 0.100 ms row, is the headline. If every method costs\n"
           "    ~15.6 ms there, the naive port cannot pace itself at all and the\n"
           "    sleep model needs redesigning rather than translating.\n");
    printf("  * If the waitable-timer rows track the request but Sleep() does not,\n"
           "    the answer is a platform seam with a Windows sleep_us() built on\n"
           "    CreateWaitableTimerEx - a contained change, not a redesign.\n");
    printf("  * Test 2's median lateness is the number to carry into the port: it\n"
           "    is the per-tick error the 30 Hz heartbeat would inherit.\n");
    printf("  * Re-run with --busy 4 and compare. Multiplayer runs several game\n"
           "    instances on one host, and that is where this is likeliest to bite.\n");

    CloseHandle(g_timer);
    return 0;
}
