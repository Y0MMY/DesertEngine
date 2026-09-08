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

# THE LAUNCHER DRAWS ON VULKAN NOW (L3), so it needs the same three variables RunEditor.sh exports
# and for the same reasons: the loader has to be told where the MoltenVK ICD is, and GLFW reaches
# the loader with dlopen("libvulkan.1.dylib") which dyld does not look for under the Homebrew
# prefix. Without them the hub refuses to start with "GLFW reports no Vulkan loader" — a correct
# refusal, but one nobody going through this script should ever have to read.
BREW_PREFIX="${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$BREW_PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json}"
export VK_LAYER_PATH="${VK_LAYER_PATH:-$BREW_PREFIX/share/vulkan/explicit_layer.d}"
export DYLD_FALLBACK_LIBRARY_PATH="$BREW_PREFIX/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"

# The hub launches the Editor through scripts/MacOS/RunEditor.sh — tell it where the repo lives and
# which configuration to start.
export DESERT_ROOT="$PWD"
export DESERT_CONFIG="$CONFIG"

exec "$HUB"
