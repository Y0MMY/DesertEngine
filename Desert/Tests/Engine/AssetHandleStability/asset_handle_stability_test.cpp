// THE IDENTITY OF AN ASSET — a suite about a RELATION, not about a function.
//
// The relation: AN ASSET AT A GIVEN PATH MUST CARRY THE SAME HANDLE IN RUNS THAT SHARE NOTHING, and it
// must do so for EVERY asset type, not for the ones somebody remembered.
//
// What was broken. `Common::UUID`'s default constructor minted a random 64-bit id, and `AssetBase` used it
// for every asset's handle. Five types — prefab, skybox, shader, skeleton, animation — kept that handle, so
// their identity was a property of the LAUNCH rather than of the file. The other nine had each grown a
// private copy of one line, `m_Metadata.Handle = AssetHandle::FromCookedPath(...)`, to escape it. Nine
// copies of a rule is how the tenth type gets forgotten, and the symptom when it is forgotten is a
// reference that resolves to nothing after a restart, with nothing logged and nothing thrown. An artist
// reports it as "it was fine yesterday".
//
// Why a CHILD PROCESS. A test that builds two assets inside one process proves the derivation is not a
// counter, which is worth having but is not the property that was broken. Anything seeded once per process
// — a random engine, a monotonic id, an address — passes an in-process comparison and still hands out a
// fresh handle on the next launch. Only two processes that share no state can tell those apart, so this
// binary re-executes ITSELF and compares what the two runs printed. That is "two independent loads" taken
// literally, and it is the same device the CloudNoiseVolumeHandle suite uses for one type; this suite
// generalises it to all of them.
//
// Why a CENSUS. The catalogue below and `Desert::Assets::AssetTypeID` are two places that must agree, and
// nothing made them agree — which is precisely the defect class this whole change is about. So the census
// test asserts the count. Add an asset type and this suite goes red until the type is entered here; that
// red is the point, not an inconvenience.

#include <gtest/gtest.h>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Constants.hpp>

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetMetadata.hpp>
#include <Engine/Assets/CloudLayoutAsset.hpp>
#include <Engine/Assets/CloudModellingVolumeAsset.hpp>
#include <Engine/Assets/CloudNoiseVolumeAsset.hpp>
#include <Engine/Assets/CloudTypeAsset.hpp>
#include <Engine/Assets/Mesh/AnimationAsset.hpp>
#include <Engine/Assets/Mesh/SkeletonAsset.hpp>
#include <Engine/Assets/Mesh/SkinnedMeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/Shader/ShaderAsset.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>
#include <Engine/Assets/TextureAsset.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using Desert::Assets::AssetPriority;
using Desert::Assets::AssetTypeID;

namespace
{
    // argv[0] of this run, captured by main. The child is this same binary.
    std::string g_ExecutablePath;

    constexpr const char* kPrintHandlesFlag = "--print-handles";
    constexpr const char* kResolveFlag      = "--resolve";

    // The subject: the handle an asset carries THE MOMENT IT IS CONSTRUCTED, before any file is read.
    // Construction and not post-Load, because the AssetManager keys a not-yet-loaded registry shell by it
    // and because a handle that only becomes correct after a successful load is not an identity — it is a
    // result.
    template <typename TAsset>
    uint64_t HandleOf( const std::string& path )
    {
        const TAsset asset( AssetPriority::Medium, Common::Filepath( path ) );
        return static_cast<uint64_t>( asset.GetMetadata().Handle );
    }

    struct AssetKind
    {
        AssetTypeID Type;
        const char* Name;
        uint64_t ( *Handle )( const std::string& );
    };

    // THE CATALOGUE. Every concrete asset type in the engine, with the type id it reports. The census test
    // below checks this list against the AssetTypeID enum, so it cannot quietly fall behind.
    const std::vector<AssetKind>& Catalogue()
    {
        static const std::vector<AssetKind> kinds = {
             { AssetTypeID::Mesh, "StaticMeshAsset", &HandleOf<Desert::Assets::StaticMeshAsset> },
             { AssetTypeID::Mesh, "SkinnedMeshAsset", &HandleOf<Desert::Assets::SkinnedMeshAsset> },
             { AssetTypeID::Material, "SurfaceMaterialAsset", &HandleOf<Desert::Assets::SurfaceMaterialAsset> },
             { AssetTypeID::Texture2D, "TextureAsset", &HandleOf<Desert::Assets::TextureAsset> },
             { AssetTypeID::Skybox, "SkyboxAsset", &HandleOf<Desert::Assets::SkyboxAsset> },
             { AssetTypeID::Shader, "ShaderAsset", &HandleOf<Desert::Assets::ShaderAsset> },
             { AssetTypeID::Skeleton, "SkeletonAsset", &HandleOf<Desert::Assets::SkeletonAsset> },
             { AssetTypeID::Animation, "AnimationAsset", &HandleOf<Desert::Assets::AnimationAsset> },
             { AssetTypeID::CloudNoiseVolume, "CloudNoiseVolumeAsset",
               &HandleOf<Desert::Assets::CloudNoiseVolumeAsset> },
             { AssetTypeID::CloudType, "CloudTypeAsset", &HandleOf<Desert::Assets::CloudTypeAsset> },
             { AssetTypeID::CloudModellingVolume, "CloudModellingVolumeAsset",
               &HandleOf<Desert::Assets::CloudModellingVolumeAsset> },
             { AssetTypeID::CloudLayout, "CloudLayoutAsset", &HandleOf<Desert::Assets::CloudLayoutAsset> },
        };
        return kinds;
    }

