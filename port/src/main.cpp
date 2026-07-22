// Guest-libmvos host: theocracy.real + libmvos.so under Unicorn; HLE the OS
// boundary (libc, FS, synthetic device plugins, SDL OpenDisplay).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "elf32.hpp"
#include "guestlink.hpp"
#include "machine.hpp"
#include "traps.hpp"

using namespace guestmap;

struct Flag { const char* name; uint32_t addr; };
static const Flag kFlags[] = {
    {"Sound",     0x085983cc}, {"Video",   0x085986ec}, {"Mouse",   0x08598b90},
    {"Keyboard",  0x0859847c}, {"Redbook", 0x08598c60}, {"Network", 0x0859848d},
    {"Pointer",   0x0859849c}, {"Timer",   0x08598080}, {"Intuition", 0x085986dc},
};

// libmvos file VAs (ET_DYN base 0) — see docs + main disassembly.
constexpr uint32_t kMvosOpenSubsystems  = 0x94f20;
constexpr uint32_t kMvosCloseSubsystems = 0x950e0;

static uint32_t run_ctors(Machine& m, uint32_t addr, uint32_t nwords, const char* tag) {
    std::vector<uint32_t> ctors;
    for (uint32_t i = 0; i < nwords; ++i) {
        uint32_t fn = m.r32(addr + 4 * i);
        if (fn == 0 || fn == 0xffffffff) continue;
        ctors.push_back(fn);
    }
    std::reverse(ctors.begin(), ctors.end());
    std::printf("\n%s .ctors: %zu constructors @ %#x\n", tag, ctors.size(), addr);

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
    std::printf("%s .ctors done: %u ok, %u aborted, %u no-return, %u faulted (of %zu)\n",
                tag, ok, aborted, timed, failed, ctors.size());
    return failed;
}

