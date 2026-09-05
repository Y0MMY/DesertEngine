// A TEXTURE REFERENCE THAT SURVIVES THE TRIP — a suite about a RELATION, not about a function.
//
// The relation: THE HANDLE A SCENE STORES MUST COME BACK AS THE SAME TEXTURE, on a machine that shares
// no directory with the one that wrote it. Both halves belong to it — a name that only this computer can
// read is not a reference, and neither is a name that reads back as a different (or a null) asset.
//
// What was broken, measured before anything was changed:
//
//   1. `MakeAssetResolver::ToPath`'s TextureAsset branch wrote `GetMetadata().Filepath` VERBATIM. Every
//      content root turns absolute the moment a `.deproj` is opened, so the string that reached the file
//      was `/Users/<somebody>/.../Cooked/Textures/T.tex` — the exact defect the MaterialAsset branch
//      beside it had already been fixed for, in a file that gets committed. The material's fix could not
//      be copied here: a material lives under ASSETS_PATH and a cooked texture under COOKED_PATH, a
//      SIBLING of it, where `relative(path, ASSETS_PATH)` gives `../Cooked/...` and falls back to the
//      absolute spelling anyway. That is why the stored form is the root-TAGGED key.
//   2. The read side was `FindByPath` and nothing else. That lookup compares filepaths VERBATIM (unlike
//      CreateAsset, which deduplicates on the spelling-independent stable key), so a miss was ordinary
//      and the branch answered it with a bare `0` — no asset created, nothing logged. Three separate
//      spellings of a real cooked texture were reported as failing to resolve, and not one said why.
//
// Why this file exists at all. Both branches lived inside ComponentRegistry.cpp, which reaches the
// ResourceRegistry and through it the whole renderer, so NO suite in the repository could execute them.
// They are now in Engine/Core/Serialize/TextureSlot.cpp, which needs the AssetManager and nothing else,
// and this is that unit under test.

#include <gtest/gtest.h>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Core/Serialize/TextureSlot.hpp>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>

#include <spdlog/sinks/ostream_sink.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using Desert::Assets::AssetManager;
using Desert::Assets::AssetPriority;
using Desert::Assets::TextureAsset;
using Desert::Core::Serialize::TextureSlotFromPath;
using Desert::Core::Serialize::TextureSlotToPath;

namespace
{
    // A handle far above 2^53, which is what a real one looks like: a texture takes its id from the
    // `Handle` field of its own cooked file, and the ones in this repository are 19-digit numbers. The
    // value is the one measured going wrong through the JSON double round trip (5355760296319878840 came
    // back as 5355760296319879168), so a regression there shows up here as well as in the serializer's
    // own suite.
    constexpr uint64_t kProbeHandle = 5355760296319878840ull;
    constexpr uint64_t kOtherHandle = 5355760296319878841ull;

    // Restores every content root SetProjectRoot rewrites. They are process-wide mutable globals, so a
    // test that opens a project and walks away leaves every test after it measuring that project.
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

    // A REAL cooked `.tex`, because the claim is about what the loader does with the file's own Handle
    // field. The bytes are the shape TextureAsset::Load parses; nothing here reads a pixel.
    void WriteCookedTexture( const std::filesystem::path& at, uint64_t handle )
    {
        std::filesystem::create_directories( at.parent_path() );
        std::ofstream out( at );
        ASSERT_TRUE( out.is_open() ) << "could not write the fixture at " << at.string();
        out << R"({"Handle":)" << handle
            << R"(,"SourcePath":"","CookedPath":"","Width":4,"Height":4,"Channels":4,"Format":"RGBA8F"})";
    }

    // Two developers' checkouts, sharing no directory above the project and not even agreeing on what the
    // assets folder is called. Everything below is built inside one of these.
    struct Checkout
    {
        std::filesystem::path Dir;
        std::filesystem::path AssetsRootName;
    };

    std::filesystem::path ScratchRoot()
    {
        return std::filesystem::temp_directory_path() / "desert_texture_slot_round_trip";
    }

    Checkout MakeCheckout( const char* who, const char* assetsRootName )
    {
        const Checkout c{ ScratchRoot() / who, assetsRootName };
        std::filesystem::create_directories( c.Dir );
        return c;
    }

