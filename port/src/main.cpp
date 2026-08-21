// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
// Guest-libmvos host: theocracy.real + libmvos.so under Unicorn; HLE the OS
// boundary (libc, FS, synthetic device plugins, SDL OpenDisplay).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "blit.hpp"
#include "log.hpp"
#include "config.hpp"
#include "elf32.hpp"
#include "guestlink.hpp"
#include "machine.hpp"
#include "traps.hpp"

// Normally set by CMake to `git describe --tags --always --dirty`. The fallback
// is here so this file still compiles if someone builds it by hand, and it says
// "unknown" rather than nothing so that a log missing its version is legible as
// such instead of looking like the banner changed.
#ifndef THEOC_VERSION
#define THEOC_VERSION "unknown"
#endif

using namespace guestmap;

// The nine cApplication requirement flags. Resolved BY NAME from whichever
// executable we booted, not hardcoded: `theocracy.real` and `server` are
// different images with different .bss, and the server only carries one of them.
// Missing symbol -> address 0 -> that subsystem is simply not requestable.
struct Flag { const char* name; uint32_t addr; };
static const char* kFlagNames[] = {
    "Sound", "Video", "Mouse", "Keyboard", "Redbook",
    "Network", "Pointer", "Timer", "Intuition",
};

// libmvos file VAs (ET_DYN base 0) — see docs + main disassembly.
constexpr uint32_t kMvosOpenSubsystems  = 0x94f20;
// libmvos's CloseSubsystems (file 0x950e0) is deliberately NOT called — see
// docs/porting/host-architecture.md, "Why teardown skips CloseSubsystems".


// Walk the g++ 2.95 EBP frame chain and print a labelled guest backtrace.
// Faults previously dumped 16 raw stack words and left you to guess which one
// was a return address -- a guess that sent one investigation down a completely
// wrong path (a stack slot 6 words deep read as the call site). Frame walking
// gives the actual chain, with each address tagged for the Ghidra DB it belongs
// to: `game 0x08...` for theocracy.real, `mvos+0x...` for libmvos file offsets.
static void print_guest_backtrace(Machine& m) {
    auto label = [&](uint32_t a) {
        char b[64];
        if (a >= guestlink::MVOS_BASE && a < guestlink::MVOS_BASE + 0x00200000)
            std::snprintf(b, sizeof b, "mvos+%#x", a - guestlink::MVOS_BASE);
        else
            std::snprintf(b, sizeof b, "game %#010x", a);
        return std::string(b);
    };
    uint32_t ebp = m.last_fault_ebp();
    std::fprintf(stderr, "  guest backtrace (EBP chain from %#x):\n", ebp);
    if (!ebp) { std::fprintf(stderr, "    (no frame pointer)\n"); return; }
    for (int i = 0; i < 24 && ebp; ++i) {
        uint32_t ret = 0, next = 0;
        try { ret = m.r32(ebp + 4); next = m.r32(ebp); } catch (...) { break; }
        if (ret) std::fprintf(stderr, "    #%-2d %s\n", i, label(ret).c_str());
        if (next <= ebp) break;          // frame pointers must ascend
        ebp = next;
    }
}

static uint32_t run_ctors(Machine& m, uint32_t addr, uint32_t nwords, const char* tag) {
    std::vector<uint32_t> ctors;
    for (uint32_t i = 0; i < nwords; ++i) {
        uint32_t fn = m.r32(addr + 4 * i);
        if (fn == 0 || fn == 0xffffffff) continue;
        ctors.push_back(fn);
    }
    std::reverse(ctors.begin(), ctors.end());
    LOG_V("\n%s .ctors: %zu constructors @ %#x\n", tag, ctors.size(), addr);

    uint32_t ok = 0, failed = 0, timed = 0, aborted = 0;
    for (size_t i = 0; i < ctors.size(); ++i) {
        try {
            m.call(ctors[i], {}, /*timeout_us=*/5'000'000);
            if (m.last_aborted()) {
                aborted++;
                if (aborted <= 5)
                    std::fprintf(stderr, "  ctor #%zu @%#x aborted (Fatal?)\n",
                                 i, ctors[i]);
            } else if (m.last_returned()) {
                ok++;
            } else {
                timed++;
                std::fprintf(stderr, "  ctor #%zu @%#x did not return\n", i, ctors[i]);
            }
        } catch (const std::exception& e) {
            failed++;
            std::fprintf(stderr, "  ctor #%zu @%#x FAULTED: %s\n", i, ctors[i], e.what());
            int n = 0;
            const uint32_t* st = m.last_fault_stack(&n);
            std::fprintf(stderr, "  fault EIP=%#x ESP=%#x access=%#x stack[%d]:\n",
                         m.last_fault_eip(), m.last_fault_esp(), m.last_fault_addr(), n);
            for (int k = 0; k < n; ++k)
                std::fprintf(stderr, "    [ESP+%02x] %#010x\n", 4 * k, st[k]);
            break;
        }
    }
    // Unconditional only when something went wrong. "10 ok, 0 aborted, 0
    // no-return, 0 faulted" is a line that has never once told a user anything;
    // the same line with a non-zero column is the first thing worth seeing.
    if (aborted || timed || failed)
        std::fprintf(stderr, "%s .ctors done: %u ok, %u aborted, %u no-return, %u faulted (of %zu)\n",
                    tag, ok, aborted, timed, failed, ctors.size());
    else
        LOG_V("%s .ctors done: %u ok, %u aborted, %u no-return, %u faulted (of %zu)\n",
              tag, ok, aborted, timed, failed, ctors.size());
    return failed;
}

