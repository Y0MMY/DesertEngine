#!/usr/bin/env bash
# Launch the Desert Editor built by BuildMacOS.sh.
#
# Usage: scripts/MacOS/RunEditor.sh [Debug|Release] [editor args...]
#        (everything after the config is forwarded to the Editor, e.g. --project <path>)
set -euo pipefail

cd "$(dirname "$0")/../.."

CONFIG="${1:-Debug}"
if [ $# -gt 0 ]; then shift; fi
EDITOR="build/Bin/$CONFIG/Editor"

if [ ! -x "$EDITOR" ]; then
    echo "$EDITOR not found — build first: scripts/MacOS/BuildMacOS.sh $CONFIG" >&2
    exit 1
fi

# Make sure the Vulkan loader finds the MoltenVK ICD and validation layers from
# Homebrew even when the environment doesn't provide them.
BREW_PREFIX="${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$BREW_PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json}"
export VK_LAYER_PATH="${VK_LAYER_PATH:-$BREW_PREFIX/share/vulkan/explicit_layer.d}"

# GLFW loads the Vulkan loader with dlopen("libvulkan.1.dylib"), and dyld does
# not search the Homebrew prefix by default.
export DYLD_FALLBACK_LIBRARY_PATH="$BREW_PREFIX/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"

# The engine resolves Resources/... relative to the working directory.
cd Editor

# The editor REQUIRES a project (--project <.deproj>); picking projects is the Project Hub's job.
# With no extra args, fall back to the built-in sandbox project (the historical Resources/Assets tree).
if [ $# -eq 0 ]; then
    set -- --project Desert.deproj
fi

exec "../$EDITOR" "$@"
