// theoc.cfg — permanent settings for release bundles.
//
// Every runtime knob is a THEOC_* environment variable (the canonical list is
// docs/porting/diagnostics.md). That is fine for development and useless for a
// player who wants a setting to stick, so this reads the same names out of a
// file at startup and puts them in the environment before anything looks.
//
// Deliberately not TOML/YAML/JSON. The knobs are a flat list of scalars, so a
// structured format would buy nothing and cost either a vendored dependency or
// a parser that implements 5% of a spec and claims the whole thing. The format
// is the one the game's own mvos.cfg already uses — KEY = value, # comments —
// which is also what a player is most likely to guess right.
//
// Rules:
//   * The environment always wins. A one-off `THEOC_X=1 ./theoc` overrides the
//     file, so the file can never make a command line lie.
//   * No file, or an unreadable one, means exactly today's behaviour. This must
//     never become a new way for the game to fail to start.
//   * A few knobs are refused from the file — see kFileRefused in config.cpp.
#pragma once
#include <string>

namespace config {

// Locate and apply theoc.cfg. Search order, first hit wins:
//   1. $THEOC_CONFIG (a file path; if set and unreadable, that is reported)
//   2. theoc.cfg next to the executable, derived from argv0
//   3. ./theoc.cfg
// Returns the path loaded, or "" if none was found. Never throws.
std::string load(const char* argv0);

}  // namespace config
