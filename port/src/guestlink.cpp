#include "guestlink.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_set>

namespace guestlink {
namespace {

uint32_t seg_prot(uint32_t elf_flags) {
    uint32_t p = 0;
    if (elf_flags & elf32::PF_R) p |= UC_PROT_READ;
    if (elf_flags & elf32::PF_W) p |= UC_PROT_WRITE;
    if (elf_flags & elf32::PF_X) p |= UC_PROT_EXEC;
    // Unicorn needs at least one prot bit; RWX for simplicity on mixed pages.
    if (!p) p = UC_PROT_ALL;
    // Writable text after reloc: map text RWX so we can apply RELATIVE/PC32 into
    // .text (libmvos has thousands of text relocs). ET_DYN needs this.
    if (elf_flags & elf32::PF_X) p = UC_PROT_ALL;
    return p;
}

// Map PT_LOAD segments of `img` at `bias` (0 for ET_EXEC). Returns end VA.
uint32_t map_image(Machine& m, const elf32::Image& img, uint32_t bias, const char* tag) {
    auto segs = img.segments();
    std::sort(segs.begin(), segs.end(),
              [](auto& a, auto& b) { return a.vaddr < b.vaddr; });
    uint32_t prev_end = 0;
    uint32_t hi = 0;
    for (auto& s : segs) {
        uint32_t va = s.vaddr + bias;
        uint32_t start = va & ~0xfffu;
        uint32_t end = (va + s.memsz + 0xfffu) & ~0xfffu;
        if (start < prev_end) start = prev_end;
        if (end > start) {
            m.map(start, end - start, seg_prot(s.flags));
            if (s.filesz)
                m.write(va, img.bytes().data() + s.offset, s.filesz);
            // BSS tail is already zero from map (Unicorn zeros new maps? —
            // not guaranteed; zero explicitly if memsz > filesz).
            if (s.memsz > s.filesz) {
                std::vector<uint8_t> z(s.memsz - s.filesz, 0);
                m.write(va + s.filesz, z.data(), (uint32_t)z.size());
            }
            prev_end = end;
        }
        if (va + s.memsz > hi) hi = va + s.memsz;
        std::fprintf(stderr, "  [%s] va %#010x memsz %#08x filesz %#08x bias+%#x -> [%#x,%#x)\n",
                    tag, s.vaddr, s.memsz, s.filesz, bias, start, end);
    }
    return hi;
}

using IdxMap = std::vector<uint32_t>;

}  // namespace

uint32_t abs_sym(const elf32::Image& img, uint32_t load_base, const std::string& name) {
    for (const auto& s : img.dynsyms())
        if (!s.undef() && s.name == name) return s.value + load_base;
    return 0;
}

LinkResult link(Machine& m, const elf32::Image& game, const elf32::Image& mvos) {
    if (!game.is_exec()) throw std::runtime_error("game must be ET_EXEC");
    if (!mvos.is_dyn())  throw std::runtime_error("libmvos must be ET_DYN");

    LinkResult R;
    std::vector<std::string> hle_names;
    std::fprintf(stderr, "=== guest link: theocracy.real + libmvos @ %#x ===\n", MVOS_BASE);

    // 1. Map both images -------------------------------------------------------
    map_image(m, game, 0, "game");
    map_image(m, mvos, MVOS_BASE, "mvos");

    // 2. Symbol tables
    //    mvos_defs — addresses *inside* libmvos (COPY source; vtable bodies).
    //    game_defs — addresses inside the game (Init/Start, local vtables, COPY
    //                *destinations* which start zeroed in .bss).
    //    Never use a game COPY destination as the COPY source.
    std::unordered_map<std::string, uint32_t> mvos_defs, mvos_sz, game_defs;
    for (const auto& s : mvos.dynsyms()) {
        if (s.undef() || s.name.empty()) continue;
        if (elf32::st_bind(s.info) == elf32::STB_LOCAL) continue;
        mvos_defs[s.name] = s.value + MVOS_BASE;
        mvos_sz[s.name] = s.size;
    }
    for (const auto& s : game.dynsyms()) {
        if (s.undef() || s.name.empty()) continue;
        if (elf32::st_bind(s.info) == elf32::STB_LOCAL) continue;
        game_defs[s.name] = s.value;
    }

    // 3. HLE set = UND not defined in the other image.
    std::unordered_set<std::string> need_hle;
    for (const auto& s : game.dynsyms()) {
        if (!s.undef() || s.name.empty()) continue;
        if (mvos_defs.count(s.name)) R.game_imports_to_mvos++;
        else { need_hle.insert(s.name); R.game_imports_to_hle++; }
    }
    for (const auto& s : mvos.dynsyms()) {
        if (!s.undef() || s.name.empty()) continue;
        if (game_defs.count(s.name)) R.mvos_imports_to_game++;
        else { need_hle.insert(s.name); R.mvos_imports_to_hle++; }
    }
    hle_names.assign(need_hle.begin(), need_hle.end());
    std::sort(hle_names.begin(), hle_names.end());
    std::fprintf(stderr, "  game UND -> mvos: %u, -> HLE: %u\n",
                R.game_imports_to_mvos, R.game_imports_to_hle);
    std::fprintf(stderr, "  mvos UND -> game: %u, -> HLE: %u\n",
                R.mvos_imports_to_game, R.mvos_imports_to_hle);
    std::fprintf(stderr, "  unique HLE symbols: %zu\n", hle_names.size());

    R.traps = std::make_unique<TrapLayer>(hle_names);
    TrapLayer* traps_ptr = R.traps.get();
    m.install_traps((uint32_t)hle_names.size(),
                    [traps_ptr](Machine& mm, uint32_t slot, uint32_t esp) {
                        return traps_ptr->dispatch(mm, slot, esp);
                    });
    std::unordered_map<std::string, uint32_t> trap_of;
    for (uint32_t i = 0; i < hle_names.size(); ++i)
        trap_of[hle_names[i]] = guestmap::TRAP_BASE + i;

    // Final global binding (mutated by COPY rebind below).
    // Seed with mvos then game so game wins on name clashes pre-COPY; COPY
    // rebind then forces shared objects to the game address.
    std::unordered_map<std::string, uint32_t> bind = mvos_defs;
    for (auto& [n, a] : game_defs) bind[n] = a;

    // Libc DATA unds (STT_OBJECT) must not resolve to trap code pages.
    // InputChFilter__8cEditRowc: mov eax,[__ctype_b]; testb $0x40,1(eax,char,2)
    // — glibc isprint bit. Without a real table, eax=0 → fault at 0xc3 for 'a'.
    {
        using namespace guestmap;
        m.map(LIBC_DATA, LIBC_DATA_SIZE, UC_PROT_READ | UC_PROT_WRITE);
        // Layout: [0]=__ctype_b pointer, [0x10]=384×u16 table (index = c+128).
        constexpr uint32_t tab = LIBC_DATA + 0x10;
        constexpr uint32_t nent = 384;
        std::vector<uint16_t> ct(nent, 0);
        // glibc _ISbit(bit): bit<8 → (1<<bit)<<8, else (1<<bit)>>8
        auto isbit = [](int bit) -> uint16_t {
            return (uint16_t)(bit < 8 ? ((1u << bit) << 8) : ((1u << bit) >> 8));
        };
        const uint16_t ISupper = isbit(0), ISlower = isbit(1), ISalpha = isbit(2);
        const uint16_t ISdigit = isbit(3), ISxdigit = isbit(4), ISspace = isbit(5);
        const uint16_t ISprint = isbit(6), ISgraph = isbit(7), ISblank = isbit(8);
        const uint16_t IScntrl = isbit(9), ISpunct = isbit(10), ISalnum = isbit(11);
        for (int c = 0; c < 256; ++c) {
            uint16_t f = 0;
            unsigned char u = (unsigned char)c;
            if (u >= 'A' && u <= 'Z') f |= ISupper | ISalpha | ISalnum | ISgraph | ISprint;
            else if (u >= 'a' && u <= 'z') f |= ISlower | ISalpha | ISalnum | ISgraph | ISprint;
            else if (u >= '0' && u <= '9') f |= ISdigit | ISxdigit | ISalnum | ISgraph | ISprint;
            else if (u == ' ' ) f |= ISspace | ISblank | ISprint;
            else if (u == '\t') f |= ISspace | ISblank | IScntrl;
            else if (u == '\n' || u == '\r' || u == '\f' || u == '\v') f |= ISspace | IScntrl;
            else if (u < 0x20 || u == 0x7f) f |= IScntrl;
            else if (u >= 0x21 && u <= 0x7e) {
                f |= ISgraph | ISprint | ISpunct;
                if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'F') || (u >= 'a' && u <= 'f'))
                    f |= ISxdigit;
            } else if (u >= 0x80) {
                // Latin-1 printable tail — treat as print so filters aren't hostile.
                f |= ISprint | ISgraph;
            }
            if ((u >= 'A' && u <= 'F') || (u >= 'a' && u <= 'f')) f |= ISxdigit;
            ct[128 + c] = f;
        }
        m.write(tab, ct.data(), (uint32_t)(ct.size() * sizeof(uint16_t)));
        // __ctype_b points at entry for c=0 (signed char index: -128..127 → 0..255).
        m.w32(LIBC_DATA, tab + 128 * 2);
        bind["__ctype_b"] = LIBC_DATA;
        std::fprintf(stderr, "  [libc] __ctype_b @ %#x -> table %#x\n", LIBC_DATA, tab + 128 * 2);
    }

    auto resolve = [&](const std::string& name, bool weak) -> uint32_t {
        if (name.empty()) return 0;
        auto it = bind.find(name);
        if (it != bind.end()) return it->second;
        auto jt = trap_of.find(name);
        if (jt != trap_of.end()) return jt->second;
        if (weak) return 0;
        std::fprintf(stderr, "  [link] unresolved strong UND: %s\n", name.c_str());
        return 0;
    };

    // R_386_COPY hands storage ownership of these globals to the game .bss: the
    // executable owns the copy, and the real ld.so makes *every* reference in
    // both images resolve to it. libmvos reaches some of them via R_386_32 to
    // its own DSO-local slot, which then diverges from the game copy — the old
    // workaround was a manual mvos↔game sync in main.cpp after each write
    // (singletons, EnvSystem, the cApplication.* flags). Give libmvos the game
    // address for its absolute refs too, so storage is genuinely shared and no
    // sync is needed. Vtables (__vt_*) keep pointing at libmvos's own relocated
    // body (virtual dispatch must hit libmvos code; the game copy is a
    // byte-identical snapshot regardless).
    std::unordered_map<std::string, uint32_t> copy_to_game;
    for (const auto& r : game.relocs()) {
        if (r.type != elf32::R_386_COPY) continue;
        const auto& s = game.sym(r.sym);
        if (s.name.empty() || s.name.rfind("__vt", 0) == 0) continue;
        copy_to_game[s.name] = r.offset;
    }
    std::fprintf(stderr, "  COPY data globals shared to game storage: %zu\n", copy_to_game.size());

    // build_idx: defined symbols use *this* image's address (so mvos R_386_32 to
    // a vtable hits mvos's relocated body). Exception: a COPY'd data global's
    // libmvos-side absolute refs resolve to the game .bss copy (shared storage,
    // above). UND uses global bind/trap.
    auto build_idx = [&](const elf32::Image& img, uint32_t bias) {
        IdxMap idx(img.dynsyms().size(), 0);
        for (uint32_t i = 0; i < img.dynsyms().size(); ++i) {
            const auto& s = img.dynsyms()[i];
            if (!s.undef()) {
                uint32_t addr = s.value + bias;             // local definition
                if (bias == MVOS_BASE && !s.name.empty()) {
                    auto cit = copy_to_game.find(s.name);
                    if (cit != copy_to_game.end()) addr = cit->second;  // shared copy
                }
                idx[i] = addr;
                continue;
            }
            idx[i] = resolve(s.name, s.weak());
        }
        return idx;
    };

    enum { F_REL = 1, F_ABS = 2, F_GOT = 4 };
    auto apply = [&](const elf32::Image& img, uint32_t bias, const IdxMap& idx,
                     int flags) {
        for (const auto& r : img.relocs()) {
            uint32_t P = r.offset + bias;
            uint32_t A = m.r32(P);
            uint32_t S = 0;
            if (r.type != elf32::R_386_RELATIVE && r.type != elf32::R_386_NONE) {
                if (r.sym >= idx.size()) continue;
                S = idx[r.sym];
            }
            switch (r.type) {
                case elf32::R_386_NONE: break;
                case elf32::R_386_RELATIVE:
                    if (flags & F_REL) m.w32(P, A + bias);
                    break;
                case elf32::R_386_32:
                    if (flags & F_ABS) m.w32(P, S + A);
                    break;
                case elf32::R_386_PC32:
                    if (flags & F_ABS) m.w32(P, S + A - P);
                    break;
                case elf32::R_386_GLOB_DAT:
                case elf32::R_386_JMP_SLOT:
                    if (flags & F_GOT) m.w32(P, S);
                    break;
                case elf32::R_386_COPY: break;
                default:
                    if (flags) std::fprintf(stderr, "  [reloc] unhandled type %u @%#x\n",
                                            r.type, P);
                    break;
            }
            R.relocs_applied++;
        }
    };

    // 4. Fully relocate libmvos (vtables get real method pointers).
    IdxMap mvos_idx = build_idx(mvos, MVOS_BASE);
    apply(mvos, MVOS_BASE, mvos_idx, F_REL | F_ABS | F_GOT);
    std::fprintf(stderr, "  mvos relocs pass 1 done\n");

    // 5. COPY from relocated mvos → game .bss; rebind name → game address.
    for (const auto& r : game.relocs()) {
        if (r.type != elf32::R_386_COPY) continue;
        const auto& s = game.sym(r.sym);
        uint32_t dst = r.offset;
        uint32_t sz = s.size ? s.size : 4;
        auto mit = mvos_defs.find(s.name);
        if (mit != mvos_defs.end()) {
            auto sit = mvos_sz.find(s.name);
            if (sit != mvos_sz.end() && sit->second) sz = sit->second;
            if (sz > 0x100000) throw std::runtime_error("COPY size absurd for " + s.name);
            std::vector<uint8_t> buf(sz);
            m.read(mit->second, buf.data(), sz);
            m.write(dst, buf.data(), sz);
        }
        bind[s.name] = dst;
        R.copies++;
    }
    std::fprintf(stderr, "  COPY relocs applied: %u (src=relocated mvos)\n", R.copies);

    // 6. mvos GOT/PLT only — point COPY'd globals (VVC, …) at game copies.
    //    R_386_32 in mvos text still references mvos-local vtable *bodies* (correct).
    //    For GLOB_DAT of COPY'd symbols we want the game address: rebuild idx
    //    using bind for UND-or-global, but local defs still local…
    //    Exception: for GLOB_DAT, the ABI wants the main executable's copy.
    //    So for GOT re-pass, use bind for every *named* global, including those
    //    defined in mvos that were COPY'd.
    auto build_idx_got = [&](const elf32::Image& img, uint32_t bias) {
        IdxMap idx(img.dynsyms().size(), 0);
        for (uint32_t i = 0; i < img.dynsyms().size(); ++i) {
            const auto& s = img.dynsyms()[i];
            if (s.name.empty()) {
                if (!s.undef()) idx[i] = s.value + bias;
                continue;
            }
            // Prefer final bind (includes COPY rebind); else local; else trap.
            auto it = bind.find(s.name);
            if (it != bind.end()) idx[i] = it->second;
            else if (!s.undef())  idx[i] = s.value + bias;
            else                  idx[i] = resolve(s.name, s.weak());
        }
        return idx;
    };
    mvos_idx = build_idx_got(mvos, MVOS_BASE);
    apply(mvos, MVOS_BASE, mvos_idx, F_GOT);

    // 7. Game relocs.
    IdxMap game_idx = build_idx_got(game, 0);
    apply(game, 0, game_idx, F_ABS | F_GOT);
    std::fprintf(stderr, "  relocs applied (counted entries): %u\n", R.relocs_applied);

    // Integrity check: a GOT/PLT slot left at 0 means a call through it jumps to
    // address 0 and faults with EIP=0 and no frame pointer -- one of the least
    // diagnosable failures possible, and easy to mistake for a null vtable or a
    // smashed stack. resolve() only warns for STRONG undefined symbols, so weak
    // ones (and anything that slipped through the idx build) would land here
    // silently. Scan both images and name every zero slot.
    {
        uint32_t zero = 0;
        auto scan = [&](const elf32::Image& img, uint32_t bias, const char* tag) {
            for (const auto& r : img.relocs()) {
                if (r.type != elf32::R_386_JMP_SLOT && r.type != elf32::R_386_GLOB_DAT)
                    continue;
                if (r.sym >= img.dynsyms().size()) continue;
                uint32_t P = r.offset + bias;
                uint32_t v = 0;
                try { v = m.r32(P); } catch (...) { continue; }
                if (v) continue;
                const std::string& n = img.dynsyms()[r.sym].name;
                if (n.empty()) continue;
                std::fprintf(stderr,
                             "  [link] ZERO GOT slot: %s '%s' -> calls through it "
                             "will fault at eip=0\n", tag, n.c_str());
                zero++;
            }
        };
        scan(game, 0, "game");
        scan(mvos, MVOS_BASE, "mvos");
        std::fprintf(stderr, "  zero GOT/PLT slots after linking: %u\n", zero);
    }

    // 8. Bookkeeping ----------------------------------------------------------
    R.init_app  = abs_sym(game, 0, "Init__12cApplication");
    R.start_app = abs_sym(game, 0, "Start__12cApplicationiPPc");
    if (mvos.dt_init()) R.mvos_init = mvos.dt_init() + MVOS_BASE;

    if (auto* ct = game.find_section(".ctors")) {
        R.game_ctors = ct->addr;
        R.game_ctors_n = ct->size / 4;
    }
    if (auto* ct = mvos.find_section(".ctors")) {
        R.mvos_ctors = ct->addr + MVOS_BASE;
        R.mvos_ctors_n = ct->size / 4;
    }

    std::fprintf(stderr, "  Init@%#x Start@%#x mvos_DT_INIT@%#x\n",
                R.init_app, R.start_app, R.mvos_init);
    std::fprintf(stderr, "  game .ctors @%#x (%u words), mvos .ctors @%#x (%u words)\n",
                R.game_ctors, R.game_ctors_n, R.mvos_ctors, R.mvos_ctors_n);
    return R;
}

}  // namespace guestlink
