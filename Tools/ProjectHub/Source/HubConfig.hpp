#pragma once

// The two files in `~/.desertengine` the launcher cares about, and where that folder is.
//
// This used to live in Main.cpp's anonymous namespace, which meant the one thing a launcher does
// before it draws anything — find the user's projects and the user's engine — could only be tested
// by starting a window. Every function here takes the config directory as an argument for exactly
// that reason: a test points it at a temp folder and asserts what landed there, which is the only
// way to assert "what the launcher writes, the engine reads" without writing into the developer's
// real registry.

#include <functional>
#include <optional>
#include <string>

#include <DesertShared/EngineRegistry.hpp>
#include <DesertShared/ProjectFormat.hpp>
#include <DesertShared/ResultStr.hpp>

namespace Hub
{
    // `$HOME/.desertengine` (`%USERPROFILE%` on Windows), created on demand. The same folder the
    // engine's ProjectContext::ConfigDirectory returns — that agreement is what makes these two
    // programs share a registry at all.
    [[nodiscard]] std::string ConfigDirectory();

    [[nodiscard]] std::string ProjectsRegistryFile( const std::string& configDirectory );
    [[nodiscard]] std::string EnginesRegistryFile( const std::string& configDirectory );

    // A missing registry is an empty one — a fresh machine is not an error. Anything else that goes
    // wrong is returned with its reason: "the registry is empty" and "the registry could not be
    // read" used to look identical to every caller, and one of them means the user's project list
    // is still on disk and about to be overwritten with nothing.
    [[nodiscard]] Common::ResultStr<Common::Project::ProjectsRegistry>
    LoadProjects( const std::string& configDirectory );

    // THE ONLY WAY THIS PROCESS CHANGES projects.json — there is deliberately no "save the registry".
    //
    // The file has TWO WRITERS IN TWO PROGRAMS: this launcher and the engine's
    // Desert::Project::ProjectContext::RegisterRecent. Nothing arbitrates between them, so a writer
    // that flushes a snapshot it loaded earlier does not merely lose a FIELD — it loses whole
    // RECORDS. The measured shape: the launcher read the registry once when its window opened
    // (Main.cpp, startup), the Editor filed every project opened while that window was up, and the
    // next launcher action wrote its own session-old list back over the top. Everything the engine
    // recorded in between was gone, with nothing said.
    //
    // So a caller states an INTENT — promote this path, forget that path — and this function
    // re-reads the file AS IT IS ON DISK at the moment of the write, applies the intent to that, and
    // writes the result. `apply` must therefore be a rewrite of the registry it is handed and must
    // not close over a registry loaded earlier; that is the whole difference between this and the
    // shape it replaces.
    //
    // It also cannot destroy a registry it could not understand. An unreadable or unparseable file
    // is returned as an error and NOTHING is written — the previous shape left the in-memory
    // registry empty when the load failed and then wrote that emptiness back, so one unparseable
    // byte cost the user every project they had.
    //
    // The merged registry comes back so the caller can ADOPT it. A caller that keeps its pre-merge
    // copy is stale again the instant this returns, which is the defect this exists to remove.
    //
    // What is left: read-modify-write is not atomic ACROSS PROCESSES. The write itself is
    // (Files.cpp renames a temp file over the target, so the registry is never torn), and the window
    // in which a concurrent writer can be lost is now a 4 KB read plus a rename rather than a whole
    // launcher session. Closing the last microseconds needs an advisory file lock BOTH hosts take,
    // which belongs beside PromoteRecent in desert-shared rather than being written twice here — see
    // the K11 report.
    [[nodiscard]] Common::ResultStr<Common::Project::ProjectsRegistry>
    MutateProjects( const std::string&                                               configDirectory,
                    const std::function<void( Common::Project::ProjectsRegistry& )>& apply );

    [[nodiscard]] Common::ResultStr<Common::Engine::EngineRegistry>
    LoadEngines( const std::string& configDirectory );

    // Which engine this launcher will start, and how it found it.
    struct EngineChoice
    {
        std::string Root;        // "" = none found; every engine-dependent action then refuses
        std::string VersionFull; // "" when the root came from the environment rather than the registry
        // Absent when the engine could not name its own build (shallow clone, no git). The sidebar
        // says so rather than printing a number nobody can act on — 0 would read as "build 0".
        std::optional<int> CommitCount;
        // Why the sidebar shows what it shows. Never empty — with no engine it is the reason there
        // is none, which is the sentence the person with a broken install actually needs.
        std::string Explanation;
    };

    // The registry first, `DESERT_ROOT` second.
    //
    // The order is the point. engines.json is the durable answer, written by the Editor at startup;
    // DESERT_ROOT is the run script's answer and stops existing for the launcher the moment it
    // moves to its own repository (L3). Keeping the variable as a FALLBACK is what makes this
    // change land before the Editor half has ever run on a given machine — not a second source of
    // truth, a bridge with an end date.
    [[nodiscard]] EngineChoice ChooseEngine( const Common::Engine::EngineRegistry& registry,
                                             const char*                           desertRootEnvironment );
} // namespace Hub
