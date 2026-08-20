// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
#include "blit.hpp"
#include "machine.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

// libmvos file offsets (== ELF vaddr; runtime = mvos_base + offset). Confirmed
// from Ghidra (Ghidra addr = file offset + 0x10000). Despite Ghidra tagging them
// __regparm2, the disassembly reads *every* argument from the stack ([EBP+8]..),
// i.e. plain cdecl — the entry PUSHAD / exit POPAD make the EAX/EDX regparm slots
// dead. So we read args off the guest stack at esp+4, esp+8, ...
namespace {
constexpr uint32_t OFF_PutBitmap        = 0x5c4e0;  // LFB16_PutBitmap
constexpr uint32_t OFF_VLineAlfa        = 0x5c940;  // LFB16_VLineAlfa
constexpr uint32_t OFF_PutBitmap8       = 0x5c9b0;  // LFB16_PutBitmap8
constexpr uint32_t OFF_PutBitmap8AMask  = 0x5cb70;  // LFB16_PutBitmap8_AMask
constexpr uint32_t OFF_PutBitmap8C1Mask = 0x5cbb0;  // LFB16_PutBitmap8C1_AMask

// Lazy, generation-stamped palette cache (avoids per-call memset and never
// over-reads past the palette's mapping). One instance is reused per blitter.
struct Palette {
    Machine* m = nullptr;
    uint32_t base = 0;
    uint16_t c[256];
    uint32_t g[256] = {0};
    uint32_t gen = 0;
    void begin(Machine* mm, uint32_t b) { m = mm; base = b; ++gen; }
    inline uint16_t operator[](uint8_t idx) {
        if (g[idx] != gen) {
            uint8_t t[2];
            try { m->read(base + (uint32_t)idx * 2, t, 2); }
            catch (...) { t[0] = t[1] = 0; }
            c[idx] = (uint16_t)(t[0] | (t[1] << 8));
            g[idx] = gen;
        }
        return c[idx];
    }
};

// ---- a bounded, host-side mirror of a strided guest span ---------------------
// The blitters walk destination rows separated by a constant byte stride. The
// whole touched region [min,max) is contiguous in guest memory (rows plus the
// inter-row gaps), so we mirror it into one host buffer, mutate it natively, and
// write it back — O(1) guest reads/writes per call instead of O(pixels).
struct Span {
    uint32_t base = 0;                 // guest addr of buffer[0]
    std::vector<uint8_t>* buf = nullptr;

    inline uint16_t r16(uint32_t addr) const {
        uint32_t o = addr - base;
        return (uint16_t)((*buf)[o] | ((*buf)[o + 1] << 8));
    }
    inline void w16(uint32_t addr, uint16_t v) {
        uint32_t o = addr - base;
        (*buf)[o] = (uint8_t)v;
        (*buf)[o + 1] = (uint8_t)(v >> 8);
    }
};

// Min/max byte extent of `rows` rows of `rowBytes`, starting at `base`, each row
// advanced by signed `stride`. Row starts are linear in r, so the extremes are
// at r=0 and r=rows-1.
void span_bounds(uint32_t base, int32_t stride, uint32_t rowBytes, int rows,
                 uint32_t& lo, uint32_t& hi) {
    int64_t a0 = (int64_t)base;
    int64_t aN = (int64_t)base + (int64_t)stride * (rows - 1);
    int64_t mn = a0 < aN ? a0 : aN;
    int64_t mx = (a0 > aN ? a0 : aN) + rowBytes;
    lo = (uint32_t)mn;
    hi = (uint32_t)mx;
}

// Forward, single-page cache over a linear guest byte stream (the RLE source).
// Offsets only ever increase, but runs can skip forward across pages.
struct SrcReader {
    Machine* m = nullptr;
    uint32_t base = 0;
    uint32_t page_start = 1;           // no page loaded (start > any real off+? )
    uint32_t page_len = 0;
    uint8_t page[4096];

