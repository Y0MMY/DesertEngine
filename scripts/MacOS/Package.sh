#!/bin/bash
# Package a distributable build: binaries + content pak (+ loose Resources for the editor).
# Output: dist/DesertEngine-<config>/ — CI archives this directory as a downloadable artifact.
#
#   ./scripts/MacOS/Package.sh [Release|Debug]
#
# Content ships BOTH ways on purpose:
#   - Content.dpak (built with the same PakTool the Runtime mounts) — the packaged-game path;
#   - loose Resources/ — the editor's dev path and the VFS's loose-file override for debugging.
# Updates later: build a new pak and `PakTool diff old new Patch_001.dpak` — the Runtime mounts
# Patch*.dpak on top of the base automatically.
set -euo pipefail

CONFIG="${1:-Release}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT/build/Bin/$CONFIG"
OUT="$ROOT/dist/DesertEngine-$CONFIG"

if [ ! -x "$BIN/Runtime" ]; then
    echo "Package.sh: no $CONFIG binaries in $BIN — build first" >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT"

for exe in Editor Runtime ProjectHub PakTool DShaderTool; do
    [ -x "$BIN/$exe" ] && cp "$BIN/$exe" "$OUT/"
done

# One content pak with everything the editor/runtime reads (keys keep the "Resources/" prefix so
# reads relative to the package root resolve through the VFS unchanged).
"$BIN/PakTool" create "$OUT/Content.dpak" "$ROOT/Editor/Resources" --prefix Resources

# Loose copy for the editor + debugging override.
cp -R "$ROOT/Editor/Resources" "$OUT/Resources"

echo "Package.sh: packaged -> $OUT"
du -sh "$OUT"
