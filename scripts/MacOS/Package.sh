#!/bin/bash
# Package a distributable build: binaries + content pak (+ loose Resources for the editor).
# Output: dist/DesertEngine-<config>/ — CI archives this directory as a downloadable artifact.
#
#   ./scripts/MacOS/Package.sh [Release|Debug]
#
# Content ships BOTH ways on purpose:
#   - Content.dpak (built with the same PakTool the Runtime mounts) — the packaged-game path;
#   - loose Resources/ — the editor's dev path and the VFS's loose-file override for debugging.
# Updates later: keep Content.manifest (written beside the pak below, 0.076 % of its size), then for
# the next release build the new pak and run
#     PakTool patch <old Content.manifest> <new Content.dpak> Patch_001.dpak
# — the Runtime mounts Patch*.dpak on top of the base automatically, and the patch carries the list of
# files the release DELETED as well as the ones it changed. Keeping the manifest is what makes that
# possible without keeping the old 318 MB archive.
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

# The manifest of THIS release, beside it. Written at package time because it cannot be written later:
# a manifest of a version can only be recorded while that version exists. It is what the next release's
# `PakTool patch` compares against, and it is ~0.076 % of the archive, so keeping one per version for
# ever costs nothing while keeping one 318 MB archive per version does not.
"$BIN/PakTool" manifest "$OUT/Content.dpak" "$OUT/Content.manifest"

# Loose copy for the editor + debugging override.
cp -R "$ROOT/Editor/Resources" "$OUT/Resources"

echo "Package.sh: packaged -> $OUT"
du -sh "$OUT"
