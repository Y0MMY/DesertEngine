// The protocol scene is a MEASURING INSTRUMENT, and this suite is what keeps it from moving.
//
// WHY IT EXISTS. Every phase of the cloud programme measures itself at the same six camera points on the
// same scene, and Docs/Clouds/CALIBRATION.md §PR establishes what that scene actually was: seven of the
// cloud layer's fifty-one parameters were written in the file and the other forty-four came from whatever
// VolumetricCloudComponent.hpp said on the day somebody pressed render. Eight later phases moved a default
// in exactly those forty-four. So the sky the protocol measured changed twelve times while the file it was
// read from changed eight, and the two sets barely overlap — a diff of the scene between two phases showed
// nothing, correctly and uselessly.
//
// The cure is a scene that states ALL of its parameters, so that no change to a C++ default can reach it.
// A cure that nothing checks is a comment: the day somebody adds a fifty-second field to
// VolumetricCloudData, Clouds_Protocol.desce silently goes back to having a default in it, and the next
// six-point table is measured through that default with nobody the wiser.
//
// So the property is asserted rather than remembered, and it is asserted as a RELATION between two things
// obliged to agree (contract §2.3.1): the reflection table and the file on disk. Adding a field to a
// reflected component turns this suite RED, and the message names the field.
//
// It is a pure-function suite: it parses JSON, walks the reflection registry, and runs the migration
// functions. No GPU, no asset manager, no scene graph.

#include <Engine/Core/Serialize/SceneMigration.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>

#include <rflcpp/rfl/json.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using Desert::Core::MigrateScene;
using Desert::Core::SceneSerialized;
using Desert::Reflection::ReflectionRegistry;
using Desert::Reflection::TypeInfo;

namespace
{
    // The scenes this suite guards, and the ONE place that list lives. Clouds_Protocol is the six-point
    // ruler; the three PR_Hero scenes are the committed legs of §PR's per-pass cost measurement, and they
    // are guarded for the same reason — a cost number whose scene can be moved by a default is a cost
    // number that will not reproduce.
    const char* const kProtocolScenes[] = {
        "Clouds_Protocol.desce",
        "PR_Hero0.desce",
        "PR_Hero3.desce",
        "PR_Hero8.desce",
    };

    // Component key on disk -> reflected type name. Only the REFLECTED components are listed: the others
    // (StaticMesh, DirectionLight, Skybox) go through hand-written serializers in ComponentRegistry.cpp
    // that write a fixed set of keys, so they cannot grow a field behind a scene's back.
    struct ReflectedComponent
    {
        const char* Key;
        const char* TypeName;
    };

    constexpr ReflectedComponent kReflected[] = {
        { "VolumetricCloud", "VolumetricCloudData" },
        { "SkyAtmosphere", "SkyAtmosphereData" },
        { "ExponentialHeightFog", "ExponentialHeightFogData" },
        { "HeroCloud", "HeroCloudData" },
    };

    // Walks up from the working directory looking for a file only the repository has. Copied in shape
    // from Desert/Tests/Engine/SceneTonemapMigration, which needs the same thing for the same reason:
    // the test runner's working directory is not fixed.
    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/Core/SceneSettings.hpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ScenePath( const std::string& name )
    {
        return RepoRoot() + "Editor/Resources/Assets/Scenes/" + name;
    }

