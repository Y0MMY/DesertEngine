#pragma once

#include <string>

namespace Desert::Editor
{
    // EVERY FIELD HERE IS READ BY PackageGame, AND THAT IS CHECKED — Desert/Tests/Editor/
    // BuildSettingsConsumers asserts the relation in both directions: no option the Build Settings panel
    // offers that the packager ignores, and no option the packager honours that nothing can set. It was
    // written because the reverse held: the panel drew a "Target platform" chooser, `PackageOptions` had
    // no platform in it, and a Windows selection on a macOS host produced a macOS package in silence.
    //
    // There is deliberately NO target-platform field. The target is this editor's own host and cannot be
    // anything else (Editor/Packaging/PackageTarget.hpp explains why and derives the four things that
    // follow from it); a field whose only reachable value is the host would be the dead setting П6
    // removed, in a new place.
    struct PackageOptions
    {
        std::string OutputDir = "Build/Output"; // relative to the editor cwd, or absolute
        std::string Config    = "Release";      // which Runtime binary to bundle: "Debug" | "Release"
        // macOS: produce <Name>.app (launcher + Info.plist + MoltenVK/loader inside Contents/Frameworks
        // so the player machine needs no Homebrew). false -> plain folder + launcher. On a host with no
        // .app concept this is refused with a LOG_WARN and the plain layout is produced instead.
        bool MacAppBundle = true;
    };

    struct PackageResult
    {
        bool        Success = false;
        std::string Message;    // human-readable summary / error
        std::string PackageDir; // the produced game folder (valid on success)
    };

    // Bakes the CURRENTLY OPEN project into a self-contained game FOR THIS EDITOR'S OWN HOST (.app
    // bundle by default on macOS, plain folder otherwise):
    //
    //   launcher            — sets the Vulkan env where that is needed, cds to the content dir and runs
    //                         the player binary; run.sh or run.bat, whichever the host uses
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
