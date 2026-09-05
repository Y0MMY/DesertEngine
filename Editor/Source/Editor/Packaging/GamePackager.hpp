#pragma once

#include <string>

namespace Desert::Editor
{
    struct PackageOptions
    {
        std::string OutputDir = "Build/Output"; // relative to the editor cwd, or absolute
        std::string Config    = "Release";      // which Runtime binary to bundle: "Debug" | "Release"
        // macOS: produce <Name>.app (launcher + Info.plist + MoltenVK/loader inside Contents/Frameworks
        // so the player machine needs no Homebrew). false -> plain folder + run.sh.
        bool MacAppBundle = true;
    };

    struct PackageResult
    {
        bool        Success = false;
        std::string Message;    // human-readable summary / error
        std::string PackageDir; // the produced game folder (valid on success)
    };

    // Bakes the CURRENTLY OPEN project into a self-contained game (.app bundle by default, plain
    // folder otherwise):
    //
    //   Runtime (launcher)  — sets the Vulkan env, cds to the content dir, execs the player binary
    //   <Name>.deproj       — regenerated: AssetsRoot "Assets", DefaultScene rebased
    //   Content.dpak        — ALL content in one archive, tree by tree out of the shared census in
    //                         PackagedContentTrees.hpp: project assets (raw mesh sources stripped —
    //                         the runtime reads cooked meshes only), the cooked cache, and the engine
    //                         shaders, fonts and icons. The Runtime mounts it at startup and every
    //                         content read resolves through the VFS.
    //   Contents/Frameworks — (bundle only) MoltenVK + the Vulkan loader, so the player machine
    //                         needs no Homebrew; falls back to the target's Homebrew when the local
    //                         artifacts are absent.
    //
    // Pure CPU + filesystem — safe to run on a JobSystem worker.
    PackageResult PackageGame( const PackageOptions& options );

    // Rebuilds ONLY the content archive (no Runtime copy, no bundle) — written next to the .deproj so
    // the standalone Runtime can mount it for the CURRENT dev project. Loose files still override pak
    // entries (disk-first VFS), so a stale archive can never shadow fresh edits in dev.
    PackageResult BuildContentPak();
} // namespace Desert::Editor
