#!/bin/bash
# THE ENGINE DROP — the downloadable build of the TOOLS, not of a game.
# Output: dist/DesertEngine-<config>/ — CI archives this directory as an artifact (ci.yml).
#
#   ./scripts/MacOS/Package.sh [Release|Debug]
#
# WHAT THIS IS AND WHAT IT IS NOT (П5). A GAME is packaged by the editor's own PackageGame() and by
# nothing else: it needs an OPEN PROJECT, which is a thing this script does not have and CI does not
# have either, and its product is the player binary plus one archive carrying the project's content
# and its descriptor. This script's product is the EDITOR plus the tools plus the loose engine
# resources the editor reads from disk. Two disjoint jobs, and only one of them ships a game.
#
# WHY Content.dpak AND Content.manifest ARE NO LONGER WRITTEN HERE, measured 2026-09-08 on the Debug
# drop. `VFS::MountPak` has exactly ONE non-test call site in this repository —
# Runtime/Source/PackagedContent.cpp — so the editor and the tools in this directory never mount an
# archive at all, and the pak's only possible reader was the Runtime sitting beside it. That reader
# mounted its 144 MB and then refused, because a pak of Editor/Resources carries no project
# descriptor and this script has no project to describe: "No game to run", reproduced by running it.
# So the drop was 528 MB of which 144 MB was an archive nothing could use and another 133 MB was the
# SAME tree loose beside it — the one tree shipped twice, 52 % of the artifact — and the pair of them
# is what made this directory read as a second, broken way to package a game.
#
# Nothing consumed either file: the only reference anywhere is ci.yml's upload of the whole folder.
# The patch workflow they were written for is not lost — `PakTool manifest <pak> <out>` records a
# manifest of any archive, and the archive a release actually patches is a GAME's, produced by
# PackageGame. Recording one for the engine drop answered a question nobody asks.
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

# The engine resources, loose — the editor reads these off disk and mounts nothing.
cp -R "$ROOT/Editor/Resources" "$OUT/Resources"

echo "Package.sh: packaged -> $OUT"
du -sh "$OUT"
