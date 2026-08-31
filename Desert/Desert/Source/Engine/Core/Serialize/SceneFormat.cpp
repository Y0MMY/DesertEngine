#include <Engine/Core/Serialize/SceneFormat.hpp>

#include <rflcpp/rfl/json.hpp>

#include <spdlog/fmt/fmt.h>

namespace Desert::Core
{
    namespace
    {
        // Where the build this message came from actually put the tool. NDEBUG is the only configuration
        // marker the workspace defines (Desert/Configurations.lua), so this is the whole choice - and
        // naming the wrong directory would send the reader to a binary that is not there, which is the same
        // dead end as naming no command at all.
#ifdef NDEBUG
        constexpr const char* kMigratorPath = "build/Bin/Release/SceneMigrator";
#else
        constexpr const char* kMigratorPath = "build/Bin/Debug/SceneMigrator";
#endif
    } // namespace

    std::string RefuseSceneVersion( std::string_view source, int foundSceneVersion, int foundUnitVersion )
    {
        return fmt::format(
             "[SceneSerializer] '{0}' is at scene schema v{1} / world units v{2}, and this engine loads "
             "scene schema v{3} / world units v{4} only. NOTHING WAS LOADED - no entity, no setting and no "
             "scene name was taken from this file, and the scene is exactly as it was. The engine does not "
             "convert old scenes any more; Tools/SceneMigrator does, once, and writes the file back. Run:  "
             "{5} \"{0}\"",
             source, foundSceneVersion, foundUnitVersion, kSceneVersion, kUnitVersion, kMigratorPath );
    }

    std::string RefuseSceneVersion( std::string_view source, const SceneSerialized& scene )
    {
        return RefuseSceneVersion( source, scene.SceneVersion.value_or( 0 ), scene.UnitVersion.value_or( 0 ) );
    }

    Common::ResultStr<SceneSerialized> ParseLoadableScene( std::string_view source, const std::string& json )
    {
        auto parsed = rfl::json::read<SceneSerialized>( json );
        if ( !parsed )
        {
            return Common::MakeError<SceneSerialized>(
                 fmt::format( "[SceneSerializer] '{0}' is not a readable scene file: {1}. Nothing was loaded.",
                              source, parsed.error().what() ) );
        }

        if ( !SceneIsAtCurrentVersion( parsed.value() ) )
            return Common::MakeError<SceneSerialized>( RefuseSceneVersion( source, parsed.value() ) );

        return Common::MakeSuccess( std::move( parsed.value() ) );
    }

} // namespace Desert::Core
