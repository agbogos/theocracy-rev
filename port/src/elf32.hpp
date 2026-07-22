// Minimal ELF32 (i386) reader for theocracy.real — dependency-free.
// We only need: PT_LOAD segments, .dynsym/.dynstr, and the REL relocation
// sections (.rel.plt / .rel.got / .rel.bss). No section is trusted beyond
// what the M1 loader consumes; everything is bounds-checked against the file.
#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace elf32 {

#pragma pack(push, 1)
struct Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};
struct Phdr {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
};
struct Shdr {
    uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
             sh_link, sh_info, sh_addralign, sh_entsize;
};
struct Sym {
    uint32_t st_name, st_value, st_size;
    uint8_t  st_info, st_other;
    uint16_t st_shndx;
};
struct Rel { uint32_t r_offset, r_info; };
#pragma pack(pop)

// e_type / p_type / sh_type
enum { ET_EXEC = 2, EM_386 = 3 };
enum { PT_LOAD = 1 };
enum { PF_X = 1, PF_W = 2, PF_R = 4 };
enum { SHT_REL = 9, SHT_DYNSYM = 11 };
enum { SHN_UNDEF = 0 };
// i386 relocation types
enum { R_386_32 = 1, R_386_COPY = 5, R_386_GLOB_DAT = 6, R_386_JMP_SLOT = 7,
       R_386_RELATIVE = 8 };

inline uint32_t rel_sym(uint32_t info)  { return info >> 8; }
inline uint32_t rel_type(uint32_t info) { return info & 0xff; }

struct Segment { uint32_t vaddr, memsz, filesz, offset, flags; };
struct Symbol  { std::string name; uint32_t value, size; uint16_t shndx;
                 bool undef() const { return shndx == SHN_UNDEF; } };
struct Reloc   { std::string section; uint32_t offset, sym, type; };
struct Section { std::string name; uint32_t addr, size, offset, type; };

class Image {
public:
    explicit Image(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open " + path);
        buf_.assign(std::istreambuf_iterator<char>(f), {});
        parse();
    }

    const Ehdr& ehdr() const { return *reinterpret_cast<const Ehdr*>(buf_.data()); }
    const std::vector<Segment>& segments() const { return segs_; }
    const std::vector<Symbol>&  dynsyms()  const { return syms_; }
    const std::vector<Reloc>&   relocs()   const { return rels_; }
    const std::vector<Section>& sections() const { return secs_; }
    const std::vector<uint8_t>& bytes()    const { return buf_; }

    const Symbol& sym(uint32_t i) const { return syms_.at(i); }
    const Section* find_section(const std::string& n) const {
        for (auto& s : secs_) if (s.name == n) return &s;
        return nullptr;
    }

private:
    std::vector<uint8_t> buf_;
    std::vector<Segment> segs_;
    std::vector<Symbol>  syms_;
    std::vector<Reloc>   rels_;
    std::vector<Section> secs_;

    template <class T> const T* at(uint32_t off, uint32_t n = 1) const {
        if (uint64_t(off) + uint64_t(sizeof(T)) * n > buf_.size())
            throw std::runtime_error("ELF: out-of-bounds structure read");
        return reinterpret_cast<const T*>(buf_.data() + off);
    }

    void parse() {
        const Ehdr& e = ehdr();
        if (std::memcmp(e.e_ident, "\x7f""ELF", 4) != 0)
            throw std::runtime_error("not an ELF file");
        if (e.e_ident[4] != 1 /*ELFCLASS32*/ || e.e_machine != EM_386)
            throw std::runtime_error("not ELF32 i386");
        if (e.e_type != ET_EXEC)
            throw std::runtime_error("not ET_EXEC");

        for (int i = 0; i < e.e_phnum; ++i) {
            const Phdr& p = *at<Phdr>(e.e_phoff + i * e.e_phentsize);
            if (p.p_type == PT_LOAD)
                segs_.push_back({p.p_vaddr, p.p_memsz, p.p_filesz, p.p_offset, p.p_flags});
        }

        // Section headers: locate .dynsym (+ its .dynstr via sh_link) and the
        // REL sections. Names come from .shstrtab (e_shstrndx).
        const Shdr* sh = at<Shdr>(e.e_shoff, e.e_shnum);
        const Shdr& shstr = sh[e.e_shstrndx];
        auto secname = [&](uint32_t nameoff) -> std::string {
            const char* s = reinterpret_cast<const char*>(
                buf_.data() + shstr.sh_offset + nameoff);
            return std::string(s);
        };

        for (int i = 0; i < e.e_shnum; ++i)
            secs_.push_back({secname(sh[i].sh_name), sh[i].sh_addr, sh[i].sh_size,
                             sh[i].sh_offset, sh[i].sh_type});

        const Shdr* dynsym = nullptr;
        for (int i = 0; i < e.e_shnum; ++i)
            if (sh[i].sh_type == SHT_DYNSYM) { dynsym = &sh[i]; break; }
        if (!dynsym) throw std::runtime_error("no .dynsym");
        const Shdr& dynstr = sh[dynsym->sh_link];

        uint32_t nsym = dynsym->sh_size / sizeof(Sym);
        const Sym* st = at<Sym>(dynsym->sh_offset, nsym);
        syms_.reserve(nsym);
        for (uint32_t i = 0; i < nsym; ++i) {
            const char* nm = reinterpret_cast<const char*>(
                buf_.data() + dynstr.sh_offset + st[i].st_name);
            syms_.push_back({std::string(nm), st[i].st_value, st[i].st_size,
                             st[i].st_shndx});
        }

        for (int i = 0; i < e.e_shnum; ++i) {
            if (sh[i].sh_type != SHT_REL) continue;
            std::string name = secname(sh[i].sh_name);
            uint32_t nrel = sh[i].sh_size / sizeof(Rel);
            const Rel* rl = at<Rel>(sh[i].sh_offset, nrel);
            for (uint32_t r = 0; r < nrel; ++r)
                rels_.push_back({name, rl[r].r_offset,
                                 rel_sym(rl[r].r_info), rel_type(rl[r].r_info)});
        }
    }
};

}  // namespace elf32
