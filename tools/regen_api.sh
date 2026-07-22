#!/bin/sh
# Regenerate the MVOS API inventory artifacts in data/ from the binaries in linux/.
# Usage: sh tools/regen_api.sh   (run from repo root)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
L=linux/libmvos.so.0.9
G=linux/theocracy.real
DEM=tools/gnuv2_demangle.py
mkdir -p data

# 1. libmvos exports (defined dynamic symbols) -> demangled TSV
objdump -T "$L" | grep -v '\*UND\*' \
  | awk 'NF>=6 && ($2=="g"||$2=="w"){print $1, $NF}' \
  | python3 "$DEM" > data/mvos_exports.tsv

# 2. game imports (undefined symbols in the game binary) = the HLE trap boundary
objdump -T "$G" | grep '\*UND\*' | awk '{print $NF}' | grep -v '^$' | sort -u \
  | python3 "$DEM" > data/game_imports.tsv

# 3. structured inventory JSON from the engine exports
python3 tools/build_api_inventory.py < data/mvos_exports.tsv > data/mvos_api.json

# 4. C++ signature-reference header (the HLE implementation worklist)
mkdir -p include
python3 tools/gen_headers.py > include/mvos_api.hpp

echo "wrote data/mvos_exports.tsv data/game_imports.tsv data/mvos_api.json include/mvos_api.hpp"