    inline uint8_t at(uint32_t off) {
        if (off < page_start || off >= page_start + page_len) {
            uint32_t ps = off & ~0xfffu;
            uint32_t want = 0x1000;
            // Read a full page; shrink on fault so we never read past mapping.
            for (;;) {
                try {
                    m->read(base + ps, page, want);
                    break;
                } catch (...) {
                    if (want <= 1) { page[0] = 0; want = 1; break; }
                    want >>= 1;
                }
            }
            page_start = ps;
            page_len = want;
            if (off - ps >= page_len) return 0;
        }
        return page[off - page_start];
    }
};

// ---- LFB16_PutBitmap8C1_AMask ------------------------------------------------
// Args (cdecl, at esp+4..): src, dst, clipLeft, palette, width, height,
// clipRight, dstAddBytes. Packet stream: [count][flag]; flag==0 => `count`
// transparent pixels; else the packet's `count` bytes (starting at the flag
// byte) are palette indices, index 0 = transparent hole. clipLeft/clipRight trim
// source pixels at row edges; the row's visible width is always `width`.
uint32_t blit_8c1_amask(Machine& m, uint32_t /*slot*/, uint32_t esp) {
    uint32_t src     = m.r32(esp + 4);
    uint32_t dst     = m.r32(esp + 8);
    uint32_t p5      = m.r32(esp + 12);          // clipLeft
    uint32_t pal     = m.r32(esp + 16);          // palette base (uint16[])
    int32_t  width   = (int32_t)m.r32(esp + 20);
    int32_t  height  = (int32_t)m.r32(esp + 24);
    uint32_t p9      = m.r32(esp + 28);          // clipRight
    int32_t  p10     = (int32_t)m.r32(esp + 32); // dst row add (bytes)
    if (width <= 0 || height <= 0) return 0;

    int32_t rowStride = width * 2 + p10;
    uint32_t lo, hi;
    span_bounds(dst, rowStride, (uint32_t)width * 2, height, lo, hi);

    static std::vector<uint8_t> dbuf;
    dbuf.resize(hi - lo);
    m.read(lo, dbuf.data(), hi - lo);            // preserve transparent pixels
    Span dstv{lo, &dbuf};

    static SrcReader sr;
    sr.m = &m; sr.base = src;
    sr.page_start = 1; sr.page_len = 0;          // invalidate cache for this call

    // Lazy palette cache (generation-stamped to skip per-call memset).
    static uint16_t pcache[256];
    static uint32_t pgen[256];
    static uint32_t gen = 0;
    ++gen;
    auto palette = [&](uint8_t idx) -> uint16_t {
        if (pgen[idx] != gen) {
            uint8_t two[2];
            try { m.read(pal + (uint32_t)idx * 2, two, 2); }
            catch (...) { two[0] = two[1] = 0; }
            pcache[idx] = (uint16_t)(two[0] | (two[1] << 8));
            pgen[idx] = gen;
        }
        return pcache[idx];
    };

    uint32_t so = 0;         // src byte offset (forward only)
    uint32_t dp = dst;       // dst guest byte addr
    int32_t  run = width;    // pixels remaining in current row (iVar4)
    uint32_t clip = p5;      // clipLeft (persistent stack slot in the original)

    for (;;) {
        uint32_t count = sr.at(so);
        uint8_t  flag  = sr.at(so + 1);

        if (flag == 0) {                          // transparent run of `count`
            so += 2;
            if (clip != 0) {
                if (clip < count) { count -= clip; clip = 0; }
                else { clip -= count; count = 0; }
            }
            while (count != 0) {
                --count; dp += 2; --run;
                if (run == 0) {
                    dp += p10; if (--height == 0) break;
                    run = width;
                    if (p9 != 0) {
                        if (count <= p9) { clip = p9 - count; break; }
                        count -= p9;
                    }
                }
            }
            if (height == 0) break;
            continue;
        }

        // opaque run: `count` palette-index bytes, starting at the flag byte.
        uint32_t rem = count;
        so += 1;                                  // advance past count byte
        if (clip != 0) {
            uint32_t skip, newclip;
            if (clip < count) { rem = count - clip; newclip = 0; skip = clip; }
            else { newclip = clip - count; rem = 0; skip = count; }
            so += skip;
            clip = newclip;
        }
        while (rem != 0) {
            --rem;
            uint8_t idx = sr.at(so);
            if (idx != 0) dstv.w16(dp, palette(idx));
            dp += 2; --run; ++so;
            if (run == 0) {
                dp += p10; if (--height == 0) break;
                run = width;
                if (p9 != 0) {
                    if (rem <= p9) { so += rem; clip = p9 - rem; break; }
                    rem -= p9; so += p9;
                }
            }
        }
        if (height == 0) break;
    }

    m.write(lo, dbuf.data(), hi - lo);
    return 0;
}

// ---- LFB16_PutBitmap ---------------------------------------------------------
// Args: src, dst, width, height, srcAddBytes, dstAddBytes. Plain 16bpp rect
// copy, rows separated by (width*2 + add) bytes on each side.
uint32_t blit_putbitmap(Machine& m, uint32_t /*slot*/, uint32_t esp) {
    uint32_t src    = m.r32(esp + 4);
    uint32_t dst    = m.r32(esp + 8);
    int32_t  width  = (int32_t)m.r32(esp + 12);
    int32_t  height = (int32_t)m.r32(esp + 16);
    int32_t  srcAdd = (int32_t)m.r32(esp + 20);
    int32_t  dstAdd = (int32_t)m.r32(esp + 24);
    if (width <= 0 || height <= 0) return 0;

    uint32_t rowBytes = (uint32_t)width * 2;
    int32_t  srcStride = (int32_t)rowBytes + srcAdd;
    int32_t  dstStride = (int32_t)rowBytes + dstAdd;

    uint32_t slo, shi, dlo, dhi;
    span_bounds(src, srcStride, rowBytes, height, slo, shi);
    span_bounds(dst, dstStride, rowBytes, height, dlo, dhi);

    static std::vector<uint8_t> sbuf, dbuf;
    sbuf.resize(shi - slo);
    dbuf.resize(dhi - dlo);
    m.read(slo, sbuf.data(), shi - slo);
    m.read(dlo, dbuf.data(), dhi - dlo);          // preserve inter-row gaps

    for (int r = 0; r < height; ++r) {
        uint32_t s = (src + (uint32_t)(srcStride * r)) - slo;
        uint32_t d = (dst + (uint32_t)(dstStride * r)) - dlo;
        std::memcpy(dbuf.data() + d, sbuf.data() + s, rowBytes);
    }

    m.write(dlo, dbuf.data(), dhi - dlo);
    return 0;
}

// ---- LFB16_PutBitmap8 / _AMask -----------------------------------------------
// Args: src(byte idx), dst, palette, width, height, srcAddBytes, dstAddBytes.
// Indexed 8->16 palette copy, one source byte per pixel. PutBitmap8 writes every
// pixel; the _AMask variant skips index 0 (leaving dst untouched → transparency).
template <bool Mask>
uint32_t blit_putbitmap8_impl(Machine& m, uint32_t esp) {
    uint32_t src    = m.r32(esp + 4);
    uint32_t dst    = m.r32(esp + 8);
    uint32_t pal    = m.r32(esp + 12);
    int32_t  width  = (int32_t)m.r32(esp + 16);
    int32_t  height = (int32_t)m.r32(esp + 20);
    int32_t  srcAdd = (int32_t)m.r32(esp + 24);
    int32_t  dstAdd = (int32_t)m.r32(esp + 28);
    if (width <= 0 || height <= 0) return 0;

    int32_t  srcStride = width + srcAdd;             // 1 byte per pixel
    int32_t  dstStride = width * 2 + dstAdd;
    uint32_t slo, shi, dlo, dhi;
    span_bounds(src, srcStride, (uint32_t)width, height, slo, shi);
    span_bounds(dst, dstStride, (uint32_t)width * 2, height, dlo, dhi);

    static std::vector<uint8_t> sbuf, dbuf;
    sbuf.resize(shi - slo);
    dbuf.resize(dhi - dlo);
    m.read(slo, sbuf.data(), shi - slo);
    m.read(dlo, dbuf.data(), dhi - dlo);             // preserve gaps / skipped px
    Span dstv{dlo, &dbuf};

    static Palette pal_cache;
    pal_cache.begin(&m, pal);

    for (int r = 0; r < height; ++r) {
        const uint8_t* s = sbuf.data() + ((src + (uint32_t)(srcStride * r)) - slo);
        uint32_t dp = dst + (uint32_t)(dstStride * r);
        for (int x = 0; x < width; ++x) {
            uint8_t idx = s[x];
            if (!Mask || idx != 0) dstv.w16(dp, pal_cache[idx]);
            dp += 2;
        }
    }

    m.write(dlo, dbuf.data(), dhi - dlo);
    return 0;
}
uint32_t blit_putbitmap8(Machine& m, uint32_t, uint32_t esp) {
    return blit_putbitmap8_impl<false>(m, esp);
}
uint32_t blit_putbitmap8_amask(Machine& m, uint32_t, uint32_t esp) {
    return blit_putbitmap8_impl<true>(m, esp);
}

// ---- LFB16_VLineAlfa ---------------------------------------------------------
// Args: dst, count, color565, dstAddBytes. 50%-per-channel RGB565 blend of
// `color` into each of `count` pixels down a column.
static inline uint16_t blend565_half(uint16_t a, uint16_t b) {
    uint16_t r = ((((a >> 11) & 0x1f) + ((b >> 11) & 0x1f)) >> 1) & 0x1f;
    uint16_t g = ((((a >> 5) & 0x3f) + ((b >> 5) & 0x3f)) >> 1) & 0x3f;
    uint16_t bl = (((a & 0x1f) + (b & 0x1f)) >> 1) & 0x1f;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint32_t blit_vline_alfa(Machine& m, uint32_t /*slot*/, uint32_t esp) {
    uint32_t dst    = m.r32(esp + 4);
    int32_t  count  = (int32_t)m.r32(esp + 8);
    uint16_t color  = (uint16_t)m.r32(esp + 12);
    int32_t  dstAdd = (int32_t)m.r32(esp + 16);
    if (count <= 0) return 0;

    uint32_t lo, hi;
    span_bounds(dst, dstAdd, 2, count, lo, hi);

    static std::vector<uint8_t> dbuf;
    dbuf.resize(hi - lo);
    m.read(lo, dbuf.data(), hi - lo);
    Span v{lo, &dbuf};

    uint32_t dp = dst;
    for (int i = 0; i < count; ++i) {
        v.w16(dp, blend565_half(v.r16(dp), color));
        dp = (uint32_t)((int32_t)dp + dstAdd);
    }

    m.write(lo, dbuf.data(), hi - lo);
    return 0;
}
}  // namespace

void install_native_blit(Machine& m, uint32_t mvos_base) {
    const char* dis = std::getenv("THEOC_NATIVE_BLIT");
    if (dis && dis[0] == '0') {
        std::fprintf(stderr, "[blit] native blit DISABLED (THEOC_NATIVE_BLIT=0); "
                             "using emulated libmvos rasterizer\n");
        return;
    }
    // Entry-point code hooks: a jump to the function entry fires our handler,
    // which reads args off the guest stack and returns via code_hook's cdecl ret
    // (pop return addr, args cleaned by caller) — the real libmvos body never
    // runs. map_region=false: libmvos is already mapped.
    m.add_code_traps(mvos_base + OFF_PutBitmap8C1Mask, 1, blit_8c1_amask, false);
    m.add_code_traps(mvos_base + OFF_PutBitmap,        1, blit_putbitmap, false);
    m.add_code_traps(mvos_base + OFF_PutBitmap8,       1, blit_putbitmap8, false);
    m.add_code_traps(mvos_base + OFF_PutBitmap8AMask,  1, blit_putbitmap8_amask, false);
    m.add_code_traps(mvos_base + OFF_VLineAlfa,        1, blit_vline_alfa, false);
    std::fprintf(stderr, "[blit] native LFB16 blit overrides installed "
                         "(PutBitmap8C1_AMask, PutBitmap8, PutBitmap8_AMask, "
                         "PutBitmap, VLineAlfa)\n");
}