    std::string ReadAll( const std::string& path )
    {
        std::ifstream     in( path, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // Every key the parsed payload carries. Written by hand because rfl::Object exposes no `contains`.
    std::set<std::string> KeysOf( const rfl::Generic::Object& object )
    {
        std::set<std::string> keys;
        for ( auto it = object.begin(); it != object.end(); ++it )
            keys.insert( it->first );
        return keys;
    }

    const TypeInfo* Reflected( const char* typeName )
    {
        return ReflectionRegistry::Get().Find( typeName );
    }
} // namespace

// THE CLAIM: no field of a reflected component reaches the renderer from a C++ default when a protocol
// scene is loaded. Every one of them is written in the file.
//
// This is the assertion that goes red when somebody adds a field, and the whole point of the suite. The
// remedy when it does is not to edit this list — it is to write the new field into the four scenes at the
// value the phase intends the protocol to be measured at, and to say in CALIBRATION.md that the protocol
// scene moved and why.
TEST( CloudProtocolScene, EveryReflectedFieldIsWrittenExplicitlySoNoDefaultCanMoveTheProtocol )
{
    for ( const char* sceneName : kProtocolScenes )
    {
        const std::string json = ReadAll( ScenePath( sceneName ) );
        ASSERT_FALSE( json.empty() ) << sceneName << " is missing or empty";

        const auto parsed = rfl::json::read<SceneSerialized>( json );
        ASSERT_TRUE( parsed ) << sceneName << " does not parse as a scene";

        int checkedComponents = 0;
        for ( const auto& entity : parsed.value().Entities )
        {
            for ( const auto& component : kReflected )
            {
                const auto found = entity.Components.get().find( component.Key );
                if ( found == entity.Components.get().end() )
                    continue;

                const auto payload = found->second.to_object();
                ASSERT_TRUE( payload ) << sceneName << ": '" << component.Key << "' is not an object";

                const TypeInfo* type = Reflected( component.TypeName );
                ASSERT_NE( type, nullptr )
                     << component.TypeName << " is not in the reflection registry, so nothing here means "
                                              "anything";

                const std::set<std::string> present = KeysOf( payload.value() );
                for ( const auto& field : type->Fields )
                    EXPECT_TRUE( present.count( field.Name ) != 0 )
                         << sceneName << ": '" << component.Key << "." << field.Name
                         << "' is NOT written in the protocol scene, so it comes from the C++ default and "
                            "the protocol moves the next time that default does. Write it into the scene "
                            "at the value the protocol is to be measured at.";
                ++checkedComponents;
            }
        }
        EXPECT_GT( checkedComponents, 0 ) << sceneName << " carries no reflected component at all";
    }
}

// The scene-wide settings block is reflected too, and it is where CloudQualityTier lives — the tier every
// six-point table in CALIBRATION.md was implicitly shot at without ever naming it.
TEST( CloudProtocolScene, TheSettingsBlockIsWrittenInFullIncludingTheCloudQualityTier )
{
    const TypeInfo* type = Reflected( "SceneSettings" );
    ASSERT_NE( type, nullptr );

    for ( const char* sceneName : kProtocolScenes )
    {
        const auto parsed = rfl::json::read<SceneSerialized>( ReadAll( ScenePath( sceneName ) ) );
        ASSERT_TRUE( parsed ) << sceneName;
        ASSERT_TRUE( parsed.value().Settings.has_value() ) << sceneName << " has no Settings block";

        const auto settings = parsed.value().Settings->to_object();
        ASSERT_TRUE( settings ) << sceneName << ": Settings is not an object";

        const std::set<std::string> present = KeysOf( settings.value() );
        for ( const auto& field : type->Fields )
            EXPECT_TRUE( present.count( field.Name ) != 0 )
                 << sceneName << ": 'Settings." << field.Name << "' is not written, so it is a default";

        EXPECT_TRUE( present.count( "CloudQualityTier" ) != 0 ) << sceneName;
    }
}

// THE SECOND CHANNEL. A file at an old schema is migrated on load, and what the renderer then sees is the
// migration's output rather than the file's content — which is a value nobody can read off the scene. A
// protocol scene has to be at the current generation of BOTH version integers, so that "what the file says"
// and "what the engine loads" are the same sentence.
TEST( CloudProtocolScene, NothingMigratesOnLoadSoTheFileIsWhatTheEngineSees )
{
    for ( const char* sceneName : kProtocolScenes )
    {
        auto parsed = rfl::json::read<SceneSerialized>( ReadAll( ScenePath( sceneName ) ) );
        ASSERT_TRUE( parsed ) << sceneName;

        SceneSerialized scene = parsed.value();
        ASSERT_TRUE( scene.SceneVersion.has_value() ) << sceneName << " states no SceneVersion";
        ASSERT_TRUE( scene.UnitVersion.has_value() ) << sceneName << " states no UnitVersion";
        EXPECT_EQ( *scene.SceneVersion, Desert::Core::kSceneVersion ) << sceneName;
        EXPECT_EQ( *scene.UnitVersion, Desert::Core::kUnitVersion ) << sceneName;

        const auto report = MigrateScene( scene );
        EXPECT_FALSE( report.Changed() )
             << sceneName
             << " is migrated on load, so the numbers the renderer uses are the migration's and not the "
                "file's. Re-save it at the current generation.";
    }
}

// The three cost legs differ in ONE thing, and a measurement whose A and B differ in two things measures
// neither. Asserted here rather than trusted to the script that wrote them.
TEST( CloudProtocolScene, TheThreeHeroCostLegsDifferOnlyInHowManyHeroCloudsAreEnabled )
{
    struct Leg
    {
        const char* Scene;
        int         Expected;
    };
    constexpr Leg kLegs[] = { { "PR_Hero0.desce", 0 }, { "PR_Hero3.desce", 3 }, { "PR_Hero8.desce", 8 } };

    std::vector<std::string> normalised;
    for ( const auto& leg : kLegs )
    {
        auto parsed = rfl::json::read<SceneSerialized>( ReadAll( ScenePath( leg.Scene ) ) );
        ASSERT_TRUE( parsed ) << leg.Scene;

        SceneSerialized scene = parsed.value();
        scene.SceneName       = "normalised";

        int live = 0;
        for ( auto& entity : scene.Entities )
        {
            auto found = entity.Components.get().find( "HeroCloud" );
            if ( found == entity.Components.get().end() )
                continue;

            auto payload = found->second.to_object();
            ASSERT_TRUE( payload ) << leg.Scene;
            const auto enabled = payload.value()["Enabled"].to_bool();
            ASSERT_TRUE( enabled ) << leg.Scene << ": HeroCloud.Enabled is not a bool";
            if ( *enabled )
                ++live;

            // Erase the one field the legs are allowed to differ in, so the rest can be compared whole.
            payload.value()["Enabled"] = false;
            found->second              = rfl::Generic( payload.value() );
        }
        EXPECT_EQ( live, leg.Expected ) << leg.Scene << " does not carry the instance count its name claims";
        normalised.push_back( rfl::json::write( scene ) );
    }

    for ( std::size_t i = 1; i < normalised.size(); ++i )
        EXPECT_EQ( normalised[0], normalised[i] )
             << "the hero cost legs differ in something other than HeroCloud.Enabled, so their A/B measures "
                "more than the instance count";
}
