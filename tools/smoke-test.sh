#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
#
# Assert that a built `theoc` binary carries the build identity of the commit it
# was built from — by running it and reading what it writes, not by reading a
# build log.
#
#   tools/smoke-test.sh <path-to-theoc> [expected-date] [expected-commit]
#
# Why this exists: three defects shipped in v1.0.0 bundles that all passed a
# green build (see docs/porting/other-os-ports.md). A build log says the compiler
# was happy; it does not say the artefact is the one you think it is. The
# arm64 bundle shipped stamp 000000/00000000 and nothing noticed.
#
# THEOC_FIX_SAVE is the lever: it runs the save-repair path and exits without
# booting the game — no display, no data tree, no copyrighted files. That path
# calls set_build_identity() first, so the repaired file carries the stamp.
# Requiring no game data is what makes this runnable in CI at all.
#
# Note that collapse_save_file() returns void and swallows every error, so the
# exit code proves nothing on its own. The assertion is on the bytes it wrote.
set -e

BIN=${1:?usage: tools/smoke-test.sh <path-to-theoc> [date] [commit]}
[ -x "$BIN" ] || { echo "smoke: not executable: $BIN" >&2; exit 1; }

# Default expectations come from git the same way port/CMakeLists.txt derives
# them, so a mismatch means the binary disagrees with the tree it was built
# from. Overridable for a tarball build with no git present.
DATE=${2:-$(git log -1 --format=%cd --date=format:%y%m%d)}
COMMIT=${3:-}
if [ -z "$COMMIT" ]; then
  COMMIT=$(git rev-parse --short=7 HEAD | cut -c1-7)
  # Tracked files only, matching `git describe --dirty` and CMakeLists.
  if git diff --quiet HEAD -- 2>/dev/null; then COMMIT="${COMMIT}0"; else COMMIT="${COMMIT}+"; fi
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
SAVE="$WORK/smoke.tsg"

# A .tsg-shaped fixture, synthesised: 0x48-byte header, "theosg42" magic at
# 0x40, a NUL-terminated save name leaving room for the 22-byte stamp window.
# Nothing copyrighted — the layout is documented in tools/fix_save.py, and the
# body is zeroed so the run scanner rejects it as flat fill and leaves the
# structure alone. Only the header path is under test.
python3 - "$SAVE" <<'PY'
import sys
h = bytearray(b"\xaa" * 0x40)
h[0:6] = b"smoke\x00"            # save name + terminator; window starts at 6
h += b"theosg42"                 # magic at 0x40
h += bytes(16)                   # flat body: nothing for the collapser to touch
open(sys.argv[1], "wb").write(bytes(h))
PY

THEOC_FIX_SAVE="$SAVE" "$BIN" >"$WORK/out" 2>&1 || {
  echo "smoke: binary exited $? — output follows" >&2; cat "$WORK/out" >&2; exit 1; }

python3 - "$SAVE" "$DATE" "$COMMIT" <<'PY'
import sys
path, date, commit = sys.argv[1], sys.argv[2], sys.argv[3]
d = open(path, "rb").read()
if len(d) < 0x48 or d[0x40:0x48] != b"theosg42":
    sys.exit("smoke: FAIL — fixture came back malformed; the binary mangled it")
nul = d.find(b"\0", 0, 0x40)
window = d[nul + 1:0x40].rstrip(b"\0").decode("ascii", "replace")
want = "8f4b" + date + commit
if not window:
    sys.exit("smoke: FAIL — stamp window is empty; the binary wrote no identity")
if not window.startswith(want):
    sys.exit(f"smoke: FAIL — stamp mismatch\n  binary says: {window}\n  git says:    {want}...")
print(f"smoke: OK — {window}  (date {date}, commit {commit})")
PY