    // A path that is REPRESENTATIVE rather than arbitrary: shaped like the project-rooted paths the
    // preloader actually registers assets under. The extension is deliberately not varied per type — the
    // handle must be a function of the STRING, and a type that folded its own extension or type id into the
    // derivation would make two different assets at one path collide, which the last test here catches.
    const std::string kPathA = "Assets/Library/Subject.asset";
    const std::string kPathB = "Assets/Library/Other.asset";

    // Restores every content root SetProjectRoot rewrites. The roots are process-wide mutable globals, so
    // a test that opens a project and walks away leaves every test after it measuring that project.
    class ProjectRootGuard
    {
    public:
        ProjectRootGuard()
             : m_Assets( Common::Constants::Path::ASSETS_PATH ), m_Mesh( Common::Constants::Path::MESH_PATH ),
               m_Material( Common::Constants::Path::MATERIAL_PATH ),
               m_TextureDir( Common::Constants::Path::TEXTUREDIR_PATH ),
               m_TextureDirEnv( Common::Constants::Path::TEXTUREDIRENV_PATH ),
               m_Skybox( Common::Constants::Path::SKYBOX_PATH ), m_Scene( Common::Constants::Path::SCENE_PATH ),
               m_Prefab( Common::Constants::Path::PREFAB_PATH ), m_Script( Common::Constants::Path::SCRIPT_PATH ),
               m_Collections( Common::Constants::Path::COLLECTIONS_PATH ),
               m_CloudNoise( Common::Constants::Path::CLOUD_NOISE_PATH ),
               m_CloudType( Common::Constants::Path::CLOUD_TYPE_PATH ),
               m_CloudVolume( Common::Constants::Path::CLOUD_VOLUME_PATH ),
               m_CloudLayout( Common::Constants::Path::CLOUD_LAYOUT_PATH ),
               m_Cooked( Common::Constants::Path::COOKED_PATH ),
               m_MeshCooked( Common::Constants::Path::MESH_PATH_COOKED ),
               m_TextureCooked( Common::Constants::Path::TEXTURE_PATH_COOKED )
        {
        }

        ~ProjectRootGuard()
        {
            Common::Constants::Path::ASSETS_PATH         = m_Assets;
            Common::Constants::Path::MESH_PATH           = m_Mesh;
            Common::Constants::Path::MATERIAL_PATH       = m_Material;
            Common::Constants::Path::TEXTUREDIR_PATH     = m_TextureDir;
            Common::Constants::Path::TEXTUREDIRENV_PATH  = m_TextureDirEnv;
            Common::Constants::Path::SKYBOX_PATH         = m_Skybox;
            Common::Constants::Path::SCENE_PATH          = m_Scene;
            Common::Constants::Path::PREFAB_PATH         = m_Prefab;
            Common::Constants::Path::SCRIPT_PATH         = m_Script;
            Common::Constants::Path::COLLECTIONS_PATH    = m_Collections;
            Common::Constants::Path::CLOUD_NOISE_PATH    = m_CloudNoise;
            Common::Constants::Path::CLOUD_TYPE_PATH     = m_CloudType;
            Common::Constants::Path::CLOUD_VOLUME_PATH   = m_CloudVolume;
            Common::Constants::Path::CLOUD_LAYOUT_PATH   = m_CloudLayout;
            Common::Constants::Path::COOKED_PATH         = m_Cooked;
            Common::Constants::Path::MESH_PATH_COOKED    = m_MeshCooked;
            Common::Constants::Path::TEXTURE_PATH_COOKED = m_TextureCooked;
        }

        ProjectRootGuard( const ProjectRootGuard& )            = delete;
        ProjectRootGuard& operator=( const ProjectRootGuard& ) = delete;

    private:
        std::filesystem::path m_Assets;
        std::filesystem::path m_Mesh;
        std::filesystem::path m_Material;
        std::filesystem::path m_TextureDir;
        std::filesystem::path m_TextureDirEnv;
        std::filesystem::path m_Skybox;
        std::filesystem::path m_Scene;
        std::filesystem::path m_Prefab;
        std::filesystem::path m_Script;
        std::filesystem::path m_Collections;
        std::filesystem::path m_CloudNoise;
        std::filesystem::path m_CloudType;
        std::filesystem::path m_CloudVolume;
        std::filesystem::path m_CloudLayout;
        std::filesystem::path m_Cooked;
        std::filesystem::path m_MeshCooked;
        std::filesystem::path m_TextureCooked;
    };

    uint64_t HandleValue( const std::filesystem::path& path )
    {
        return static_cast<uint64_t>( Common::AssetHandle::FromCookedPath( path ) );
    }

    std::string Trimmed( const std::string& text )
    {
        const auto first = text.find_first_not_of( " \t\r\n" );
        if ( first == std::string::npos )
            return {};
        const auto last = text.find_last_not_of( " \t\r\n" );
        return text.substr( first, last - first + 1 );
    }

