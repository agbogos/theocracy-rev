// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
#pragma once

#include <cstdio>

// Host log levels.
//
// The port printed 65 lines before the dedicated server reached its first
// socket, and every one of them was on for every user. That output is genuinely
// valuable — it is how the guest link, the relocation passes and the subsystem
// bring-up were debugged in the first place, and it is why a bug report from a
// tester has usually already answered the first three questions. But it is
// developer output, and a player running a 26-year-old strategy game should not
// have to scroll past a relocation count to see that their save loaded.
//
// So the default is quiet and THEOC_VERBOSE brings it all back. Nothing is
// deleted; the same lines print, behind a knob.
//
// WHAT STAYS UNCONDITIONAL, and why the list is short but not empty
// ----------------------------------------------------------------
//   * The banner. It names the build, and a tester who sends a log has then
//     already told us which binary they ran without being asked — the only
//     version-reporting scheme that survives contact with non-developers. See
//     docs/porting/diagnostics.md, "The first line names the build".
//   * Errors and warnings. Anything the user can act on, or that explains
//     something they will otherwise experience as a mystery: a missing file, a
//     save that could not be repaired, audio that will not play.
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
