// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
// Minimal ELF32 (i386) reader — dependency-free.
// Supports ET_EXEC (theocracy.real) and ET_DYN (libmvos.so): PT_LOAD,
// .dynsym/.dynstr, and all SHT_REL sections. Bounds-checked against the file.
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
enum { ET_REL = 1, ET_EXEC = 2, ET_DYN = 3, EM_386 = 3 };
enum { PT_LOAD = 1, PT_DYNAMIC = 2 };
enum { PF_X = 1, PF_W = 2, PF_R = 4 };
enum { SHT_REL = 9, SHT_DYNSYM = 11, SHT_DYNAMIC = 6 };
enum { SHN_UNDEF = 0 };
// i386 relocation types
enum {
    R_386_NONE = 0, R_386_32 = 1, R_386_PC32 = 2,
    R_386_COPY = 5, R_386_GLOB_DAT = 6, R_386_JMP_SLOT = 7,
    R_386_RELATIVE = 8,
};
// st_info helpers
enum { STB_LOCAL = 0, STB_GLOBAL = 1, STB_WEAK = 2 };
enum { STT_NOTYPE = 0, STT_OBJECT = 1, STT_FUNC = 2 };
inline uint8_t st_bind(uint8_t info) { return info >> 4; }
inline uint8_t st_type(uint8_t info) { return info & 0xf; }

// DT_* tags we care about
enum { DT_NULL = 0, DT_NEEDED = 1, DT_INIT = 12, DT_FINI = 13,
       DT_INIT_ARRAY = 25, DT_INIT_ARRAYSZ = 27 };

inline uint32_t rel_sym(uint32_t info)  { return info >> 8; }
inline uint32_t rel_type(uint32_t info) { return info & 0xff; }

struct Segment { uint32_t vaddr, memsz, filesz, offset, flags; };
struct Symbol  {
    std::string name; uint32_t value, size; uint16_t shndx; uint8_t info;
    bool undef() const { return shndx == SHN_UNDEF; }
    bool weak()  const { return st_bind(info) == STB_WEAK; }
};
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
    uint16_t type() const { return ehdr().e_type; }
    bool is_dyn() const { return type() == ET_DYN; }
    bool is_exec() const { return type() == ET_EXEC; }
    const std::vector<Segment>& segments() const { return segs_; }
    const std::vector<Symbol>&  dynsyms()  const { return syms_; }
    const std::vector<Reloc>&   relocs()   const { return rels_; }
    const std::vector<Section>& sections() const { return secs_; }
    const std::vector<uint8_t>& bytes()    const { return buf_; }
    uint32_t dt_init() const { return dt_init_; }   // 0 if none (file-relative VA)
    uint32_t dt_fini() const { return dt_fini_; }

    const Symbol& sym(uint32_t i) const { return syms_.at(i); }
    const Section* find_section(const std::string& n) const {
        for (auto& s : secs_) if (s.name == n) return &s;
        return nullptr;
    }

    // Dynsym index of a name, or (uint32_t)-1.
    uint32_t find_sym_idx(const std::string& name) const {
        for (uint32_t i = 0; i < syms_.size(); ++i)
            if (syms_[i].name == name) return i;
        return (uint32_t)-1;
    }

private:
    std::vector<uint8_t> buf_;
    std::vector<Segment> segs_;
    std::vector<Symbol>  syms_;
    std::vector<Reloc>   rels_;
    std::vector<Section> secs_;
    uint32_t dt_init_ = 0, dt_fini_ = 0;

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
        if (e.e_type != ET_EXEC && e.e_type != ET_DYN)
            throw std::runtime_error("not ET_EXEC/ET_DYN");

        for (int i = 0; i < e.e_phnum; ++i) {
            const Phdr& p = *at<Phdr>(e.e_phoff + i * e.e_phentsize);
            if (p.p_type == PT_LOAD)
                segs_.push_back({p.p_vaddr, p.p_memsz, p.p_filesz, p.p_offset, p.p_flags});
        }

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
                             st[i].st_shndx, st[i].st_info});
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

        // DT_INIT / DT_FINI from SHT_DYNAMIC or PT_DYNAMIC content via section.
        for (int i = 0; i < e.e_shnum; ++i) {
            if (sh[i].sh_type != SHT_DYNAMIC) continue;
            uint32_t n = sh[i].sh_size / 8;
            for (uint32_t k = 0; k < n; ++k) {
                int32_t tag; uint32_t val;
                std::memcpy(&tag, buf_.data() + sh[i].sh_offset + k * 8, 4);
                std::memcpy(&val, buf_.data() + sh[i].sh_offset + k * 8 + 4, 4);
                if (tag == DT_NULL) break;
                if (tag == DT_INIT) dt_init_ = val;
                if (tag == DT_FINI) dt_fini_ = val;
            }
        }
    }
};

}  // namespace elf32
