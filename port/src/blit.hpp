// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
// Native overrides for the hot libmvos software rasterizer (cGD_LFB16).
//
// Profiling province view (THEOC_PROFILE=1) showed ~80% of guest CPU inside
// three LFB16 leaf blitters, emulated pixel-by-pixel through Unicorn. These are
// tiny, self-contained pixel loops; running them natively (host speed) instead
// of emulated is the "incremental native-override" seam — the real libmvos body
// is bypassed by an entry-point code hook and a byte-exact C++ reimplementation
// runs instead, reading/writing the same guest memory.
//
// Set THEOC_NATIVE_BLIT=0 to disable (falls back to the emulated originals) for
// A/B comparison with the profiler.
#pragma once
#include <cstdint>

class Machine;

// Install entry-point overrides for LFB16_PutBitmap / _VLineAlfa /
// _PutBitmap8C1_AMask. `mvos_base` is guestlink::MVOS_BASE (0x10000000); the
// runtime address of each function is mvos_base + its ELF/file offset.
void install_native_blit(Machine& m, uint32_t mvos_base);