    // Runs THIS binary again, in a process of its own, and returns one line per catalogue entry: the
    // handle that entry derives for `path`. An empty result means the child could not be run or printed
    // nothing; the callers treat that as a failure of the test rather than as a pass, because a silently
    // skipped guard is not a guard.
    std::vector<std::string> HandlesFromAnIndependentProcess( const std::string& path )
    {
        if ( g_ExecutablePath.empty() )
            return {};

#ifdef DESERT_PLATFORM_WINDOWS
        // cmd.exe strips the outer pair of quotes off the whole command line, so a quoted executable AND a
        // quoted argument need one more pair around the lot.
        const std::string command = "\"\"" + g_ExecutablePath + "\" " + kPrintHandlesFlag + " \"" + path + "\"\"";
        FILE*             pipe    = _popen( command.c_str(), "r" );
#else
        const std::string command = "'" + g_ExecutablePath + "' " + kPrintHandlesFlag + " '" + path + "'";
        FILE*             pipe    = popen( command.c_str(), "r" );
#endif
        if ( !pipe )
            return {};

        std::vector<std::string> lines;
        char                     buffer[256];
        while ( fgets( buffer, sizeof( buffer ), pipe ) != nullptr )
        {
            const std::string line = Trimmed( buffer );
            if ( !line.empty() )
                lines.push_back( line );
        }

#ifdef DESERT_PLATFORM_WINDOWS
        _pclose( pipe );
#else
        pclose( pipe );
#endif
        return lines;
    }

