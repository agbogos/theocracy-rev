#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#if defined(_WIN32)
#include <stdlib.h>   // _putenv_s
#endif

namespace config {
namespace {

// Knobs that are NOT settings — they replace running the game, so a stale line
// in a config file means "the game never starts" with no visible cause, and the
// person hitting it is the least equipped to work out why. Environment only.
// THEOC_TREE is here for a different reason: it lives in port/src/mvos.cpp,
// which CMakeLists has commented out of the target, so it does nothing at all.
const std::set<std::string> kFileRefused = {
    "THEOC_FIX_SAVE",    // repairs a save and exits
    "THEOC_HEAP_TEST",   // runs the allocator self-test and exits
    "THEOC_SERVER",      // boots the dedicated server instead of the game
    "THEOC_TREE",        // dead: lives in the unbuilt legacy MVOS layer
};

// Every knob the file may set. This exists ONLY to catch typos — a name that is
// not here is still applied, with a warning, so adding a knob to the code and
// forgetting this list degrades to a cosmetic message rather than a setting
// that silently does nothing. Keep it in step with docs/porting/diagnostics.md;
// `grep -rhoE 'getenv\("THEOC_[A-Z0-9_]+"\)' port/src/` prints the real set.
const std::set<std::string> kKnown = {
    // player
    "THEOC_FULLSCREEN", "THEOC_SCANLINES", "THEOC_NO_HIDPI", "THEOC_FRAME_MS",
    "THEOC_PROVINCE_MS", "THEOC_MUSIC_VOL", "THEOC_AUDIO_MS", "THEOC_SKIP_MOVIES",
    "THEOC_VIDEO_HOLD",
    // paths
    "THEOC_DATA", "THEOC_CD", "THEOC_CD_AUDIO",
    // enthusiast
    "THEOC_CONSOLE", "THEOC_EDIT", "THEOC_REAL_LOCK",
    // compatibility escape hatches
    "THEOC_LEGACY_CURSOR", "THEOC_LEGACY_KEYMB", "THEOC_LEGACY_SCALE",
    "THEOC_LEGACY_SLEEP", "THEOC_LEGACY_SPRITE", "THEOC_NATIVE_BLIT",
    "THEOC_NO_SAVE_FIX", "THEOC_START_ANYWAY",
    // diagnostics
    "THEOC_FPS", "THEOC_WATCHDOG", "THEOC_WATCHDOG_SAMPLE", "THEOC_SLOWLOG",
    "THEOC_PROFILE", "THEOC_TRACE", "THEOC_KEYLOG", "THEOC_CD_TRACE",
    "THEOC_REPORT_CLICKS", "THEOC_LOUD_ABORT", "THEOC_ABORT_CAP",
    "THEOC_START_SEC", "THEOC_LONGRUN",
    // test harness
    "THEOC_AUTO_KEYS", "THEOC_AUTO_MENU", "THEOC_AUTO_PROVINCE", "THEOC_CLICKS",
    "THEOC_MOUSE_SWEEP", "THEOC_SOAK", "THEOC_SOAK_PLAY", "THEOC_SHOT_EVERY",
    "THEOC_SHOT_DIR",
};

std::string trim(std::string s) {
    auto sp = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && sp((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && sp((unsigned char)s.back())) s.pop_back();
    return s;
}

// Strip a trailing comment. `#` only starts one at the beginning of the value or
// after whitespace, so a value that legitimately contains a hash (a path, a
// colour) survives; quoting protects it in every case.
std::string strip_comment(const std::string& v) {
    if (!v.empty() && (v[0] == '"' || v[0] == '\'')) {
        char q = v[0];
        auto end = v.find(q, 1);
        if (end != std::string::npos) return v.substr(1, end - 1);
        return v.substr(1);
    }
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == '#' && (i == 0 || std::isspace((unsigned char)v[i - 1])))
            return trim(v.substr(0, i));
    return trim(v);
}

void set_if_unset(const std::string& k, const std::string& v) {
#if defined(_WIN32)
    if (std::getenv(k.c_str()) != nullptr) return;
    _putenv_s(k.c_str(), v.c_str());
#else
    ::setenv(k.c_str(), v.c_str(), /*overwrite=*/0);
#endif
}

std::string dir_of(const char* argv0) {
    if (!argv0 || !*argv0) return "";
    std::string p = argv0;
    auto slash = p.find_last_of("/\\");
    return slash == std::string::npos ? "" : p.substr(0, slash);
}

bool readable(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

// Parse and apply. Returns the number of settings taken from the file.
int apply(const std::string& path) {
    std::ifstream f(path);
    if (!f) return 0;
    std::string line;
    int lineno = 0, applied = 0, refused = 0, overridden = 0;
    while (std::getline(f, line)) {
        lineno++;
        // A UTF-8 BOM on line 1 would otherwise become part of the first key —
        // Notepad writes one by default, and this file is meant to be edited by
        // people using whatever editor they have.
        if (lineno == 1 && line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);
        std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        // [section] headers are accepted and ignored: mvos.cfg has them, so
        // someone copying its shape should not be punished for the habit.
        if (s.front() == '[' && s.back() == ']') continue;

        auto eq = s.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "  [cfg] %s:%d: no '=', ignored: %s\n",
                        path.c_str(), lineno, s.c_str());
            continue;
        }
        std::string key = trim(s.substr(0, eq));
        std::string val = strip_comment(trim(s.substr(eq + 1)));
        for (auto& c : key) c = (char)std::toupper((unsigned char)c);
        if (key.empty()) continue;

        if (kFileRefused.count(key)) {
            std::fprintf(stderr,
                        "  [cfg] %s:%d: %s cannot be set from a config file "
                        "(it replaces running the game) — ignored\n",
                        path.c_str(), lineno, key.c_str());
            refused++;
            continue;
        }
        if (key.rfind("THEOC_", 0) != 0) {
            std::fprintf(stderr, "  [cfg] %s:%d: not a THEOC_ setting, ignored: %s\n",
                        path.c_str(), lineno, key.c_str());
            continue;
        }
        if (!kKnown.count(key))
            std::fprintf(stderr, "  [cfg] %s:%d: unknown setting %s — applying "
                                 "anyway; check the spelling against "
                                 "docs/porting/diagnostics.md\n",
                        path.c_str(), lineno, key.c_str());
        // An empty value means "leave it unset", not "set it to empty" — most
        // knobs are presence-tested, so `THEOC_FPS =` would otherwise turn the
        // instrument ON, which is the opposite of what anyone writing that means.
        if (val.empty()) continue;
        if (std::getenv(key.c_str()) != nullptr) { overridden++; continue; }
        set_if_unset(key, val);
        applied++;
    }
    if (refused || overridden)
        std::fprintf(stderr, "  [cfg] %d applied, %d overridden by the "
                             "environment, %d refused\n",
                    applied, overridden, refused);
    return applied;
}

}  // namespace

std::string load(const char* argv0) {
    if (const char* explicit_path = std::getenv("THEOC_CONFIG")) {
        if (!readable(explicit_path)) {
            std::fprintf(stderr, "  [cfg] THEOC_CONFIG='%s' cannot be read — "
                                 "continuing with defaults\n", explicit_path);
            return "";
        }
        int n = apply(explicit_path);
        std::fprintf(stderr, "  [cfg] %s: %d settings\n", explicit_path, n);
        return explicit_path;
    }
    std::string beside = dir_of(argv0);
    if (!beside.empty()) beside += "/theoc.cfg";
    for (const std::string& cand : {beside, std::string("theoc.cfg")}) {
        if (cand.empty() || !readable(cand)) continue;
        int n = apply(cand);
        std::fprintf(stderr, "  [cfg] %s: %d settings\n", cand.c_str(), n);
        return cand;
    }
    return "";
}

}  // namespace config