    void Open( const Checkout& c )
    {
        Common::Constants::Path::SetProjectRoot( c.Dir, c.AssetsRootName );
    }

    // Captures everything the logger emits for the duration of one call. The default logger is restored
    // afterwards, because a test that leaves a sink behind silences every test after it.
    class LogCapture
    {
    public:
        LogCapture() : m_Previous( spdlog::default_logger() )
        {
            auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>( m_Stream );
            spdlog::set_default_logger( std::make_shared<spdlog::logger>( "capture", std::move( sink ) ) );
            spdlog::set_level( spdlog::level::trace );
        }

        ~LogCapture()
        {
            spdlog::set_default_logger( m_Previous );
        }

        LogCapture( const LogCapture& )            = delete;
        LogCapture& operator=( const LogCapture& ) = delete;

        std::string Text() const
        {
            return m_Stream.str();
        }

    private:
        std::ostringstream              m_Stream;
        std::shared_ptr<spdlog::logger> m_Previous;
    };
} // namespace

// ---------------------------------------------------------------------------------------------------
// THE RELATION.
// ---------------------------------------------------------------------------------------------------

TEST( TextureSlotRoundTrip, AHandleStoredOnOneMachineNamesTheSameTextureOnAnother )
{
    ProjectRootGuard guard;
    std::filesystem::remove_all( ScratchRoot() );

    // --- the machine that saves the scene --------------------------------------------------------
    const Checkout ann = MakeCheckout( "ann", "Content" );
    WriteCookedTexture( ann.Dir / "Cooked" / "Textures" / "T_Probe.tex", kProbeHandle );
    Open( ann );

    AssetManager annsManager;
    const auto   annsTexture = annsManager.CreateAsset<TextureAsset>(
         AssetPriority::Medium, Common::Filepath( ann.Dir / "Cooked" / "Textures" / "T_Probe.tex" ) );
    ASSERT_NE( annsTexture, nullptr );
    const uint64_t saved = static_cast<uint64_t>( annsTexture->GetMetadata().Handle );
    ASSERT_EQ( saved, kProbeHandle ) << "a texture's identity comes from its own file; the fixture is wrong";

    const std::string stored = TextureSlotToPath( annsManager, saved );

    // What actually goes into the file. Asserted as a VALUE and not merely as "not absolute", because
    // "not absolute" is also true of the empty string this branch used to produce for an unknown type.
    EXPECT_EQ( stored, "cooked:Textures/T_Probe.tex" );

    // --- the machine that opens it ----------------------------------------------------------------
    const Checkout ci = MakeCheckout( "ci", "Assets" );
    WriteCookedTexture( ci.Dir / "Cooked" / "Textures" / "T_Probe.tex", kProbeHandle );
    Open( ci );

    AssetManager   cisManager;
    const uint64_t loaded = TextureSlotFromPath( cisManager, stored );

    EXPECT_EQ( loaded, saved ) << "a texture reference written down by one checkout did not come back as "
                                  "the same texture in another. This is the defect as an artist meets it: "
                                  "the scene still names the image, the image is still on disk, and the "
                                  "slot is empty.";

    // And it is THIS checkout's file that was resolved, not a stale record of the other one.
    const auto resolved = cisManager.FindByHandle<TextureAsset>( Common::AssetHandle( loaded ) );
    ASSERT_NE( resolved, nullptr );
    EXPECT_EQ( resolved->GetMetadata().Filepath.lexically_normal(),
               ( ci.Dir / "Cooked" / "Textures" / "T_Probe.tex" ).lexically_normal() );

    std::filesystem::remove_all( ScratchRoot() );
}

TEST( TextureSlotRoundTrip, TheStoredFormCarriesNoPartOfTheMachineItWasWrittenOn )
{
    // The property the round trip above rests on, asserted directly so a failure says WHAT leaked rather
    // than only that something did. 42 of 50 shipped scenes once carried a home directory this way.
    ProjectRootGuard guard;
    std::filesystem::remove_all( ScratchRoot() );

    const Checkout ann = MakeCheckout( "ann", "Content" );
    WriteCookedTexture( ann.Dir / "Cooked" / "Textures" / "T_Probe.tex", kProbeHandle );
    Open( ann );

    AssetManager manager;
    const auto   texture = manager.CreateAsset<TextureAsset>(
         AssetPriority::Medium, Common::Filepath( ann.Dir / "Cooked" / "Textures" / "T_Probe.tex" ) );
    ASSERT_NE( texture, nullptr );

    const std::string stored =
         TextureSlotToPath( manager, static_cast<uint64_t>( texture->GetMetadata().Handle ) );

    EXPECT_EQ( stored.find( ann.Dir.generic_string() ), std::string::npos )
         << "the stored reference '" << stored << "' contains the checkout directory";
    EXPECT_FALSE( std::filesystem::path( stored ).is_absolute() ) << stored;

    std::filesystem::remove_all( ScratchRoot() );
}

