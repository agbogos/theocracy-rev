// M1 — ELF loader + Unicorn bring-up for theocracy.real.
// Maps the game, wires the 232-import HLE trap boundary, runs the game's own
// C++ global constructors, calls cApplication::Init, and reads back the 9
// subsystem flags. This is "we execute the game's code and reach its first
// callback."
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "elf32.hpp"
#include "machine.hpp"
#include "mvos.hpp"
#include "traps.hpp"

using namespace guestmap;

// cApplication subsystem flags: static members COPY-relocated into game .bss.
// Addresses verified against theocracy.real (see docs/porting/m1-loader.md).
struct Flag { const char* name; uint32_t addr; };
static const Flag kFlags[] = {
    {"Sound",     0x085983cc}, {"Video",   0x085986ec}, {"Mouse",   0x08598b90},
    {"Keyboard",  0x0859847c}, {"Redbook", 0x08598c60}, {"Network", 0x0859848d},
    {"Pointer",   0x0859849c}, {"Timer",   0x08598080}, {"Intuition", 0x085986dc},
};
constexpr uint32_t kInitAddr  = 0x08144600;  // Init__12cApplication
constexpr uint32_t kStartAddr = 0x08144650;  // Start__12cApplicationiPPc (M2)

static uint32_t seg_prot(uint32_t elf_flags) {
    uint32_t p = 0;
    if (elf_flags & elf32::PF_R) p |= UC_PROT_READ;
    if (elf_flags & elf32::PF_W) p |= UC_PROT_WRITE;
    if (elf_flags & elf32::PF_X) p |= UC_PROT_EXEC;
    return p;
}

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "linux/theocracy.real";
    std::printf("=== Theocracy M1 loader ===\nimage: %s\n", path.c_str());

    elf32::Image img(path);
    Machine m;

    // 1. Map PT_LOAD segments (page-aligned, tiled, no overlap). --------------
    auto segs = img.segments();
    std::sort(segs.begin(), segs.end(),
              [](auto& a, auto& b) { return a.vaddr < b.vaddr; });
    uint32_t prev_end = 0;
    for (auto& s : segs) {
        uint32_t start = s.vaddr & ~0xfffu;
        uint32_t end = (s.vaddr + s.memsz + 0xfffu) & ~0xfffu;
        if (start < prev_end) start = prev_end;   // clamp shared boundary page
        if (end > start) {
            m.map(start, end - start, seg_prot(s.flags));
            m.write(s.vaddr, img.bytes().data() + s.offset, s.filesz);
            prev_end = end;
        }
        std::printf("  segment va %#010x memsz %#08x filesz %#08x -> map [%#x,%#x) %c%c%c\n",
                    s.vaddr, s.memsz, s.filesz, start, end,
                    (s.flags & elf32::PF_R) ? 'r' : '-',
                    (s.flags & elf32::PF_W) ? 'w' : '-',
                    (s.flags & elf32::PF_X) ? 'x' : '-');
    }

    // 2. Assign a trap slot to every UND import (in .dynsym order). -----------
    std::vector<std::string> names;          // slot -> name
    std::unordered_map<uint32_t, uint32_t> symidx_to_trap;  // dynsym idx -> addr
    for (uint32_t i = 0; i < img.dynsyms().size(); ++i) {
        const auto& sym = img.dynsyms()[i];
        if (!sym.undef() || sym.name.empty()) continue;
        symidx_to_trap[i] = TRAP_BASE + (uint32_t)names.size();
        names.push_back(sym.name);
    }
    std::printf("imports (trap boundary): %zu\n", names.size());

    TrapLayer traps(names);
    m.install_traps((uint32_t)names.size(),
                    [&traps](Machine& mm, uint32_t slot, uint32_t esp) {
                        return traps.dispatch(mm, slot, esp);
                    });

    // 3. Apply relocations. --------------------------------------------------
    // JMP_SLOT/GLOB_DAT: point GOT slot at the trap addr (UND) or the local
    // symbol value (vague-linkage refs to the game's own exports). COPY: the
    // framework singletons/vtables — left zero for M1 (Init sets the flags).
    std::map<uint32_t, uint32_t> reltypes;   // type -> count
    uint32_t jmp_local = 0, jmp_trap = 0, copies = 0;
    for (const auto& r : img.relocs()) {
        reltypes[r.type]++;
        switch (r.type) {
            case elf32::R_386_JMP_SLOT:
            case elf32::R_386_GLOB_DAT: {
                const auto& sym = img.sym(r.sym);
                uint32_t val;
                if (sym.undef()) { val = symidx_to_trap[r.sym]; jmp_trap++; }
                else             { val = sym.value; jmp_local++; }
                m.w32(r.offset, val);
                break;
            }
            case elf32::R_386_COPY:
                copies++;   // leave target zero-initialized for M1
                break;
            default:
                std::fprintf(stderr, "  [reloc] unhandled type %u at %#x\n",
                             r.type, r.offset);
        }
    }
    std::printf("relocs:");
    for (auto& [t, n] : reltypes) std::printf(" type%u=%u", t, n);
    std::printf("  (jmp->trap %u, jmp->local %u, copy %u)\n",
                jmp_trap, jmp_local, copies);

    // 3b. MVOS layer: synthesize vtables from the COPY relocs, wire singletons,
    // and register native handlers (must precede .ctors, which dispatch through
    // these vtables).
    Mvos mvos(m, img, traps);
    mvos.apply_copyrelocs();

    // 4. Stack / heap / scratch, and initial ESP. ----------------------------
    m.map(STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    m.map(HEAP_BASE, HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    m.map(SCRATCH, SCRATCH_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    m.setreg(UC_X86_REG_ESP, STACK_TOP - 16);

    // 5. Run the game's global constructors (.ctors). ------------------------
    // GCC 2.x layout: [ -1 marker, ctor0, ctor1, ..., 0 ]; run high->low,
    // skipping the header and the null terminator.
    const auto* ct = img.find_section(".ctors");
    if (!ct) { std::fprintf(stderr, "no .ctors!\n"); return 1; }
    uint32_t nword = ct->size / 4;
    std::vector<uint32_t> ctors;
    for (uint32_t i = 0; i < nword; ++i) {
        uint32_t fn = m.r32(ct->addr + 4 * i);
        if (fn == 0 || fn == 0xffffffff) continue;
        ctors.push_back(fn);
    }
    std::reverse(ctors.begin(), ctors.end());
    std::printf("\n.ctors: %zu constructors @ %#x\n", ctors.size(), ct->addr);

    uint32_t ok = 0, failed = 0, timed = 0;
    for (size_t i = 0; i < ctors.size(); ++i) {
        try {
            m.call(ctors[i], {}, /*timeout_us=*/2'000'000);
            if (m.last_returned()) ok++;
            else { timed++; std::fprintf(stderr, "  ctor #%zu @%#x did not return\n",
                                         i, ctors[i]); }
        } catch (const std::exception& e) {
            failed++;
            std::fprintf(stderr, "  ctor #%zu @%#x FAULTED: %s\n", i, ctors[i], e.what());
        }
    }
    std::printf(".ctors done: %u ok, %u no-return, %u faulted\n", ok, timed, failed);

    // 6. Call cApplication::Init(this) with a scratch instance. ---------------
    // Snapshot the flags first: .bss is zeroed and COPY relocs were skipped, so
    // any that read 1 afterward were written by Init (proving we reached it).
    uint8_t before[9];
    for (int i = 0; i < 9; ++i) m.read(kFlags[i].addr, &before[i], 1);

    std::printf("\ncalling Init__12cApplication @ %#x ...\n", kInitAddr);
    bool init_ok = false;
    try {
        m.call(kInitAddr, {SCRATCH}, /*timeout_us=*/5'000'000);
        init_ok = m.last_returned();
        std::printf("Init returned%s\n", init_ok ? "" : " (timeout/early stop)");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Init FAULTED: %s\n", e.what());
    }

    // 7. Read back the 9 subsystem flags. ------------------------------------
    std::printf("\n=== cApplication subsystem flags (pre-Init -> post-Init) ===\n");
    for (int i = 0; i < 9; ++i) {
        uint8_t v; m.read(kFlags[i].addr, &v, 1);
        std::printf("  %-10s @ %#010x : %u -> %u%s\n", kFlags[i].name, kFlags[i].addr,
                    before[i], v, (before[i] != v) ? "   <- set by Init" : "");
    }

    // 7b. Direct video-path test: invoke cVVC::OpenDisplay in isolation with a
    // synthetic cVModeRequest, proving the SDL backend + trap end-to-end without
    // depending on Start's (still-unmapped) early game-side wiring.
    if (init_ok && std::getenv("THEOC_VIDEO_TEST")) {
        uint32_t vvc = m.r32(0x08598cec);                 // VVC singleton pointer
        uint32_t req = SCRATCH + 0x91000;
        m.w32(req + 0, 800); m.w32(req + 4, 600); m.w32(req + 8, 5);  // 800x600, RGB565
        uint32_t od = traps.trap_addr("OpenDisplay__4cVVCRC13cVModeRequest");
        std::printf("\n[video-test] cVVC=%#x OpenDisplay-trap=%#x\n", vvc, od);
        if (od) {
            uint32_t r = m.call(od, {vvc, req}, /*timeout_us=*/2'000'000);
            std::printf("[video-test] OpenDisplay returned %u\n", r);
            // Paint an RGB565 gradient so the framebuffer->window path is visibly
            // producing pixels (stand-in until the cGD draw traps are wired).
            Video& v = mvos.video();
            if (v.is_open()) {
                uint16_t* fb = v.fb();
                for (int y = 0; y < v.height(); ++y)
                    for (int x = 0; x < v.width(); ++x) {
                        uint16_t rr = (uint16_t)(x * 31 / v.width());
                        uint16_t gg = (uint16_t)(y * 63 / v.height());
                        uint16_t bb = (uint16_t)((x + y) * 31 / (v.width() + v.height()));
                        fb[y * v.width() + x] = (uint16_t)((rr << 11) | (gg << 5) | bb);
                    }
                v.present();
            }
        }
    }

    // 8. M2: call cApplication::Start(argc, argv) — drives config, video bring-up
    // (cVVC::OpenDisplay -> our SDL window), intro, and the menu. Unmapped
    // territory; run under a timeout and report where it lands.
    if (init_ok && std::getenv("THEOC_START")) {
        // Minimal argv: ["theocracy", NULL] in a scratch corner.
        uint32_t argv_str = SCRATCH + 0x90000, argv_arr = SCRATCH + 0x90100;
        const char kArg0[] = "theocracy";
        m.write(argv_str, kArg0, sizeof kArg0);
        m.w32(argv_arr, argv_str);
        m.w32(argv_arr + 4, 0);
        std::printf("\ncalling Start__12cApplication @ %#x ...\n", kStartAddr);
        try {
            m.call(kStartAddr, {SCRATCH, 1, argv_arr}, /*timeout_us=*/15'000'000);
            std::printf("Start %s\n", m.last_returned() ? "returned" : "(timeout/early stop)");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Start stopped: %s\n", e.what());
        }
    }

    traps.report();
    mvos.report();
    std::printf("\nM1 %s\n", init_ok ? "REACHED Init cleanly." :
                                       "ran (see trap report for the M2 worklist).");

    // Keep the window up briefly so the SDL bring-up is visible.
    if (mvos.video().is_open()) {
        int hold = std::getenv("THEOC_VIDEO_HOLD") ? atoi(std::getenv("THEOC_VIDEO_HOLD")) : 3;
        std::printf("video window open — holding %ds\n", hold);
        mvos.video().keep_open_for(hold);
    }
    return 0;
}
