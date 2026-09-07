#include "HubConfig.hpp"

#include "Files.hpp"

#include <cstdlib>
#include <filesystem>

namespace Hub
{
    namespace fs = std::filesystem;

    std::string ConfigDirectory()
    {
        const char* home = std::getenv( "HOME" );
#ifdef _WIN32
        if ( !home )
            home = std::getenv( "USERPROFILE" );
#endif
        fs::path        directory = fs::path( home ? home : "." ) / ".desertengine";
        std::error_code ec;
        fs::create_directories( directory, ec );
        return directory.string();
    }

    std::string ProjectsRegistryFile( const std::string& configDirectory )
    {
        return ( fs::path( configDirectory ) / "projects.json" ).string();
    }

    std::string EnginesRegistryFile( const std::string& configDirectory )
    {
        return ( fs::path( configDirectory ) / "engines.json" ).string();
    }

    namespace
    {
        // One shape for both registries: absent is empty, unreadable and unparseable are refusals
        // carrying their own reason.
        template <typename T, typename ReadFn>
        Common::ResultStr<T> LoadRegistry( const std::string& file, ReadFn read )
        {
            std::error_code ec;
            if ( !fs::exists( file, ec ) ) // no registry yet — a fresh machine, not an error
                return Common::MakeSuccess( T{} );

            const auto raw = ReadTextFile( file );
            if ( !raw.IsSuccess() )
                return Common::MakeError<T>( raw.GetError() );
            if ( raw.GetValue().empty() )
                return Common::MakeSuccess( T{} );

            auto parsed = read( raw.GetValue() );
            if ( !parsed.IsSuccess() )
                return Common::MakeError<T>( file + ": " + parsed.GetError() );
            return Common::MakeSuccess( parsed.ExtractValue() );
        }
    } // namespace

    Common::ResultStr<Common::Project::ProjectsRegistry> LoadProjects( const std::string& configDirectory )
    {
        return LoadRegistry<Common::Project::ProjectsRegistry>( ProjectsRegistryFile( configDirectory ),
                                                                Common::Project::ReadProjectsRegistry );
    }

    Common::BoolResultStr SaveProjects( const std::string&                       configDirectory,
                                        const Common::Project::ProjectsRegistry& registry )
    {
        return WriteTextFile( ProjectsRegistryFile( configDirectory ),
                              Common::Project::WriteProjectsRegistry( registry ) );
    }

    Common::ResultStr<Common::Engine::EngineRegistry> LoadEngines( const std::string& configDirectory )
    {
        return LoadRegistry<Common::Engine::EngineRegistry>( EnginesRegistryFile( configDirectory ),
                                                             Common::Engine::ReadEngineRegistry );
    }

    EngineChoice ChooseEngine( const Common::Engine::EngineRegistry& registry, const char* desertRootEnvironment )
    {
        EngineChoice choice;
        if ( const Common::Engine::EngineInstall* install = Common::Engine::PreferredInstall( registry ) )
        {
            std::error_code ec;
            if ( fs::exists( install->Root, ec ) )
            {
                choice.Root        = install->Root;
                choice.VersionFull = install->VersionFull;
                choice.CommitCount = install->CommitCount;
                choice.Explanation = "from " + std::string( "engines.json" );
                return choice;
            }
            // A registered root that is no longer there is the same disease as a dead project entry,
            // one level up: agent worktrees get reclaimed, and an engine that is gone must not be
            // presented as the one that will start.
            choice.Explanation = install->Root + " is registered in engines.json but is no longer on disk";
        }

        if ( desertRootEnvironment && desertRootEnvironment[0] )
        {
            choice.Root = desertRootEnvironment;
            choice.Explanation =
                 "from DESERT_ROOT - no Editor has registered itself in engines.json on this machine yet";
            return choice;
        }

        if ( choice.Explanation.empty() )
            choice.Explanation = "No engine found: engines.json is empty and DESERT_ROOT is not set. Start the "
                                 "Editor once, or launch this hub through scripts/MacOS/RunProjectHub.sh.";
        return choice;
    }
} // namespace Hub
