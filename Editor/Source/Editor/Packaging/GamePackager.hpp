#pragma once

#include <cstddef>
#include <string>

namespace Desert::Editor
{
    // THE PLAYER BINARY'S NAME INSIDE A .app. macOS starts whatever CFBundleExecutable names, and that
    // has to be the launcher SCRIPT (only a process started by the script inherits the Vulkan
    // environment dyld reads at image load), so the real binary cannot also be called "Runtime". This
    // is a constant rather than a literal spelled three times — the packager copies it, the launcher
    // execs it and a test has to be able to find it — and the three drifting apart produces a bundle
    // macOS reports as "damaged", which is the least diagnosable failure in the whole packager.
    inline constexpr const char* kBundlePlayerBinary = "Runtime-bin";

    // The launcher script's name inside a .app, which is also what its Info.plist declares as
    // CFBundleExecutable. Two independent literals until now, in two different std::ostringstreams
    // twenty lines apart, and their disagreement is the same "damaged application" with no further
    // explanation — the relation above, in the other direction. Not `host.LauncherName`: that is
    // `run.sh`, the name a person double-clicks in a plain folder, and a bundle's executable must be
    // extensionless.
    inline constexpr const char* kBundleLauncherName = "Runtime";

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

    // ── WHAT PACKAGING ANSWERS, AND WHY IT IS THREE ANSWERS AND NOT TWO (I12) ────────────────────────
    //
    // THE DEFECT. This carried ONE bit for THREE outcomes. A cook failure — a `.ttf` that will not bake,
    // a shader that will not compile — was counted by CookStats, logged, and then thrown away: the
    // result came back `Success == true`, and the Build Settings panel painted it the same green as a
    // clean package. Measured on a fixture whose font and icon are both unbakeable: the log said
    // "2 failure(s)" and the caller could not tell that from a package with nothing wrong with it. The
    // one non-clean case that DID reach the caller, an unwritten artifact, reached it as ENGLISH inside
    // `Message`, so the only way to ask the question was to substring-match prose. That is Ф4's shape at
    // the last step before the game reaches a player: everything that did not make it is discovered by
    // whoever RUNS the game, not by whoever built it.
    //
    // WHY `Success` IS NOT REDEFINED TO MEAN "everything cooked". Because that answer is not available:
    // this repository deliberately ships a broken shader (Resources/Shaders/Programs/Graph/MatBroken.shader)
    // so that the engine's own "registered but has no compiled stages" refusal is a reachable, tested
    // path. A packager that refused over a compile failure could not package this project at all. A
    // project may legitimately ship content that is already broken, and the runtime reports that content
    // for itself; the packager's job is to say what it shipped, not to decide the project is invalid.
    //
    // SO THE ANSWER IS STRUCTURED INSTEAD. `Success` keeps its one meaning — a package exists — and what
    // the cook could not put into it comes back as NUMBERS a caller can branch on, with `Complete()` as
    // the named third state. "Packaged" and "packaged, N artifacts will be rebuilt on the player's
    // machine at every start" are now two different values rather than one value and a sentence.
    //
    // THE TWO COUNTS STAY SEPARATE because they are separate facts, and CookStats is careful about the
    // difference for a reason: content that could not be READ is already-broken content a project may
    // choose to ship, while an artifact that was produced and could not be WRITTEN is never normal — it
    // is a hole in the shipped cache that only a player's slow startup would ever reveal. Folding them
    // into one number would throw that away.
    struct PackageResult
    {
        bool        Success = false;
        std::string Message;    // human-readable summary / error
        std::string PackageDir; // the produced game folder (valid on success)

        // Appended AFTER the three fields above rather than beside `Success`, so the fifteen
        // `return { false, "...", "" }` refusals in GamePackager.cpp keep meaning what they say.
        size_t CookFailures  = 0; // content the cook could not read/parse/compile (see CookStats)
        size_t CookUnwritten = 0; // artifacts produced that did not reach the disk

        // A package exists AND everything the cook was asked to produce is in it. This is the question
        // "did the build go green", and it is the one a caller should ask — `Success` alone answers a
        // narrower question than anybody looking at a build result means.
        bool Complete() const
        {
            return Success && CookFailures == 0 && CookUnwritten == 0;
        }
    };

    // Bakes the CURRENTLY OPEN project into a self-contained game FOR THIS EDITOR'S OWN HOST (.app
    // bundle by default on macOS, plain folder otherwise). THE PRODUCT IS A BINARY AND ONE ARCHIVE,
    // in one directory, and it starts with no arguments (П5):
    //
    //   the player binary   — the Runtime for options.Config
    //   Content.dpak        — ALL content in one archive, tree by tree out of the shared census in
    //                         PackagedContentTrees.hpp: project assets (raw mesh sources stripped —
    //                         the runtime reads cooked meshes only), the cooked cache, and the engine
    //                         shaders, fonts and icons — PLUS the regenerated descriptor at the
    //                         archive root under Project::kPackagedDescriptorName. The Runtime mounts
    //                         the archive found beside its own executable, opens that descriptor out
    //                         of it, and every content read resolves through the same mount.
    //   launcher            — run.sh / run.bat. It exists for the VULKAN ENVIRONMENT ONLY (Finder
    //                         hands a double-clicked app no VK_ICD_FILENAMES and no DYLD_*, and both
    //                         must be set before the image loads). It is not a second way to start
    //                         the game and it names no project: the binary run directly comes up too.
    //   Contents/Frameworks — (bundle only) MoltenVK + the Vulkan loader, so the player machine
    //                         needs no Homebrew; falls back to the target's Homebrew when the local
    //                         artifacts are absent.
    //
    // In a .app the binary and the archive live together in Contents/MacOS; Contents/Resources is not
    // produced. That split was what forced the launcher to pass `--project`, and it is gone with it.
    //
    // Pure CPU + filesystem — safe to run on a JobSystem worker.
    PackageResult PackageGame( const PackageOptions& options );

    // Rebuilds ONLY the content archive (no Runtime copy, no bundle) — written next to the project's
    // own .deproj so the standalone Runtime can mount it for the CURRENT dev project. Same census AND
    // same embedded descriptor as PackageGame, so the two entry points produce the same KIND of
    // archive. Loose files still override pak entries (disk-first VFS), so a stale archive can never
    // shadow fresh edits in dev.
    PackageResult BuildContentPak();
} // namespace Desert::Editor