    // The other half of the reproduction: a SECOND process registers the asset at `path` in a fresh
    // AssetManager and asks for `handle` — the number the first process would have written into a scene.
    // Prints RESOLVED or MISSED.
    std::string ResolveInAnIndependentProcess( const std::string& path, uint64_t handle )
    {
        if ( g_ExecutablePath.empty() )
            return {};

        const std::string h = std::to_string( handle );
#ifdef DESERT_PLATFORM_WINDOWS
        const std::string command =
             "\"\"" + g_ExecutablePath + "\" " + kResolveFlag + " \"" + path + "\" " + h + "\"";
        FILE* pipe = _popen( command.c_str(), "r" );
#else
        const std::string command = "'" + g_ExecutablePath + "' " + kResolveFlag + " '" + path + "' " + h;
        FILE*             pipe    = popen( command.c_str(), "r" );
#endif
        if ( !pipe )
            return {};

        std::string output;
        char        buffer[256];
        while ( fgets( buffer, sizeof( buffer ), pipe ) != nullptr )
            output += buffer;

#ifdef DESERT_PLATFORM_WINDOWS
        _pclose( pipe );
#else
        pclose( pipe );
#endif
        return Trimmed( output );
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// The root cause. Everything below is a consequence of this one property, which is why it is asserted
// first and on its own: if the default-constructed UUID is ever random again, the rest of this suite is
// measuring symptoms.
// ---------------------------------------------------------------------------------------------------

TEST( AssetHandleStability, ADefaultConstructedUuidIsNull )
{
    EXPECT_TRUE( Common::UUID{}.IsNull() );
    EXPECT_EQ( static_cast<uint64_t>( Common::UUID{} ), 0u );
    EXPECT_EQ( static_cast<uint64_t>( Common::UUID{} ), static_cast<uint64_t>( Common::UUID::Null() ) );

    // Two of them are the SAME value. This is the assertion that `value_or({})`, `!= {}` and an
    // uninitialized member can be trusted; when the default was random it was false, and every one of
    // those idioms was a live defect.
    EXPECT_EQ( static_cast<uint64_t>( Common::UUID{} ), static_cast<uint64_t>( Common::UUID{} ) );
}

TEST( AssetHandleStability, GenerateStillGivesFreshNonNullIds )
{
    // The null default is only safe if the callers that need randomness can still get it — entity ids and
    // new material GUIDs depend on it. A fix that made Generate() a constant would pass every stability
    // test in this file and collapse every entity in a scene onto one id.
    std::set<uint64_t> seen;
    for ( int i = 0; i < 64; ++i )
    {
        const uint64_t id = static_cast<uint64_t>( Common::UUID::Generate() );
        EXPECT_NE( id, 0u ) << "Generate() returned the null id, which reads as 'no id' everywhere";
        seen.insert( id );
    }
    EXPECT_EQ( seen.size(), 64u ) << "Generate() repeated itself in 64 draws";
}

TEST( AssetHandleStability, AnEmptyAssetMetadataIsNotValid )
{
    // IsValid() compares the handle against 0. Under the random default a default-constructed metadata
    // reported itself VALID, which is how an empty record could be handed to a service.
    const Desert::Assets::AssetMetadata empty{};
    EXPECT_FALSE( empty.IsValid() );
    EXPECT_TRUE( empty.Handle.IsNull() );
    EXPECT_EQ( empty.AssetType, AssetTypeID::Unknown );
}

// ---------------------------------------------------------------------------------------------------
// The relation, per asset type.
// ---------------------------------------------------------------------------------------------------

TEST( AssetHandleStability, EveryAssetTypeDerivesItsHandleFromItsPath )
{
    const uint64_t expected = static_cast<uint64_t>( Common::AssetHandle::FromCookedPath( kPathA ) );

    for ( const auto& kind : Catalogue() )
    {
        EXPECT_EQ( kind.Handle( kPathA ), expected )
             << kind.Name
             << " does not take the shared path-derived identity. A type that computes its "
                "own is a copy of a rule, and the copy is what falls behind.";
    }
}

TEST( AssetHandleStability, NoAssetTypeGetsTheNullHandle )
{
    // The null handle reads as "no asset" everywhere in the engine, so a derivation that produced it would
    // be perfectly stable and perfectly useless.
    for ( const auto& kind : Catalogue() )
    {
        EXPECT_NE( kind.Handle( kPathA ), 0u ) << kind.Name << " derived the null handle";
    }
}

TEST( AssetHandleStability, DifferentPathsGiveDifferentHandles )
{
    // Stability is only half of the relation. A derivation that returned a constant would pass the
    // cross-process test below and collapse every asset in the library onto one handle.
    for ( const auto& kind : Catalogue() )
    {
        EXPECT_NE( kind.Handle( kPathA ), kind.Handle( kPathB ) )
             << kind.Name << " gave one handle to two different paths";
    }
}

TEST( AssetHandleStability, EquivalentSpellingsOfOnePathGiveOneHandle )
{
    // Two spellings of one file are one file. The derivation normalises before hashing, so a caller that
    // joined a path with a `.` or a `..` in it lands on the same asset as one that did not — the
    // difference between a handle that identifies a FILE and one that identifies a STRING.
    for ( const auto& kind : Catalogue() )
    {
        const uint64_t direct = kind.Handle( "Assets/Library/Subject.asset" );
        EXPECT_EQ( kind.Handle( "Assets/./Library/Subject.asset" ), direct ) << kind.Name;
        EXPECT_EQ( kind.Handle( "Assets/Library/../Library/Subject.asset" ), direct ) << kind.Name;
    }
}

TEST( AssetHandleStability, TwoAssetsOverOnePathAgreeWithinASingleProcess )
{
    // The cheap half of the property: it catches a counter or an address-derived id immediately, without
    // paying for a child process.
    for ( const auto& kind : Catalogue() )
    {
        EXPECT_EQ( kind.Handle( kPathA ), kind.Handle( kPathA ) ) << kind.Name;
    }
}

// THE test the defect was about.
TEST( AssetHandleStability, EveryAssetTypeAgreesInTwoIndependentRuns )
{
    const std::vector<std::string> firstRun  = HandlesFromAnIndependentProcess( kPathA );
    const std::vector<std::string> secondRun = HandlesFromAnIndependentProcess( kPathA );

    ASSERT_EQ( firstRun.size(), Catalogue().size() )
         << "the first child run printed " << firstRun.size() << " handles for " << Catalogue().size()
         << " asset types; the guard did not run properly";
    ASSERT_EQ( secondRun.size(), Catalogue().size() ) << "the second child run printed the wrong count";

    for ( size_t i = 0; i < Catalogue().size(); ++i )
    {
        EXPECT_EQ( firstRun[i], secondRun[i] )
             << Catalogue()[i].Name << " at '" << kPathA << "' was handle " << firstRun[i] << " in one run and "
             << secondRun[i]
             << " in the next. Any scene storing that handle would resolve to nothing after a restart.";

        // And the running process agrees with both, which is what makes the value a property of the PATH
        // rather than of a process that happens to be consistent with itself.
        EXPECT_EQ( std::to_string( Catalogue()[i].Handle( kPathA ) ), firstRun[i] ) << Catalogue()[i].Name;
    }
}

TEST( AssetHandleStability, AMaterialsExternalIdIsItsHandleWhenTheFileCarriesNoGuid )
{
    // MaterialService keys the mesh->material link by the material's EXTERNAL id
    // (GetAssetHandleByExternal). A `.demat` with no MaterialId used to leave that id unset, which under
    // the random default meant the material was registered under a number that changed every launch — so
    // the link resolved in the session that wrote it and missed after a restart. The two ids must be the
    // same value, and this is the assertion that says so.
    //
    // A REAL file with no MaterialId in it, because that is the case under test. It is written rather than
    // fixtured because the whole point is a material the importer never stamped, which by definition is
    // not something the repository ships.
    const auto scratch = std::filesystem::temp_directory_path() / "desert_assethandlestability_noguid.demat";
    {
        std::ofstream out( scratch );
        ASSERT_TRUE( out.is_open() ) << "could not write the fixture at " << scratch.string();
        out << R"({"Params":[],"Textures":[]})";
    }

    Desert::Assets::SurfaceMaterialAsset material( AssetPriority::Medium, Common::Filepath( scratch ) );
    ASSERT_TRUE( material.Load().IsSuccess() );
    std::filesystem::remove( scratch );

    EXPECT_EQ( static_cast<uint64_t>( material.GetMaterialUUID() ),
               static_cast<uint64_t>( material.GetMetadata().Handle ) )
         << "a material's external id and its handle disagree; the mesh->material link resolves through "
            "the external id and would miss after a restart";
    EXPECT_FALSE( material.GetMaterialUUID().IsNull() );
}

// The defect as a USER meets it: save a reference, restart, dereference it.
//
// This is the scenario spelled out literally rather than argued about. Process A registers a skybox and
// reports the handle a scene would have written down. Process B — a genuinely separate run, the "restart"
// — registers the same asset from the same path and asks the AssetManager for that number.
//
// Skybox is the subject because it was one of the five types that carried the random handle, and because
// its Load touches no file, so what is measured is identity and nothing else.
TEST( AssetHandleStability, AHandleSavedByOneRunResolvesInTheNext )
{
    const uint64_t saved = HandleOf<Desert::Assets::SkyboxAsset>( kPathA );

    const std::string verdict = ResolveInAnIndependentProcess( kPathA, saved );

    ASSERT_FALSE( verdict.empty() ) << "the child run printed nothing; the guard did not run at all";
    EXPECT_EQ( verdict, "RESOLVED" )
         << "a handle written down by one run did not resolve in the next. This is the defect exactly as "
            "an artist meets it: the scene still names the asset, the asset is still on disk, and the "
            "reference silently points at nothing.";
}

TEST( AssetHandleStability, AHandleFromNoAssetStillFailsToResolve )
{
    // The companion the test above needs to mean anything: if FindByHandle returned something for every
    // number, "RESOLVED" would be worthless.
    const std::string verdict = ResolveInAnIndependentProcess( kPathA, 12345ull );
    ASSERT_FALSE( verdict.empty() );
    EXPECT_EQ( verdict, "MISSED" );
}

// ---------------------------------------------------------------------------------------------------
// THE RELATION THIS SUITE EXISTS FOR: identity is a property of the asset's PLACE IN THE PROJECT, not
// of where the project happens to sit on somebody's disk.
//
// The tests above establish that one path gives one handle in runs that share nothing. They were all
// satisfied by hashing the path the caller held, and that is precisely what was wrong: every content
// root turns ABSOLUTE the moment a `.deproj` is opened (Constants::Path::SetProjectRoot), so the string
// being hashed began with a developer's home directory. Measured on the derivation as it stood:
// `Assets/Clouds/X.dcnv` -> 122788169303960361, the same file spelled absolutely -> 868888776058461864,
// and the absolute form is the one that reached the hash in a real session. Stable in two runs on ONE
// machine; a different number on every other machine, and a rename of the checkout directory was enough
// to change it here.
// ---------------------------------------------------------------------------------------------------

TEST( AssetHandleStability, OneAssetKeepsOneHandleUnderTwoUnrelatedProjectRoots )
{
    // THE guard of this task. Two developers, two checkouts that share no directory above the project,
    // and an assets folder that is not even called the same thing. Same asset, same handle, or a handle
    // is not a reference anyone can write down and send to another machine.
    ProjectRootGuard guard;

    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );
    const uint64_t fromAnnsMachine = HandleValue( "/ann/work/Game/Content/Clouds/Cumulus.dcnv" );

    Common::Constants::Path::SetProjectRoot( "/opt/ci/checkout/Game", "Assets" );
    const uint64_t fromTheBuildAgent = HandleValue( "/opt/ci/checkout/Game/Assets/Clouds/Cumulus.dcnv" );

    EXPECT_EQ( fromAnnsMachine, fromTheBuildAgent )
         << "the same asset derived two different handles under two project roots. A handle written into "
            "a file on one machine then names nothing on the other, which is the defect this suite is "
            "named after, one level up: stable per machine, meaningless between them.";
}

TEST( AssetHandleStability, TheHashedKeyCarriesNoPartOfTheProjectRoot )
{
    // The relation above stated positively, and the reason it holds: the string that reaches the hash is
    // the asset's place in the project and nothing else. Asserted on the key rather than on the number
    // because a failure here says WHAT leaked, where a mismatched uint64 only says that something did.
    ProjectRootGuard guard;

    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );
    EXPECT_EQ( Common::AssetHandle::StableKeyForPath( "/ann/work/Game/Content/Clouds/Cumulus.dcnv" ),
               "assets:Clouds/Cumulus.dcnv" );

