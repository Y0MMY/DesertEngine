#!/usr/bin/env bash
# Launch the Desert Project Hub (standalone launcher; picks/creates a project, then starts the Editor).
#
# Usage: scripts/MacOS/RunProjectHub.sh [Debug|Release]
set -euo pipefail

cd "$(dirname "$0")/../.."

CONFIG="${1:-Debug}"
HUB="build/Bin/$CONFIG/ProjectHub"

if [ ! -x "$HUB" ]; then
    echo "$HUB not found — build first: scripts/MacOS/BuildMacOS.sh $CONFIG" >&2
    exit 1
fi

# The hub launches the Editor through scripts/MacOS/RunEditor.sh — tell it where the repo lives and
# which configuration to start.
export DESERT_ROOT="$PWD"
export DESERT_CONFIG="$CONFIG"

exec "$HUB"
