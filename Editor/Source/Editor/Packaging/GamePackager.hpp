#pragma once

#include <string>

namespace Desert::Editor
{
    struct PackageOptions
    {
        std::string OutputDir = "Build/Output"; // relative to the editor cwd, or absolute
        std::string Config    = "Release";      // which Runtime binary to bundle: "Debug" | "Release"
    };

    struct PackageResult
    {
        bool        Success = false;
        std::string Message;    // human-readable summary / error
        std::string PackageDir; // the produced game folder (valid on success)
    };

    // Bakes the CURRENTLY OPEN project into a self-contained game folder:
    //
    //   <OutputDir>/<Name>/
    //     Runtime              — the player binary (build/Bin/<Config>/Runtime)
    //     <Name>.deproj        — regenerated: AssetsRoot "Assets", DefaultScene rebased
    //     Assets/              — project content (raw mesh sources are STRIPPED: the runtime only
    //                            reads cooked meshes; textures/scenes/scripts/materials ship as-is)
    //     Cooked/              — the project's cooked cache (meshes/textures the runtime loads)
    //     Resources/Shaders/   — engine shaders (compiled at runtime by shaderc)
    //     Resources/Fonts/     — engine fonts
    //     run.sh               — launcher (sets the Vulkan env, cds to the folder, starts Runtime)
    //
    // Pure CPU + filesystem — safe to run on a JobSystem worker. Known v1 limitation: system
    // dependencies (MoltenVK/Vulkan loader via Homebrew) are NOT bundled; run.sh resolves them the
    // same way the dev scripts do.
    PackageResult PackageGame( const PackageOptions& options );
} // namespace Desert::Editor