    Common::Constants::Path::SetProjectRoot( "/opt/ci/checkout/Game", "Assets" );
    EXPECT_EQ( Common::AssetHandle::StableKeyForPath( "/opt/ci/checkout/Game/Assets/Clouds/Cumulus.dcnv" ),
               "assets:Clouds/Cumulus.dcnv" );

    // And the cooked tree, which moves with the project the same way.
    EXPECT_EQ( Common::AssetHandle::StableKeyForPath( "/opt/ci/checkout/Game/Cooked/Textures/T.tex" ),
               "cooked:Textures/T.tex" );
}

TEST( AssetHandleStability, AnAbsoluteAndARelativeSpellingOfOneAssetAgree )
{
    // The second required relation. It is not hypothetical: with a project open the roots are absolute
    // and most callers pass absolute paths, but shaders never do (SHADERDIR_PATH is const and is never
    // remapped), the prefab save box hardcodes a relative literal while the instantiate box defaults to
    // the absolute root, and .dpak entries arrive relative. One file under two spellings was two assets.
    ProjectRootGuard guard;

    const std::filesystem::path projectDir = std::filesystem::current_path() / "SpellingProbe";
    Common::Constants::Path::SetProjectRoot( projectDir, "Content" );

    const uint64_t absolute = HandleValue( projectDir / "Content" / "Clouds" / "Cumulus.dcnv" );
    const uint64_t relative = HandleValue( "SpellingProbe/Content/Clouds/Cumulus.dcnv" );

    EXPECT_EQ( absolute, relative )
         << "one file spelled absolutely and relatively derived two handles, so the editor and the "
            "preloader can disagree about which asset a slot points at";

    // The same claim once more with a `..` in the middle, because normalization and relativization are
    // two steps and only one of them used to happen.
    EXPECT_EQ( HandleValue( "SpellingProbe/Content/Types/../Clouds/Cumulus.dcnv" ), absolute );
}

