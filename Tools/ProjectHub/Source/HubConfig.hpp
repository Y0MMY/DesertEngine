#pragma once

// The two files in `~/.desertengine` the launcher cares about, and where that folder is.
//
// This used to live in Main.cpp's anonymous namespace, which meant the one thing a launcher does
// before it draws anything — find the user's projects and the user's engine — could only be tested
// by starting a window. Every function here takes the config directory as an argument for exactly
// that reason: a test points it at a temp folder and asserts what landed there, which is the only
// way to assert "what the launcher writes, the engine reads" without writing into the developer's
// real registry.

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

    // Returns the reason on failure — the file on disk then keeps its previous list, which for a
    // convenience file is the right failure: nothing is lost, the one change is.
    [[nodiscard]] Common::BoolResultStr SaveProjects( const std::string&                       configDirectory,
                                                      const Common::Project::ProjectsRegistry& registry );

    [[nodiscard]] Common::ResultStr<Common::Engine::EngineRegistry>
    LoadEngines( const std::string& configDirectory );

    // Which engine this launcher will start, and how it found it.
    struct EngineChoice
    {
        std::string Root;            // "" = none found; every engine-dependent action then refuses
        std::string VersionFull;     // "" when the root came from the environment rather than the registry
        int         CommitCount = 0; // the build number the sidebar shows beside the version
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
