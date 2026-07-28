#!/usr/bin/env bash
# Generate project files (premake5 gmake2) and build Desert Engine on macOS.
#
# Usage:
#   scripts/MacOS/BuildMacOS.sh [Debug|Release] [--with-tests] [--gen-only]
#
# Examples:
#   scripts/MacOS/BuildMacOS.sh              # Debug build
#   scripts/MacOS/BuildMacOS.sh Release      # Release build
#   scripts/MacOS/BuildMacOS.sh Debug --with-tests
set -euo pipefail

cd "$(dirname "$0")/../.."

CONFIG="Debug"
PREMAKE_ARGS=()
GEN_ONLY=0

for arg in "$@"; do
    case "$arg" in
        Debug|Release) CONFIG="$arg" ;;
        --with-tests)  PREMAKE_ARGS+=("--with-tests") ;;
        --gen-only)    GEN_ONLY=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

if ! command -v premake5 >/dev/null 2>&1; then
    echo "premake5 not found. Run scripts/MacOS/Setup.sh first." >&2
    exit 1
fi

echo "--- Generating Makefiles (premake5 gmake2)"
premake5 gmake2 "${PREMAKE_ARGS[@]+"${PREMAKE_ARGS[@]}"}"

if [ "$GEN_ONLY" -eq 1 ]; then
    exit 0
fi

# gmake2 configs are lowercase
MAKE_CONFIG="$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')"
CORES="$(sysctl -n hw.ncpu)"

echo "--- Building ($CONFIG, -j$CORES)"
make config="$MAKE_CONFIG" -j"$CORES"

echo ""
echo "=== Build complete: build/Bin/$CONFIG ==="
