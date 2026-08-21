// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
#include "log.hpp"

#include <cstdlib>

namespace logging {

int level = 0;

void init() {
    const char* v = std::getenv("THEOC_VERBOSE");
    if (!v || !*v) { level = 0; return; }
    // Any value means verbose; a number selects the depth. "0" turns it off
    // again, so THEOC_VERBOSE=0 in theoc.cfg can override an inherited one.
    char* end = nullptr;
    long n = std::strtol(v, &end, 10);
    level = (end && end != v) ? (int)n : 1;
    if (level < 0) level = 0;
}

}  // namespace logging