TEST( TextureSlotRoundTrip, AContentTextureAndACookedOneTakeDifferentRootsAndBothComeBack )
{
    // Both roots, because the whole reason the stored form is TAGGED is that a texture can sit under
    // either and the two are siblings. A form relative to the assets root can only spell one of them.
    ProjectRootGuard guard;
    std::filesystem::remove_all( ScratchRoot() );

    const Checkout ann = MakeCheckout( "ann", "Content" );
    WriteCookedTexture( ann.Dir / "Cooked" / "Textures" / "T_Cooked.tex", kProbeHandle );
    WriteCookedTexture( ann.Dir / "Content" / "Textures" / "T_Content.tex", kOtherHandle );
    Open( ann );

    AssetManager manager;
    ASSERT_NE( manager.CreateAsset<TextureAsset>(
                    AssetPriority::Medium,
                    Common::Filepath( ann.Dir / "Cooked" / "Textures" / "T_Cooked.tex" ) ),
               nullptr );
    ASSERT_NE( manager.CreateAsset<TextureAsset>(
                    AssetPriority::Medium,
                    Common::Filepath( ann.Dir / "Content" / "Textures" / "T_Content.tex" ) ),
               nullptr );

    EXPECT_EQ( TextureSlotToPath( manager, kProbeHandle ), "cooked:Textures/T_Cooked.tex" );
    EXPECT_EQ( TextureSlotToPath( manager, kOtherHandle ), "assets:Textures/T_Content.tex" );

    // And back, in a manager that knows nothing, which is what a cold start is.
    AssetManager fresh;
    EXPECT_EQ( TextureSlotFromPath( fresh, "cooked:Textures/T_Cooked.tex" ), kProbeHandle );
    EXPECT_EQ( TextureSlotFromPath( fresh, "assets:Textures/T_Content.tex" ), kOtherHandle );

    std::filesystem::remove_all( ScratchRoot() );
}

TEST( TextureSlotRoundTrip, TwoTexturesDoNotCollapseOntoOneReference )
{
    // The companion every round-trip assertion needs: a writer that returned one constant, or a reader
    // that answered every name with the first texture it had, would satisfy the tests above.
    ProjectRootGuard guard;
    std::filesystem::remove_all( ScratchRoot() );

    const Checkout ann = MakeCheckout( "ann", "Content" );
    WriteCookedTexture( ann.Dir / "Cooked" / "Textures" / "A.tex", kProbeHandle );
    WriteCookedTexture( ann.Dir / "Cooked" / "Textures" / "B.tex", kOtherHandle );
    Open( ann );

    AssetManager manager;
    ASSERT_NE( manager.CreateAsset<TextureAsset>(
                    AssetPriority::Medium, Common::Filepath( ann.Dir / "Cooked" / "Textures" / "A.tex" ) ),
               nullptr );
    ASSERT_NE( manager.CreateAsset<TextureAsset>(
                    AssetPriority::Medium, Common::Filepath( ann.Dir / "Cooked" / "Textures" / "B.tex" ) ),
               nullptr );

    const std::string a = TextureSlotToPath( manager, kProbeHandle );
    const std::string b = TextureSlotToPath( manager, kOtherHandle );
    EXPECT_NE( a, b );
    EXPECT_EQ( TextureSlotFromPath( manager, a ), kProbeHandle );
    EXPECT_EQ( TextureSlotFromPath( manager, b ), kOtherHandle );

    std::filesystem::remove_all( ScratchRoot() );
}