TEST( AssetHandleStability, TheCookedTwinOfAnAssetIsNotTheSameAsset )
{
    // Why the key is tagged with its root. `Cooked/Textures/T.tex` and `Content/Textures/T.tex` both
    // reduce to `Textures/T.tex`, so an untagged relative key would hand one handle to two files -- a
    // collision that the old absolute-path hash could not produce and that a naive fix introduces.
    ProjectRootGuard guard;
    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );

    EXPECT_NE( HandleValue( "/ann/work/Game/Content/Textures/T.tex" ),
               HandleValue( "/ann/work/Game/Cooked/Textures/T.tex" ) )
         << "a content asset and a cooked asset at mirrored offsets collided onto one handle";
}

TEST( AssetHandleStability, EngineResourcesAreKeyedOnTheirOwnRootAndDoNotMoveWithTheProject )
{
    // Shaders live under RESOURCE_PATH, which is const and is never remapped, so their identity must not
    // change when a project is opened or swapped. This is the one asset family whose handle was already
    // portable before this change, and it has to stay that way.
    ProjectRootGuard guard;

    const uint64_t beforeAnyProject = HandleValue( "Resources/Shaders/Programs/PBR.shader" );
    EXPECT_EQ( Common::AssetHandle::StableKeyForPath( "Resources/Shaders/Programs/PBR.shader" ),
               "engine:Shaders/Programs/PBR.shader" );

    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );
    EXPECT_EQ( HandleValue( "Resources/Shaders/Programs/PBR.shader" ), beforeAnyProject )
         << "opening a project moved the shaders, which are not project content";

    Common::Constants::Path::SetProjectRoot( "/opt/ci/checkout/Other", "Assets" );
    EXPECT_EQ( HandleValue( "Resources/Shaders/Programs/PBR.shader" ), beforeAnyProject );
}

TEST( AssetHandleStability, TheDefaultSandboxNestsAssetsInsideResourcesAndAssetsStillWins )
{
    // With no project open ASSETS_PATH is `Resources/Assets/` -- INSIDE RESOURCE_PATH (`Resources/`).
    // Both roots contain the file, so the answer must not depend on which one the code happens to test
    // first. Longest match is what makes that true, and this is the case that proves it.
    ProjectRootGuard guard;
    Common::Constants::Path::ASSETS_PATH = "Resources/Assets/";

    EXPECT_EQ( Common::AssetHandle::StableKeyForPath( "Resources/Assets/Clouds/Cumulus.dcnv" ),
               "assets:Clouds/Cumulus.dcnv" )
         << "a content asset was keyed as an engine resource because a shorter root matched first";
}

TEST( AssetHandleStability, KeysThatAreNotFilesystemPathsAreLeftAlone )
{
    // Procedural animation clips and sequencer clips register under `procedural://` / `memory://` keys.
    // They are identities already, not locations, and relativizing them would be meaningless -- worse,
    // absolutizing them would make them depend on the process's working directory, which is a
    // regression this change must not introduce.
    ProjectRootGuard guard;

    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );
    const uint64_t underOneProject = HandleValue( "procedural://humanoid/Walk" );

    Common::Constants::Path::SetProjectRoot( "/opt/ci/checkout/Other", "Assets" );
    EXPECT_EQ( HandleValue( "procedural://humanoid/Walk" ), underOneProject );

    EXPECT_NE( HandleValue( "procedural://humanoid/Walk" ), HandleValue( "procedural://humanoid/Run" ) );

    // The key is the spelling itself, normalized, and nothing else has been glued to it.
    //
    // This line and the working-directory test below exist because the first version of this test did
    // NOT catch its own sabotage: made to absolutize the no-root fallback, the suite stayed green. Both
    // assertions it had were blind to that -- one used an already-absolute path, where absolutizing is
    // the identity, and the other only compared two project roots, which does not move the working
    // directory. A relation that survives its own diversion is not a guard.
    EXPECT_EQ( Common::AssetHandle::StableKeyForPath( "procedural://humanoid/Walk" ),
               std::filesystem::path( "procedural://humanoid/Walk" ).lexically_normal().generic_string() );
}

TEST( AssetHandleStability, ASyntheticKeyDoesNotFollowTheWorkingDirectory )
{
    // The property above as a process meets it. A `procedural://` clip is an identity, not a location;
    // if the derivation resolved it against the working directory then the editor (which runs from
    // Editor/) and a packaged runtime (which does not) would register the same clip under two handles,
    // and the animation a scene names would resolve in one and vanish in the other.
    ProjectRootGuard guard;
    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );

    std::error_code   ec;
    const std::filesystem::path original = std::filesystem::current_path( ec );
    ASSERT_FALSE( ec ) << "could not read the working directory";

    const uint64_t here = HandleValue( "procedural://humanoid/Walk" );

    std::filesystem::current_path( std::filesystem::temp_directory_path(), ec );
    ASSERT_FALSE( ec ) << "could not change the working directory";
    const uint64_t elsewhere = HandleValue( "procedural://humanoid/Walk" );
    std::filesystem::current_path( original, ec );

    EXPECT_EQ( here, elsewhere )
         << "a synthetic key changed identity because the process changed directory";
}

