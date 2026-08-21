// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
#pragma once

#include <cstdio>

// Host log levels.
//
// The default is quiet and THEOC_VERBOSE enables debug logging.
//
// What prints regardless:
// ----------------------------------------------------------------
//   * The banner with build info. Useful in non-developer testing.
//   * Errors and warnings.
//
// Everything else is LOG_V. When in doubt the answer is LOG_V, because the cost
// of hiding a line is one environment variable and the cost of keeping it is
// paid by every user on every run.
namespace logging {

// 0 = quiet (default), 1 = the pre-2026-08-21 output, 2 = that plus per-frame
// and per-trap detail that was already behind its own knobs.
extern int level;

// Reads THEOC_VERBOSE once. Call before the banner: config::load() runs after
// it and may set THEOC_VERBOSE from theoc.cfg, so the level is re-read there.
void init();

}  // namespace logging

// Verbose: the boot chatter. Compiled in always — this is not a hot path, the
// branch is a predictable load-and-compare, and being able to ask a user for a
// verbose log without shipping them a different binary is the entire point.
#define LOG_V(...)                                                    \
    do {                                                              \
        if (logging::level >= 1) std::fprintf(stderr, __VA_ARGS__);   \
    } while (0)

#define LOG_VV(...)                                                   \
    do {                                                              \
        if (logging::level >= 2) std::fprintf(stderr, __VA_ARGS__);   \
    } while (0)