TEST( TextureSlotRoundTrip, EverySpellingOfOneFileResolvesToOneTexture )
{
    // The read side used to be `FindByPath` alone, and that lookup compares filepaths VERBATIM. So a
    // scene that named a preloaded texture by any other spelling of the same file missed it and got 0 —
    // while AssetManager::CreateAsset, one line away, deduplicates on the spelling-independent key and
    // would have found it. Two lookups that must agree about what "the same file" means, and did not.
    ProjectRootGuard guard;
    std::filesystem::remove_all( ScratchRoot() );

    const Checkout ann = MakeCheckout( "ann", "Content" );
    WriteCookedTexture( ann.Dir / "Cooked" / "Textures" / "T_Probe.tex", kProbeHandle );
    Open( ann );

    AssetManager manager;
    ASSERT_NE( manager.CreateAsset<TextureAsset>(
                    AssetPriority::Medium,
                    Common::Filepath( ann.Dir / "Cooked" / "Textures" / "T_Probe.tex" ) ),
               nullptr );

    EXPECT_EQ( TextureSlotFromPath( manager, "cooked:Textures/T_Probe.tex" ), kProbeHandle );
    EXPECT_EQ( TextureSlotFromPath( manager, ( ann.Dir / "Cooked" / "Textures" / "T_Probe.tex" ).string() ),
               kProbeHandle )
         << "the absolute spelling — what every file written before the tagged form carries — no longer "
            "resolves";
    EXPECT_EQ( TextureSlotFromPath( manager, "cooked:Textures/../Textures/T_Probe.tex" ), kProbeHandle );

    // One record, not four: the reader must not manufacture a second asset per spelling.
    EXPECT_EQ( manager.FindAllByType<TextureAsset>().size(), 1u );

    std::filesystem::remove_all( ScratchRoot() );
}

TEST( TextureSlotRoundTrip, AnUnsetSlotIsEmptyAndStaysUnset )
{
    // 0 is a MEANINGFUL value — "no texture" — so it must survive the trip as itself, and must not be
    // reported as a failure. This is the case that stops the logging below from becoming noise on every
    // empty slot of every scene.
    ProjectRootGuard guard;
    AssetManager     manager;

    LogCapture log;
    EXPECT_EQ( TextureSlotToPath( manager, 0 ), "" );
    EXPECT_EQ( TextureSlotFromPath( manager, "" ), 0u );
    EXPECT_EQ( log.Text(), "" ) << "an empty slot logged something; every scene has dozens of them";
}

// ---------------------------------------------------------------------------------------------------
// A MISS SPEAKS (DC §1.4).
// ---------------------------------------------------------------------------------------------------

TEST( TextureSlotRoundTrip, ANameThatResolvesToNothingSaysSoWithTheNameAndTheRoots )
{
    ProjectRootGuard guard;
    std::filesystem::remove_all( ScratchRoot() );

    const Checkout ann = MakeCheckout( "ann", "Content" );
    Open( ann );

    AssetManager manager;

    std::string text;
    uint64_t    resolved = 1;
    {
        LogCapture log;
        resolved = TextureSlotFromPath( manager, "cooked:Textures/NotThere.tex" );
        text     = log.Text();
    }

    EXPECT_EQ( resolved, 0u );
    EXPECT_NE( text.find( "cooked:Textures/NotThere.tex" ), std::string::npos )
         << "the failure did not name the reference that failed. Three spellings of a real texture were "
            "reported as not resolving and none of them said why, because this branch returned 0 in "
            "silence; a bare 0 is indistinguishable from an empty slot.\nlogged: "
         << text;
    EXPECT_NE( text.find( "Textures" ), std::string::npos ) << text;
}

TEST( TextureSlotRoundTrip, AHandleWithNoRegisteredTextureSaysSoRatherThanWritingAnEmptySlot )
{
    // The other direction of the same rule, and the more damaging one: writing "" for a handle that IS
    // set destroys the reference in the file, so the next load has nothing to fail on.
    ProjectRootGuard guard;
    AssetManager     manager;

    std::string text;
    std::string stored = "unset";
    {
        LogCapture log;
        stored = TextureSlotToPath( manager, kProbeHandle );
        text   = log.Text();
    }

    EXPECT_EQ( stored, "" );
    EXPECT_NE( text.find( std::to_string( kProbeHandle ) ), std::string::npos )
         << "the slot was written out empty without a word about the handle it lost.\nlogged: " << text;
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