int main(int argc, char** argv) {
    // THEOC_SERVER=1 boots the shipped dedicated server (`server`, 47 KB, links
    // libmvos + libc) instead of the game. Same host, same linker, same HLE — the
    // server is just another executable that libmvos's main() drives, and running
    // the original binary means the netgame wire protocol never has to be
    // reimplemented. readme.linux says in-game server spawn was broken on the
    // original Linux release too, so a separate process is the intended topology.
    // THEOC_FIX_SAVE=<path>: repair a .tsg save and exit, without booting the
    // game. Same code the save path runs, so this is both the offline repair
    // tool and the way the fix is tested — it needs no display and no data
    // tree, which is what makes it verifiable at all (cf. THEOC_HEAP_TEST).
    // Before the THEOC_FIX_SAVE branch below, because that path closes a save
    // without ever reaching the banner: the repair tool must stamp the same
    // identity the game path does, or the two produce different bytes for the
    // same file and tools/fix_save.py has nothing to be checked against.
    // Before anything can print. The banner below is unconditional, but
    // everything after it asks logging::level, so this has to be resolved
    // first — and again after config::load(), which may set THEOC_VERBOSE
    // from theoc.cfg.
    logging::init();

    TrapLayer::set_build_identity(THEOC_STAMP_DATE, THEOC_COMMIT);

    if (const char* fix = std::getenv("THEOC_FIX_SAVE")) {
        std::fprintf(stderr, "=== save repair: %s ===\n", fix);
        TrapLayer::collapse_save_file(fix);
        return 0;
    }

    const bool want_server = std::getenv("THEOC_SERVER") != nullptr;
    std::string game_path = want_server ? "data/cd/linux/server"
                                        : "data/cd/linux/theocracy.real";
    std::string mvos_path = "data/cd/linux/libmvos.so.0.9";
    if (argc > 1) game_path = argv[1];
    if (argc > 2) mvos_path = argv[2];

    // Build identity in the banner, not behind a flag. A tester who sends a log
    // has then already told us which binary they ran, without being asked and
    // without knowing they did — which is the only version-reporting scheme
    // that survives contact with people who are not developers.
    std::fprintf(stderr, "=== Theocracy guest-libmvos host %s ===\n", THEOC_VERSION);

    // theoc.cfg, immediately after the banner and before anything else reads a
    // THEOC_* variable. After, so the build identity stays the first line of
    // every log; before, so a setting from the file is in the environment by the
    // time any subsystem looks for it. The two knobs read above this point
    // (THEOC_FIX_SAVE, THEOC_SERVER) are both refused from the file precisely
    // because they decide what to run rather than how to run it.
    config::load(argv[0]);
    logging::init();   // theoc.cfg may have set THEOC_VERBOSE

    LOG_V("exe:  %s\nmvos: %s\n", game_path.c_str(), mvos_path.c_str());

    elf32::Image game(game_path);
    elf32::Image mvos(mvos_path);
    Machine m;

    guestlink::LinkResult L = guestlink::link(m, game, mvos);

    // Resolve the requirement flags out of the executable we actually loaded.
    std::vector<Flag> flags;
    for (const char* n : kFlagNames)
        flags.push_back({n, guestlink::abs_sym(game, 0,
                                               std::string("_12cApplication.") + n)});
    auto flag_addr = [&](const char* n) -> uint32_t {
        for (auto& f : flags) if (std::strcmp(f.name, n) == 0) return f.addr;
        return 0;
    };
    auto game_sym = [&](const char* n) { return guestlink::abs_sym(game, 0, n); };

    // Headless is derived, not declared: an executable that does not even carry
    // the Video requirement flag can never ask for a display. The game copy-relocs
    // all nine flags; `server` carries only Network. So the same boot path serves
    // both, and we skip video/input/blit bring-up when it cannot be wanted.
    const bool headless = flag_addr("Video") == 0;
    if (headless)
        LOG_V("  [headless] no _12cApplication.Video in this image — "
                    "skipping video/input/blit bring-up\n");

    m.map(STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    m.map(HEAP_BASE, HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    m.map(SCRATCH, SCRATCH_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    m.setreg(UC_X86_REG_ESP, STACK_TOP - 16);

    // THEOC_HEAP_TEST=1: exercise the guest allocator standalone and exit. Runs
    // here (arena mapped, nothing allocated yet) and leaves it fragmented, so it
    // deliberately does not continue into boot.
    if (std::getenv("THEOC_HEAP_TEST") && L.traps) {
        std::fprintf(stderr, "\n=== guest heap self-test ===\n");
        return L.traps->heap_selftest() ? 0 : 1;
    }

    if (L.traps && !headless)
        L.traps->install_plugins_and_video(m, guestlink::MVOS_BASE);

    // Hand the trap layer the game-space singleton pointers, resolved BY NAME
    // from whichever executable we booted. These are R_386_COPY globals, so the
    // dynamic symbol table names them; hardcoding their addresses instead would
    // silently point at nothing in a differently built image. See
    // docs/porting/host-architecture.md, "Game-space addresses in the host".
    if (L.traps) {
        std::unordered_map<std::string, uint32_t> globs;
        for (const char* n : {"VVC", "VKeyboard", "VMouse", "Intuition"})
            if (uint32_t a = game_sym(n)) globs[n] = a;
        L.traps->set_game_globals(globs);
        LOG_V("  [link] game singleton globals resolved by name: %zu/4\n",
                    globs.size());
    }

    // THEOC_CONSOLE=1: arm the dev console. Skipped for headless images
    // (`server` has no screens and no console).
    if (L.traps && !headless && std::getenv("THEOC_CONSOLE"))
        L.traps->enable_dev_console();

    // THEOC_LONGRUN[=secs]: multi-hour session harness. Arms the stall watchdog
    // too unless the user set it explicitly — a fault hours in is exactly the
    // case the watchdog exists for, and forgetting it wastes the whole session.
    if (L.traps && !headless && std::getenv("THEOC_LONGRUN")) {
        if (!std::getenv("THEOC_WATCHDOG")) {
            static char wd[] = "THEOC_WATCHDOG=30";
            putenv(wd);
            std::fprintf(stderr, "  [longrun] THEOC_WATCHDOG defaulted to 30s\n");
        }
        // ...and lift the Start() budget, for the same reason. Its 600s default
        // is sized for "boot, look at it, exit", so leaving it in place caps a
        // multi-hour session at ten minutes and ends it with a line that reads
        // like a fault ("host Start timeout") when nothing went wrong. Measured:
        // a session driven with the documented invocation stopped at 0.17h.
        if (!std::getenv("THEOC_START_SEC")) {
            static char ss[] = "THEOC_START_SEC=0";
            putenv(ss);
            std::fprintf(stderr, "  [longrun] THEOC_START_SEC defaulted to unlimited\n");
        }
        L.traps->enable_longrun();
    }

    // THEOC_EDIT=1: force the game's edit mode on. Independent of the console,
    // though `save` is the main reason to want it.
    if (L.traps && !headless && std::getenv("THEOC_EDIT"))
        L.traps->enable_edit_mode();

    // libmvos DT_INIT + .ctors, then game .ctors --------------------------------
    if (L.mvos_init) {
        LOG_V("\ncalling libmvos DT_INIT @ %#x ...\n", L.mvos_init);
        try {
            m.call(L.mvos_init, {}, /*timeout_us=*/5'000'000);
            if (m.last_returned()) LOG_V("DT_INIT returned\n");
            else std::fprintf(stderr, "DT_INIT (timeout)\n");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "DT_INIT FAULTED: %s\n", e.what());
        }
    }
    if (L.mvos_ctors) run_ctors(m, L.mvos_ctors, L.mvos_ctors_n, "mvos");

    // EnvSystem, the cApplication.* flags, and the device singletons are all
    // R_386_COPY globals whose storage the linker now shares between game and
    // libmvos (guestlink copy_to_game), so the old manual mvos↔game syncs here
    // are gone — libmvos's ctors / OpenSubsystems write the game copy directly.

    // libmvos IdentifyFileSystem / load mvos.cfg (main calls this before Init).
    {
        uint32_t id = guestlink::MVOS_BASE + 0x94640;
        LOG_V("\ncalling mvos.cfg loader @ %#x ...\n", id);
        try {
            m.call(id, {}, /*timeout_us=*/10'000'000);
            if (m.last_returned() && !m.last_aborted()) {
                LOG_V("mvos.cfg loader returned\n");
            } else {
                std::fprintf(stderr, "mvos.cfg loader %s\n",
                            m.last_aborted() ? "aborted" : "(timeout)");
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "mvos.cfg loader FAULTED: %s\n", e.what());
        }
        if (uint32_t es = game_sym("EnvSystem"))
            LOG_V("  EnvSystem head after cfg = %#x\n", m.r32(es));
    }

    if (L.game_ctors) run_ctors(m, L.game_ctors, L.game_ctors_n, "game");

    // Init ----------------------------------------------------------------------
    std::vector<uint8_t> before(flags.size(), 0);
    for (size_t i = 0; i < flags.size(); ++i)
        if (flags[i].addr) m.read(flags[i].addr, &before[i], 1);

    bool init_ok = false;
    if (L.init_app) {
        LOG_V("\ncalling Init__12cApplication @ %#x ...\n", L.init_app);
        try {
            m.call(L.init_app, {SCRATCH}, /*timeout_us=*/10'000'000);
            init_ok = m.last_returned();
            if (init_ok) LOG_V("Init returned\n");
            else std::fprintf(stderr, "Init returned (timeout/early stop)\n");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Init FAULTED: %s\n", e.what());
        }
    }

    LOG_V("\n=== cApplication subsystem flags (pre-Init -> post-Init) ===\n");
    for (size_t i = 0; i < flags.size(); ++i) {
        if (!flags[i].addr) {
            LOG_V("  %-10s : not in this image\n", flags[i].name);
            continue;
        }
        uint8_t v; m.read(flags[i].addr, &v, 1);
        LOG_V("  %-10s @ %#010x : %u -> %u%s\n", flags[i].name, flags[i].addr,
                    before[i], v, (before[i] != v) ? "   <- set by Init" : "");
    }

    // The cApplication.* subsystem flags Init just wrote are R_386_COPY globals
    // with linker-shared storage, so libmvos OpenSubsystems reads the same bytes
    // — no game→mvos mirror needed.

    // OpenSubsystems (libmvos) — dlopen plugins, fill VVC/VKeyboard/… ----------
    bool open_ok = false;
    if (init_ok) {
        uint32_t open_ss = guestlink::MVOS_BASE + kMvosOpenSubsystems;
        LOG_V("\ncalling OpenSubsystems @ %#x ...\n", open_ss);
        try {
            m.call(open_ss, {}, /*timeout_us=*/15'000'000);
            open_ok = m.last_returned() && !m.last_aborted();
            if (m.last_returned() && !m.last_aborted()) {
                LOG_V("OpenSubsystems returned\n");
            } else {
                std::fprintf(stderr, "OpenSubsystems %s\n",
                            m.last_aborted() ? "aborted" : "(timeout)");
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "OpenSubsystems FAULTED: %s\n", e.what());
            int n = 0;
            const uint32_t* st = m.last_fault_stack(&n);
            std::fprintf(stderr, "  fault EIP=%#x ESP=%#x access=%#x\n",
                         m.last_fault_eip(), m.last_fault_esp(), m.last_fault_addr());
            for (int k = 0; k < n; ++k)
                std::fprintf(stderr, "    [ESP+%02x] %#010x\n", 4 * k, st[k]);
        }
        // The device singletons (VVC / VKeyboard / VMouse / Intuition / SoundCard
        // / VCD / SystemMemory / IPCSystem / LocaleDataBase) are R_386_COPY globals
        // with linker-shared storage, so OpenSubsystems already published them into
        // the game copy — no mvos→game sync needed. Log them to confirm.
        for (const char* n : {"VVC", "VKeyboard", "VMouse", "Intuition", "SoundCard",
                              "VCD", "SystemMemory", "IPCSystem", "LocaleDataBase"})
            if (uint32_t g = game_sym(n))
                LOG_V("  %s (exe) = %#x\n", n, m.r32(g));

        // Mirror libmvos main: if Intuition required, construct cIntuition
        // (sizeof 0xb4) and publish the global pointer. Start reads
        // Intuition+0x24 (active screen) — null Intuition → fault @+0x24.
        uint8_t need_i = 0;
        const uint32_t intu_flag = flag_addr("Intuition");
        const uint32_t intu_glob = game_sym("Intuition");
        if (intu_flag) m.read(intu_flag, &need_i, 1);
        if (need_i && intu_glob && m.r32(intu_glob) == 0 && L.traps) {
            // sizeof(cIntuition) = 0xb4, reserved from the guest allocator.
            // This used to be a hardcoded HEAP_BASE+0xf00000 "carve from high
            // heap" — but that address is inside the bump arena and unreserved,
            // so once cumulative allocation passed 15 MB the guest got it back
            // as ordinary memory and painted a bitmap over the live singleton.
            // Symptom: Intuition+0x24 (active cScreen*) = RGB565 pixel pairs,
            // non-null, so ActivateScreen's null guard passed and it faulted.
            uint32_t obj = L.traps->guest_alloc(0xb4);
            std::vector<uint8_t> z(0xb4, 0);
            m.write(obj, z.data(), 0xb4);
            uint32_t ctor = guestlink::MVOS_BASE + 0x8d370;  // __10cIntuition
            LOG_V("\ncalling cIntuition ctor @ %#x (this=%#x) ...\n", ctor, obj);
            try {
                m.call(ctor, {obj}, /*timeout_us=*/5'000'000);
                if (m.last_returned()) LOG_V("cIntuition ctor returned\n");
                else std::fprintf(stderr, "cIntuition ctor (timeout/abort)\n");
            } catch (const std::exception& e) {
                std::fprintf(stderr, "cIntuition ctor FAULTED: %s\n", e.what());
            }
            m.w32(intu_glob, obj);   // shared COPY storage → libmvos sees it too
            LOG_V("  Intuition = %#x\n", obj);
        }
    }

    // Start — the game (menu / state machine) ----------------------------------
    bool start_ok = false;
    if (init_ok && (open_ok || std::getenv("THEOC_START_ANYWAY"))) {
        // Optional guest-code profiler: drive the UI into the slow view and read
        // the rolling top-N dumps (THEOC_PROFILE=1). Enabled just before Start so
        // boot/ctors aren't in the sample.
        if (std::getenv("THEOC_TRACE")) m.enable_block_trace();
        if (std::getenv("THEOC_PROFILE")) m.enable_profiling(guestlink::MVOS_BASE);
        // Three instruments read exec_blocks(), not one: [fps], the [health]
        // line, and the watchdog's "guest spinning vs stuck host-side" verdict.
        // Arming the counter for THEOC_FPS alone left the other two reading a
        // frozen zero, so a watchdog-only run — the documented first reach on
        // "it froze" — called every stall "NOT EXECUTING" no matter what the
        // guest was doing. It costs one relaxed increment per basic block, and
        // that does cost frames; growth is reported per 1k frames as well as per
        // hour precisely so a session with it on stays comparable to one without.
        else if (std::getenv("THEOC_FPS") || std::getenv("THEOC_LONGRUN") ||
                 std::getenv("THEOC_WATCHDOG"))
            m.enable_block_counter();

        // Native overrides for the hot LFB16 software rasterizer (province view
        // was CPU-bound emulating these pixel loops). THEOC_NATIVE_BLIT=0 falls
        // back to the emulated libmvos originals for A/B comparison.
        if (!headless) install_native_blit(m, guestlink::MVOS_BASE);

        // THEOC_PROVINCE_MS — retune cProvince_Do's 12fps frame limiter. A game
        // patch, and a game-*speed* one: province steps its sim once per frame.
        L.traps->install_province_rate(m);

        // THEOC_DUMP_WORLD — read the starting world out of the game's own
        // loader (heroes and magic items per init.dat). Passive; patches
        // nothing. See docs/subsystems/starting-world.md.
        L.traps->install_world_dump(m);

        // THEOC_NEW_WORLD — select the game's own "init mode" so the world is
        // generated rather than loaded. Its campaign builder, recovered.
        L.traps->install_world_gen(m);

        uint32_t argv_str = SCRATCH + 0x90000, argv_arr = SCRATCH + 0x90100;
        const char kArg0[] = "theocracy";
        m.write(argv_str, kArg0, sizeof kArg0);
        m.w32(argv_arr, argv_str);
        m.w32(argv_arr + 4, 0);
        LOG_V("\ncalling Start__12cApplication @ %#x ...\n", L.start_app);
        try {
            // Host wall-clock budget for the entire Start() call (intros + menu +
            // play). Not an in-game menu timer. Override with THEOC_START_SEC;
            // 0 = unlimited. Default 600s covers paced cutscenes (~80s video) + play.
            int sec = 600;
            if (const char* e = std::getenv("THEOC_START_SEC"))
                sec = atoi(e);
            if (sec < 0) sec = 0;
            uint64_t timeout_us = sec == 0 ? 0 : (uint64_t)sec * 1'000'000ull;
            if (sec == 0)
                std::fprintf(stderr, "  [start] THEOC_START_SEC unlimited\n");
            else
                LOG_V("  [start] THEOC_START_SEC=%d (host timeout for Start)\n", sec);
            m.call(L.start_app, {SCRATCH, 1, argv_arr}, timeout_us);
            start_ok = m.last_returned() && !m.last_aborted();
            bool host_timeout = !start_ok && !m.last_aborted() && !m.last_returned()
                                && L.traps && L.traps->video().is_open();
            std::fprintf(stderr, "Start %s\n",
                        m.last_aborted() ? "aborted" :
                        m.last_returned() ? "returned" :
                        host_timeout ? "(host Start timeout — still in game; raise THEOC_START_SEC or use 0)" :
                                       "(timeout/early stop)");
            if (host_timeout) start_ok = true;  // window open = session was live
            // Nothing else prints the last world's block; do it while stderr
            // is still up and before any teardown noise.
            L.traps->flush_world_dump();
        } catch (const std::exception& e) {
            L.traps->flush_world_dump();
            std::fprintf(stderr, "Start FAULTED: %s\n", e.what());
            int n = 0;
            const uint32_t* st = m.last_fault_stack(&n);
            std::fprintf(stderr, "  fault EIP=%#x ESP=%#x EBP=%#x access=%#x\n",
                         m.last_fault_eip(), m.last_fault_esp(), m.last_fault_ebp(),
                         m.last_fault_addr());
            print_guest_backtrace(m);
            if (m.trace_depth()) {
                // Most-recent-last: the final entries are the path into the fault.
                std::fprintf(stderr, "  last %d basic blocks (THEOC_TRACE):\n",
                             m.trace_depth());
                int start = m.trace_depth() < 32 ? 0 : 0;
                for (int i = start; i < m.trace_depth(); ++i) {
                    uint32_t a = m.trace_at(i);
                    if (a >= guestlink::MVOS_BASE && a < guestlink::MVOS_BASE + 0x200000)
                        std::fprintf(stderr, "    mvos+%#x\n", a - guestlink::MVOS_BASE);
                    else
                        std::fprintf(stderr, "    game %#010x\n", a);
                }
            } else {
                std::fprintf(stderr, "  (re-run with THEOC_TRACE=1 for a block trace"
                                     " -- essential when EBP is 0)\n");
            }
            for (int k = 0; k < std::min(n, 16); ++k)
                std::fprintf(stderr, "    [ESP+%02x] %#010x\n", 4 * k, st[k]);
        }
    }

    if (L.traps) L.traps->stop_watchdog();  // frames stop legitimately now
    if (L.traps) L.traps->report();
    if (init_ok && open_ok && start_ok)
        LOG_V("\nGuest-libmvos: Init=ok OpenSub=ok Start=ok\n");
    else
        std::fprintf(stderr, "\nGuest-libmvos: Init=%s OpenSub=%s Start=%s\n",
                    init_ok ? "ok" : "fail",
                    open_ok ? "ok" : "fail",
                    start_ok ? "ok" : "n/a");

    if (L.traps && L.traps->video().is_open()) {
        int hold = std::getenv("THEOC_VIDEO_HOLD") ? atoi(std::getenv("THEOC_VIDEO_HOLD")) : 2;
        std::fprintf(stderr, "video window open — holding %ds\n", hold);
        L.traps->video().keep_open_for(hold);
    }
    return init_ok ? 0 : 1;
}