int main(int argc, char** argv) {
    std::string game_path = "data/cd/linux/theocracy.real";
    std::string mvos_path = "data/cd/linux/libmvos.so.0.9";
    if (argc > 1) game_path = argv[1];
    if (argc > 2) mvos_path = argv[2];

    std::printf("=== Theocracy guest-libmvos host ===\n");
    std::printf("game: %s\nmvos: %s\n", game_path.c_str(), mvos_path.c_str());

    elf32::Image game(game_path);
    elf32::Image mvos(mvos_path);
    Machine m;

    guestlink::LinkResult L = guestlink::link(m, game, mvos);

    m.map(STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    m.map(HEAP_BASE, HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    m.map(SCRATCH, SCRATCH_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    m.setreg(UC_X86_REG_ESP, STACK_TOP - 16);

    if (L.traps)
        L.traps->install_plugins_and_video(m, guestlink::MVOS_BASE);

    // libmvos DT_INIT + .ctors, then game .ctors --------------------------------
    if (L.mvos_init) {
        std::printf("\ncalling libmvos DT_INIT @ %#x ...\n", L.mvos_init);
        try {
            m.call(L.mvos_init, {}, /*timeout_us=*/5'000'000);
            std::printf("DT_INIT %s\n", m.last_returned() ? "returned" : "(timeout)");
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
        std::printf("\ncalling mvos.cfg loader @ %#x ...\n", id);
        try {
            m.call(id, {}, /*timeout_us=*/10'000'000);
            std::printf("mvos.cfg loader %s\n",
                        m.last_aborted() ? "aborted" :
                        m.last_returned() ? "returned" : "(timeout)");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "mvos.cfg loader FAULTED: %s\n", e.what());
        }
        std::printf("  EnvSystem head after cfg = %#x\n", m.r32(0x08598370));
    }

    if (L.game_ctors) run_ctors(m, L.game_ctors, L.game_ctors_n, "game");

    // Init ----------------------------------------------------------------------
    uint8_t before[9];
    for (int i = 0; i < 9; ++i) m.read(kFlags[i].addr, &before[i], 1);

    bool init_ok = false;
    if (L.init_app) {
        std::printf("\ncalling Init__12cApplication @ %#x ...\n", L.init_app);
        try {
            m.call(L.init_app, {SCRATCH}, /*timeout_us=*/10'000'000);
            init_ok = m.last_returned();
            std::printf("Init returned%s\n", init_ok ? "" : " (timeout/early stop)");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Init FAULTED: %s\n", e.what());
        }
    }

    std::printf("\n=== cApplication subsystem flags (pre-Init -> post-Init) ===\n");
    for (int i = 0; i < 9; ++i) {
        uint8_t v; m.read(kFlags[i].addr, &v, 1);
        std::printf("  %-10s @ %#010x : %u -> %u%s\n", kFlags[i].name, kFlags[i].addr,
                    before[i], v, (before[i] != v) ? "   <- set by Init" : "");
    }

    // The cApplication.* subsystem flags Init just wrote are R_386_COPY globals
    // with linker-shared storage, so libmvos OpenSubsystems reads the same bytes
    // — no game→mvos mirror needed.

    // OpenSubsystems (libmvos) — dlopen plugins, fill VVC/VKeyboard/… ----------
    bool open_ok = false;
    if (init_ok) {
        uint32_t open_ss = guestlink::MVOS_BASE + kMvosOpenSubsystems;
        std::printf("\ncalling OpenSubsystems @ %#x ...\n", open_ss);
        try {
            m.call(open_ss, {}, /*timeout_us=*/15'000'000);
            open_ok = m.last_returned() && !m.last_aborted();
            std::printf("OpenSubsystems %s\n",
                        m.last_aborted() ? "aborted" :
                        m.last_returned() ? "returned" : "(timeout)");
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
        for (auto [n, g] : {std::pair<const char*, uint32_t>
                 {"VVC", 0x08598cec}, {"VKeyboard", 0x08598b58}, {"VMouse", 0x08598c3c},
                 {"Intuition", 0x08598454}, {"SoundCard", 0x08598d0c}, {"VCD", 0x085984ac},
                 {"SystemMemory", 0x08598404}, {"IPCSystem", 0x08598338},
                 {"LocaleDataBase", 0x08598c4c}})
            std::printf("  %s (game) = %#x\n", n, m.r32(g));

        // Mirror libmvos main: if Intuition required, construct cIntuition
        // (sizeof 0xb4) and publish the global pointer. Start reads
        // Intuition+0x24 (active screen) — null Intuition → fault @+0x24.
        uint8_t need_i = 0;
        m.read(0x085986dc, &need_i, 1);
        if (need_i && m.r32(0x08598454) == 0) {
            uint32_t obj = HEAP_BASE + 0x00f00000;  // carve from high heap
            std::vector<uint8_t> z(0xb4, 0);
            m.write(obj, z.data(), 0xb4);
            uint32_t ctor = guestlink::MVOS_BASE + 0x8d370;  // __10cIntuition
            std::printf("\ncalling cIntuition ctor @ %#x (this=%#x) ...\n", ctor, obj);
            try {
                m.call(ctor, {obj}, /*timeout_us=*/5'000'000);
                std::printf("cIntuition ctor %s\n",
                            m.last_returned() ? "returned" : "(timeout/abort)");
            } catch (const std::exception& e) {
                std::fprintf(stderr, "cIntuition ctor FAULTED: %s\n", e.what());
            }
            m.w32(0x08598454, obj);   // shared COPY storage → libmvos sees it too
            std::printf("  Intuition = %#x\n", obj);
        }
    }

    // Start — the game (menu / state machine) ----------------------------------
    bool start_ok = false;
    if (init_ok && (open_ok || std::getenv("THEOC_START_ANYWAY"))) {
        uint32_t argv_str = SCRATCH + 0x90000, argv_arr = SCRATCH + 0x90100;
        const char kArg0[] = "theocracy";
        m.write(argv_str, kArg0, sizeof kArg0);
        m.w32(argv_arr, argv_str);
        m.w32(argv_arr + 4, 0);
        std::printf("\ncalling Start__12cApplication @ %#x ...\n", L.start_app);
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
                std::printf("  [start] THEOC_START_SEC unlimited\n");
            else
                std::printf("  [start] THEOC_START_SEC=%d (host timeout for Start)\n", sec);
            m.call(L.start_app, {SCRATCH, 1, argv_arr}, timeout_us);
            start_ok = m.last_returned() && !m.last_aborted();
            bool host_timeout = !start_ok && !m.last_aborted() && !m.last_returned()
                                && L.traps && L.traps->video().is_open();
            std::printf("Start %s\n",
                        m.last_aborted() ? "aborted" :
                        m.last_returned() ? "returned" :
                        host_timeout ? "(host Start timeout — still in game; raise THEOC_START_SEC or use 0)" :
                                       "(timeout/early stop)");
            if (host_timeout) start_ok = true;  // window open = session was live
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Start FAULTED: %s\n", e.what());
            int n = 0;
            const uint32_t* st = m.last_fault_stack(&n);
            std::fprintf(stderr, "  fault EIP=%#x ESP=%#x access=%#x\n",
                         m.last_fault_eip(), m.last_fault_esp(), m.last_fault_addr());
            for (int k = 0; k < std::min(n, 16); ++k)
                std::fprintf(stderr, "    [ESP+%02x] %#010x\n", 4 * k, st[k]);
        }
    }

    if (L.traps) L.traps->report();
    std::printf("\nGuest-libmvos: Init=%s OpenSub=%s Start=%s\n",
                init_ok ? "ok" : "fail",
                open_ok ? "ok" : "fail",
                start_ok ? "ok" : "n/a");

    if (L.traps && L.traps->video().is_open()) {
        int hold = std::getenv("THEOC_VIDEO_HOLD") ? atoi(std::getenv("THEOC_VIDEO_HOLD")) : 2;
        std::printf("video window open — holding %ds\n", hold);
        L.traps->video().keep_open_for(hold);
    }
    return init_ok ? 0 : 1;
}
