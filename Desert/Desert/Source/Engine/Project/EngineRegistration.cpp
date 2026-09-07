#include "EngineRegistration.hpp"

#include <Common/Core/Version.hpp>
#include <Common/Project/EngineRegistry.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <filesystem>

namespace Desert::Project
{
    Common::BoolResultStr RegisterThisEngine( const std::string& configDirectory, const std::string& engineRoot )
    {
        if ( engineRoot.empty() )
            return Common::MakeError<bool>(
                 "DESERT_ROOT is not set, so this engine could not record itself in engines.json and the "
                 "launcher will not list it - start the Editor through scripts/MacOS/RunEditor.sh (or "
                 "scripts\\Windows\\RunEditor.bat), which exports it." );

        std::error_code             ec;
        const std::filesystem::path root = std::filesystem::absolute( engineRoot, ec );
        if ( ec )
            return Common::MakeFormattedError<bool>(
                 "DESERT_ROOT={} could not be resolved to an absolute path: {}", engineRoot, ec.message() );

        const std::filesystem::path file = std::filesystem::path( configDirectory ) / "engines.json";

        // Read what is there first. A registry with two installs in it belongs to the USER, not to
        // this process: clobbering it with a single entry would delete the other engine from the
        // launcher's sidebar every time this one started.
        Common::Engine::EngineRegistry registry;
        if ( std::filesystem::exists( file, ec ) )
        {
            const auto raw = Common::Utils::FileSystem::ReadFileContent( file.string() );
            if ( !raw )
                return Common::MakeFormattedError<bool>( "Could not read {} - it is left untouched",
                                                         file.string() );
            if ( !raw.GetValue().empty() )
            {
                auto parsed = Common::Engine::ReadEngineRegistry( raw.GetValue() );
                if ( !parsed.IsSuccess() )
                    // Verbatim, and NOT overwritten: a file this process cannot parse may still be a
                    // file the user (or a newer build) can, and rewriting it from scratch would
                    // silently drop whatever it held.
                    return Common::MakeFormattedError<bool>( "{}: {} - it is left untouched", file.string(),
                                                             parsed.GetError() );
                registry = parsed.ExtractValue();
            }
        }

        Common::Engine::RegisterInstall(
             registry, Common::Engine::EngineInstall{ root.string(), Common::Version::Full(),
                                                      static_cast<int>( Common::Version::CommitCount() ) } );

        // Atomic, for the same reason projects.json is: two processes share this file, and an
        // interrupted in-place write leaves a torn one that neither can parse.
        if ( !Common::Utils::FileSystem::WriteContentToFileAtomic(
                  file, Common::Engine::WriteEngineRegistry( registry ) ) )
            return Common::MakeFormattedError<bool>( "Could not write {} - it keeps its previous contents",
                                                     file.string() );
        return Common::MakeSuccess( true );
    }
} // namespace Desert::Project