TEST( AssetHandleStability, AFileOutsideTheProjectKeepsItsOwnSpelling )
{
    // An asset a user picked from a save dialog somewhere else on disk has no place in the project, so
    // there is no project-relative identity to give it. It keeps the spelling it came with, which is
    // what ComponentRegistry does in the same situation and says so in the same words. The alternative
    // -- inventing a `../../..` key relative to the assets root -- would encode the distance between two
    // unrelated directories, which is machine-specific in exactly the way this change is removing.
    ProjectRootGuard guard;
    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );

    EXPECT_EQ( Common::AssetHandle::StableKeyForPath( "/elsewhere/scratch/Hand.dcnv" ),
               "/elsewhere/scratch/Hand.dcnv" );
    EXPECT_EQ( HandleValue( "/elsewhere/scratch/Hand.dcnv" ),
               static_cast<uint64_t>( Common::AssetHandle::FromKey( "/elsewhere/scratch/Hand.dcnv" ) ) );
}

TEST( AssetHandleStability, TwoAssetsUnderOneRootStillDiffer )
{
    // The companion every stability assertion needs: a derivation that ignored the relative part
    // entirely and hashed only the tag would satisfy every test above and collapse the whole library
    // onto three handles.
    ProjectRootGuard guard;
    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );

    EXPECT_NE( HandleValue( "/ann/work/Game/Content/Clouds/Cumulus.dcnv" ),
               HandleValue( "/ann/work/Game/Content/Clouds/Stratus.dcnv" ) );
    EXPECT_NE( HandleValue( "/ann/work/Game/Content/A/X.dcnv" ),
               HandleValue( "/ann/work/Game/Content/B/X.dcnv" ) );
}

TEST( AssetHandleStability, EveryAssetTypeAgreesAcrossProjectRoots )
{
    // The relation applied to the catalogue rather than to one call, because the point of deriving
    // identity in AssetBase is that no type gets to have its own answer.
    ProjectRootGuard guard;

    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );
    std::vector<uint64_t> first;
    for ( const auto& kind : Catalogue() )
        first.push_back( kind.Handle( "/ann/work/Game/Content/Library/Subject.asset" ) );

    Common::Constants::Path::SetProjectRoot( "/opt/ci/checkout/Game", "Assets" );
    for ( size_t i = 0; i < Catalogue().size(); ++i )
    {
        EXPECT_EQ( Catalogue()[i].Handle( "/opt/ci/checkout/Game/Assets/Library/Subject.asset" ), first[i] )
             << Catalogue()[i].Name << " changed identity when the project moved";
    }
}

// ---------------------------------------------------------------------------------------------------
// WHY THE RE-STAMP NEEDED NO MIGRATION.
//
// Making the derivation project-relative changes the number every path-derived handle takes. That is
// only safe because no file in the repository refers to a path-derived handle BY NUMBER, and the two
// classes whose numbers ARE written down — Texture2D and Material — do not take theirs from the path at
// all: they read an id out of the file and overwrite what AssetBase installed. The audit behind the
// first half of that claim found exactly five persisted path-derived ids in committed assets, and all
// five were texture references, i.e. the second half.
//
// So the migration is the absence of one, and these two tests are what makes that an assertion rather
// than a hope: if either class ever stopped carrying its own id, the re-stamp WOULD move a number that a
// `.demat` has written down, and this suite says so before a material silently loses its textures.
// ---------------------------------------------------------------------------------------------------

TEST( AssetHandleStability, ATexturesIdComesFromItsFileAndSurvivesTheProjectMoving )
{
    // A real cooked `.tex`, because the claim is about what Load does with the file's Handle field.
    const auto scratch = std::filesystem::temp_directory_path() / "desert_assethandlestability_rooted.tex";
    constexpr uint64_t kIdInTheFile = 16135626166276358966ull; // the value T_Checker.tex actually carries
    {
        std::ofstream out( scratch );
        ASSERT_TRUE( out.is_open() ) << "could not write the fixture at " << scratch.string();
        out << R"({"Handle":16135626166276358966,"SourcePath":"","CookedPath":"","Width":4,"Height":4,)"
            << R"("Channels":4,"Format":"RGBA8F"})";
    }

    ProjectRootGuard guard;

    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );
    Desert::Assets::TextureAsset underOneRoot( AssetPriority::Medium, Common::Filepath( scratch ) );
    ASSERT_TRUE( underOneRoot.Load().IsSuccess() );

    Common::Constants::Path::SetProjectRoot( "/opt/ci/checkout/Game", "Assets" );
    Desert::Assets::TextureAsset underAnother( AssetPriority::Medium, Common::Filepath( scratch ) );
    ASSERT_TRUE( underAnother.Load().IsSuccess() );

    std::filesystem::remove( scratch );

    EXPECT_EQ( static_cast<uint64_t>( underOneRoot.GetMetadata().Handle ), kIdInTheFile );
    EXPECT_EQ( static_cast<uint64_t>( underAnother.GetMetadata().Handle ), kIdInTheFile )
         << "a texture stopped taking its identity from its own file. Every `.demat` in the repository "
            "names its textures by that number, so the moment it becomes path-derived a change to the "
            "derivation silently empties every texture slot.";
}

