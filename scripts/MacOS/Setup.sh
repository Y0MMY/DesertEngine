#!/usr/bin/env bash
# One-time environment setup for building Desert Engine on macOS (Apple Silicon).
# Installs every build dependency and fetches the third-party pieces that are
# not part of the repo. Safe to re-run: everything is idempotent.
set -euo pipefail

cd "$(dirname "$0")/../.."
ROOT="$(pwd)"

echo "=== Desert Engine macOS setup ==="

# ---------------------------------------------------------------------------
# 1. Homebrew packages: build tools + Vulkan (MoltenVK) + shader toolchain
# ---------------------------------------------------------------------------
if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is required. Install it from https://brew.sh and re-run this script." >&2
    exit 1
fi

BREW_PACKAGES=(
    premake                 # project generator (premake5)
    cmake                   # used by some third-party builds
    vulkan-headers          # <vulkan/vulkan.h>
    vulkan-loader           # libvulkan.dylib
    molten-vk               # Vulkan-over-Metal driver (ICD)
    vulkan-validationlayers # VK_LAYER_KHRONOS_validation (Debug builds)
    shaderc                 # libshaderc_combined.a — runtime GLSL compilation
    spirv-cross             # SPIR-V reflection/translation
    assimp                  # model importing (Editor, FbxMeshSplitter)
    googletest              # unit tests (optional, --with-tests builds)
)

echo "--- Installing Homebrew packages: ${BREW_PACKAGES[*]}"
for pkg in "${BREW_PACKAGES[@]}"; do
    if brew list --formula "$pkg" >/dev/null 2>&1; then
        echo "    $pkg: already installed"
    else
        brew install "$pkg"
    fi
done

# ---------------------------------------------------------------------------
# 2. Git submodules
# ---------------------------------------------------------------------------
echo "--- Initializing git submodules"
git submodule update --init --recursive

# ---------------------------------------------------------------------------
# 3. Optick profiler sources (ThirdParty/optick is not a submodule)
# ---------------------------------------------------------------------------
if [ ! -f "$ROOT/ThirdParty/optick/src/optick.h" ]; then
    echo "--- Cloning Optick into ThirdParty/optick"
    rm -rf "$ROOT/ThirdParty/optick"
    git clone --depth 1 https://github.com/bombomby/optick.git "$ROOT/ThirdParty/optick"
else
    echo "--- Optick sources present"
fi

# ---------------------------------------------------------------------------
# 3b. volk (Vulkan meta-loader) headers — shipped by the LunarG SDK on Windows,
#     vendored here since Homebrew has no formula for it
# ---------------------------------------------------------------------------
if [ ! -f "$ROOT/ThirdParty/volk/volk.h" ]; then
    echo "--- Cloning volk into ThirdParty/volk"
    rm -rf "$ROOT/ThirdParty/volk"
    git clone --depth 1 https://github.com/zeux/volk.git "$ROOT/ThirdParty/volk"
else
    echo "--- volk headers present"
fi

# ---------------------------------------------------------------------------
# 3c. meshoptimizer (mesh simplification / LOD generation) — not a submodule
# ---------------------------------------------------------------------------
if [ ! -f "$ROOT/ThirdParty/meshoptimizer/src/meshoptimizer.h" ]; then
    echo "--- Cloning meshoptimizer into ThirdParty/meshoptimizer"
    rm -rf "$ROOT/ThirdParty/meshoptimizer"
    git clone --branch v0.20 --depth 1 https://github.com/zeux/meshoptimizer.git "$ROOT/ThirdParty/meshoptimizer"
else
    echo "--- meshoptimizer sources present"
fi

# ---------------------------------------------------------------------------
# 4. reflect-cpp compiled sources. The vendored include/ headers are reflect-cpp
#    v0.19.0 and need rfl::Generic / rfl::json / yyjson built from the matching
#    sources (Windows uses the prebuilt reflectcpp.lib instead).
# ---------------------------------------------------------------------------
RCPP_SRC="$ROOT/ThirdParty/reflect-cpp/src"
if [ ! -f "$RCPP_SRC/reflectcpp.cpp" ]; then
    echo "--- Fetching reflect-cpp v0.19.0 sources"
    TMP_RCPP="$(mktemp -d)"
    git clone --branch v0.19.0 --depth 1 https://github.com/getml/reflect-cpp "$TMP_RCPP"
    mkdir -p "$RCPP_SRC"
    cp "$TMP_RCPP/src/reflectcpp.cpp" "$TMP_RCPP/src/reflectcpp_json.cpp" "$TMP_RCPP/src/yyjson.c" "$RCPP_SRC/"
    cp -R "$TMP_RCPP/src/rfl" "$RCPP_SRC/"
    rm -rf "$TMP_RCPP"
else
    echo "--- reflect-cpp sources present"
fi

echo ""
echo "=== Setup complete ==="
echo "Build with:  scripts/MacOS/BuildMacOS.sh [Debug|Release]"
