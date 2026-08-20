// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
// Dual-image guest linker: theocracy.real (ET_EXEC) + libmvos.so (ET_DYN).
// Maps both into Unicorn, applies i386 relocs, resolves game↔engine symbols,
// and routes remaining UND (libc / SMPEG / dl) to the trap HLE layer.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "elf32.hpp"
#include "machine.hpp"
#include "traps.hpp"

namespace guestlink {

// libmvos load base — well clear of the game image (~0x08048000–0x0869xxxx)
// and of the host trap/heap/stack windows (0x50–0x79xxxxxx).
constexpr uint32_t MVOS_BASE = 0x10000000;

struct LinkResult {
    // Absolute guest addresses of key symbols (0 if missing).
    uint32_t init_app = 0;     // Init__12cApplication
    uint32_t start_app = 0;    // Start__12cApplicationiPPc
    uint32_t mvos_init = 0;    // DT_INIT of libmvos (_init)
    uint32_t game_ctors = 0;   // .ctors section addr (game)
    uint32_t game_ctors_n = 0; // word count
    uint32_t mvos_ctors = 0;
    uint32_t mvos_ctors_n = 0;

    uint32_t game_imports_to_mvos = 0;
    uint32_t game_imports_to_hle = 0;
    uint32_t mvos_imports_to_game = 0;
    uint32_t mvos_imports_to_hle = 0;
    uint32_t copies = 0;
    uint32_t relocs_applied = 0;

    // HLE trap layer for unresolved UND (libc / SMPEG / dl / …). Owned here so
    // the code-hook lambda can bind to a stable TrapLayer*.
    std::unique_ptr<TrapLayer> traps;
};

// Map both images, link them, install HLE traps for unresolved UND symbols.
// On return the machine is ready for stack/heap setup + running constructors.
LinkResult link(Machine& m, const elf32::Image& game, const elf32::Image& mvos);

// Absolute address of a defined symbol in an already-linked image, or 0.
// For DYN, pass the load base used at link time.
uint32_t abs_sym(const elf32::Image& img, uint32_t load_base, const std::string& name);

}  // namespace guestlink
