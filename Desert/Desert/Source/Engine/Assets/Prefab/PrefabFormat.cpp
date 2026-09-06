#include <Engine/Assets/Prefab/PrefabFormat.hpp>

#include <rflcpp/rfl/json.hpp>

#include <spdlog/fmt/fmt.h>

namespace Desert::Assets
{
    namespace
    {
        // Where the build this message came from actually put the tool — same NDEBUG choice, and for the
        // same reason, as SceneFormat.cpp: naming a directory the binary is not in is the same dead end
        // as naming no command at all.
#ifdef NDEBUG
        constexpr const char* kMigratorPath = "build/Bin/Release/PrefabMigrator";
#else
        constexpr const char* kMigratorPath = "build/Bin/Debug/PrefabMigrator";
#endif
    } // namespace

    std::string RefusePrefabVersion( std::string_view source, int foundSceneVersion, int foundUnitVersion )
    {
        return fmt::format(
             "[PrefabAsset] '{0}' is at scene schema v{1} / world units v{2}, and this engine loads "
             "scene schema v{3} / world units v{4} only. NOTHING WAS LOADED - no entity was taken from "
             "this file, and the scene is exactly as it was. A prefab carries the same entity payloads a "
             "scene does, so it moves through the same generations; Tools/PrefabMigrator converts it, "
             "once, and writes the file back. Run:  {5} \"{0}\"",
             source, foundSceneVersion, foundUnitVersion, Core::kSceneVersion, Core::kUnitVersion, kMigratorPath );
    }

    std::string RefusePrefabVersion( std::string_view source, const PrefabData& prefab )
    {
        return RefusePrefabVersion( source, prefab.SceneVersion.value_or( 0 ), prefab.UnitVersion.value_or( 0 ) );
    }

    Common::ResultStr<PrefabData> ParseLoadablePrefab( std::string_view source, const std::string& json )
    {
        auto parsed = rfl::json::read<PrefabData>( json );
        if ( !parsed )
        {
            return Common::MakeError<PrefabData>(
                 fmt::format( "[PrefabAsset] '{0}' is not a readable prefab file: {1}. Nothing was loaded.",
                              source, parsed.error().what() ) );
        }

        if ( !PrefabIsAtCurrentVersion( parsed.value() ) )
            return Common::MakeError<PrefabData>( RefusePrefabVersion( source, parsed.value() ) );

        return Common::MakeSuccess( std::move( parsed.value() ) );
    }

    std::string WritePrefabJson( PrefabData prefab )
    {
        prefab.SceneVersion = Core::kSceneVersion;
        prefab.UnitVersion  = Core::kUnitVersion;
        return rfl::json::write( prefab );
    }

} // namespace Desert::Assets