TEST( AssetHandleStability, AMaterialsIdComesFromItsFileAndSurvivesTheProjectMoving )
{
    // The same claim for the other class that carries its own id. `MaterialId` is what a mesh's
    // surface and a scene's material override both key on.
    const auto scratch = std::filesystem::temp_directory_path() / "desert_assethandlestability_rooted.demat";
    constexpr uint64_t kIdInTheFile = 6418972230554417713ull; // M_CheckerFloor.demat's actual MaterialId
    {
        std::ofstream out( scratch );
        ASSERT_TRUE( out.is_open() ) << "could not write the fixture at " << scratch.string();
        out << R"({"Params":[],"Textures":[],"MaterialId":6418972230554417713})";
    }

    ProjectRootGuard guard;

    Common::Constants::Path::SetProjectRoot( "/ann/work/Game", "Content" );
    Desert::Assets::SurfaceMaterialAsset underOneRoot( AssetPriority::Medium, Common::Filepath( scratch ) );
    ASSERT_TRUE( underOneRoot.Load().IsSuccess() );

    Common::Constants::Path::SetProjectRoot( "/opt/ci/checkout/Game", "Assets" );
    Desert::Assets::SurfaceMaterialAsset underAnother( AssetPriority::Medium, Common::Filepath( scratch ) );
    ASSERT_TRUE( underAnother.Load().IsSuccess() );

    std::filesystem::remove( scratch );

    EXPECT_EQ( static_cast<uint64_t>( underOneRoot.GetMetadata().Handle ), kIdInTheFile );
    EXPECT_EQ( static_cast<uint64_t>( underAnother.GetMetadata().Handle ), kIdInTheFile );
    EXPECT_EQ( static_cast<uint64_t>( underAnother.GetMaterialUUID() ), kIdInTheFile );
}

// ---------------------------------------------------------------------------------------------------
// The census: the catalogue above against the enum it claims to cover.
// ---------------------------------------------------------------------------------------------------

TEST( AssetHandleStability, TheCatalogueCoversEveryAssetTypeId )
{
    // Every enumerator of AssetTypeID except Unknown, which is the absence of a type and has no asset
    // class. Written out rather than counted from the enum so that RENAMING an enumerator is also caught.
    const std::vector<AssetTypeID> declared = {
         AssetTypeID::Mesh,
         AssetTypeID::Material,
         AssetTypeID::Texture2D,
         AssetTypeID::Skybox,
         AssetTypeID::Shader,
         AssetTypeID::Skeleton,
         AssetTypeID::Animation,
         AssetTypeID::Prefab,
         AssetTypeID::CloudNoiseVolume,
         AssetTypeID::CloudType,
         AssetTypeID::CloudModellingVolume,
         AssetTypeID::CloudLayout,
    };

    // AssetTypeID::Count is the enum's own tally and exists for this assertion. Naming the last real
    // enumerator instead would not work: appending a type after it leaves the arithmetic unchanged and the
    // census silently passes, which is the failure mode this suite is here to make impossible.
    // The +1 is Unknown, which is the absence of a type.
    const size_t enumerators = static_cast<size_t>( AssetTypeID::Count );
    EXPECT_EQ( declared.size() + 1, enumerators )
         << "AssetTypeID has gained or lost an enumerator. A new asset type must be added to this suite's "
            "catalogue, or its handle stability is untested — which is exactly how five types kept a "
            "random per-launch identity for as long as they did.";

    std::set<int> covered;
    for ( const auto& kind : Catalogue() )
        covered.insert( static_cast<int>( kind.Type ) );

    for ( const auto type : declared )
    {
        // Prefab is the one declared type with no catalogue entry: PrefabAsset's out-of-line members reach
        // Core::Scene and through it the whole renderer, so constructing one here would drag Vulkan into a
        // unit test. Its handle comes from AssetBase like every other type's — the first test in this
        // section is what proves that claim for the shared mechanism — and its END-TO-END stability is
        // covered by the AssetReferenceRoundTrip suite, which exercises the real prefab path.
        if ( type == AssetTypeID::Prefab )
            continue;

        EXPECT_TRUE( covered.count( static_cast<int>( type ) ) != 0 )
             << "AssetTypeID " << static_cast<int>( type ) << " has no entry in the catalogue";
    }
}

int main( int argc, char** argv )
{
    // The child branch. Deliberately before InitGoogleTest: this invocation is not a test run, it is one
    // half of the measurement the cross-process test makes.
    if ( argc >= 3 && std::strcmp( argv[1], kPrintHandlesFlag ) == 0 )
    {
        for ( const auto& kind : Catalogue() )
            printf( "%llu\n", static_cast<unsigned long long>( kind.Handle( argv[2] ) ) );
        return 0;
    }

    // The "restart" half of the round-trip: a fresh process, a fresh AssetManager, and a handle that came
    // from somewhere else entirely — which is what a saved scene is.
    if ( argc >= 4 && std::strcmp( argv[1], kResolveFlag ) == 0 )
    {
        Desert::Assets::AssetManager manager;
        manager.CreateAsset<Desert::Assets::SkyboxAsset>( AssetPriority::Medium, Common::Filepath( argv[2] ) );

        const uint64_t wanted = std::strtoull( argv[3], nullptr, 10 );
        const auto     found  = manager.FindByHandle<Desert::Assets::SkyboxAsset>( Common::AssetHandle( wanted ) );
        printf( "%s\n", found ? "RESOLVED" : "MISSED" );
        return 0;
    }

    g_ExecutablePath = argv[0];

    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
