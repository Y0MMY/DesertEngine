// The packaging <-> scanning relation. Constants.hpp declared FONTS_PATH and ICONS_PATH, the font
// and icon services scanned them, and the game packager packed three OTHER trees — so a packaged
// game shipped without a single .ttf and the first frame with text had nothing to draw with. Both
// ends were individually "correct"; the missing property was the RELATION between what the packager
// puts into Content.dpak and what the runtime scanners go looking for. That relation is what this
// suite asserts, three ways:
//
//   1. Census: every root the scanners enumerate is a tree the packager packs.
//   2. Keys: every packed tree's archive key prefix is exactly the key a runtime lookup of that
//      tree produces under the package root — remapped trees through the regenerated .deproj's
//      AssetsRoot, resource trees through their own (never-remapped) relative paths.
//   3. End to end: BuildContentPak() over a real (temp) project, the pak mounted in a bare
//      "package" directory, and the scanners' own enumeration finding the font, the icon and the
//      scene inside it.

#include <Editor/Packaging/GamePackager.hpp>
#include <Editor/Packaging/PackageCook.hpp>
#include <Editor/Packaging/PackagedContentTrees.hpp>

#include <Engine/Core/ShaderCompiler/ShaderCacheKey.hpp>
#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>
#include <Engine/Core/ShaderCompiler/ShaderSpirvCache.hpp>
#include <Engine/Project/ProjectContext.hpp>
#include <Engine/Runtime/Services/ServiceScanRoots.hpp>
#include <Engine/Text/FontCache.hpp>
#include <Engine/Vector/IconBake.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/VFS.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    void WriteFile( const fs::path& p, const std::string& content )
    {
        fs::create_directories( p.parent_path() );
        std::ofstream out( p, std::ios::binary );
        out << content;
    }

    // The archive key a lookup of `dir` produces once the VFS normalizes it against the package
    // root — the same relation VFS::KeyFor implements.
    std::string KeyUnder( const fs::path& root, const fs::path& dir )
    {
        const fs::path rel = dir.lexically_normal().lexically_relative( root.lexically_normal() );
        std::string    key;
        for ( const auto& part : rel )
        {
            if ( part.empty() || part == "." )
                continue;
            if ( !key.empty() )
                key += '/';
            key += part.generic_string();
        }
        return key;
    }

    void SetEnv( const char* key, const std::string& value )
    {
#if defined( _WIN32 )
        _putenv_s( key, value.c_str() );
#else
        setenv( key, value.c_str(), 1 );
#endif
    }

    // Restores cwd, HOME and the (global) project-root remap, whatever the test body did.
    struct EnvironmentGuard
    {
        fs::path    OldCwd  = fs::current_path();
        std::string OldHome = std::getenv( "HOME" ) ? std::getenv( "HOME" ) : "";
        ~EnvironmentGuard()
        {
            std::error_code ec;
            fs::current_path( OldCwd, ec );
            if ( !OldHome.empty() )
                SetEnv( "HOME", OldHome );
            Common::Utils::VFS::Unmount();
            // Back to the built-in sandbox mapping the process started with.
            Common::Constants::Path::SetProjectRoot( "", "Resources/Assets" );
        }
    };
} // namespace

TEST( PackagedContent, EveryScannedRootIsAPackagedTree )
{
    const auto trees = Desert::Editor::PackagedContentTrees();

    // Pointer identity, not path equality: the scanners and the packager must read the SAME live
    // constant, so a project remap can never split the two.
    const auto packed = [&]( const fs::path* root )
    {
        for ( const auto& t : trees )
            if ( t.Tree == root )
                return true;
        return false;
    };

    for ( const fs::path* root : Desert::Runtime::FontScanRoots() )
        EXPECT_TRUE( packed( root ) ) << "font scan root not packaged: " << root->string();
    for ( const fs::path* root : Desert::Runtime::IconScanRoots() )
        EXPECT_TRUE( packed( root ) ) << "icon scan root not packaged: " << root->string();
}

TEST( PackagedContent, PakKeysAreTheRuntimeLookupKeysUnderThePackageRoot )
{
    EnvironmentGuard guard;

    // Simulate the packaged game's world: Game.deproj opened from the package dir remaps the content
    // trees under it, the launcher cds there, and every resource path resolves against it.
    const fs::path pkg = fs::temp_directory_path() / "desert_pkgkeys";
    Common::Constants::Path::SetProjectRoot( pkg, Desert::Editor::kPackagedAssetsRoot );

    for ( const auto& t : Desert::Editor::PackagedContentTrees() )
    {
        const fs::path lookup = t.Tree->is_absolute() ? *t.Tree : pkg / *t.Tree;
        EXPECT_EQ( KeyUnder( pkg, lookup ), std::string( t.PakKey ) )
             << "tree " << t.Tree->string() << " is packed under a key its own lookup cannot reach";
    }
}

TEST( PackagedContent, BuildContentPakPacksWhatTheScannersFind )
{
    EnvironmentGuard guard;

    const fs::path base = fs::temp_directory_path() / "desert_pkg_e2e";
    fs::remove_all( base );
    const fs::path proj = base / "proj";
    const fs::path pkg  = base / "pkg";

    // ---- the DEV side: a project with one scene, plus engine resources next to the editor's cwd.
    // AssetsRoot is deliberately NOT "Assets", so the test also proves the packer rebases content
    // into the packaged root rather than echoing the dev layout.
    WriteFile( proj / "GameAssets" / "Scenes" / "level.desce", "scene-body" );
    WriteFile( proj / "Resources" / "Fonts" / "fake.ttf", "font-body" );
    WriteFile( proj / "Resources" / "Icons" / "fake.svg", "icon-body" );
    WriteFile( proj / "T.deproj", "{\"Name\":\"T\",\"AssetsRoot\":\"GameAssets\",\"DefaultScene\":\"\"}" );

    SetEnv( "HOME", base.string() ); // keep RegisterRecent out of the real user config
    fs::current_path( proj );        // relative resource trees resolve against the editor cwd
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( proj / "T.deproj" ).string() ) );

    const auto result = Desert::Editor::BuildContentPak();
    ASSERT_TRUE( result.Success ) << result.Message;

    // ---- the PACKAGED side: a bare directory holding ONLY the pak and the regenerated descriptor —
    // no loose content at all, exactly what a player's machine has.
    fs::create_directories( pkg );
    fs::copy_file( proj / "Content.dpak", pkg / "Content.dpak" );
    WriteFile( pkg / "Game.deproj", std::string( "{\"Name\":\"T\",\"AssetsRoot\":\"" ) +
                                         Desert::Editor::kPackagedAssetsRoot + "\",\"DefaultScene\":\"\"}" );

    fs::current_path( pkg );
    ASSERT_TRUE( Common::Utils::VFS::MountPak( pkg / "Content.dpak" ) );
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( pkg / "Game.deproj" ).string() ) );

    // The scanners' own enumeration: roots from ServiceScanRoots, both halves via ListFilesRecursive.
    const auto findByExt = []( const std::array<const fs::path*, 2>& roots, const char* ext )
    {
        std::vector<fs::path> out;
        for ( const fs::path* root : roots )
            for ( const auto& p : Common::Utils::FileSystem::ListFilesRecursive( *root ) )
                if ( p.extension() == ext )
                    out.push_back( p );
        return out;
    };

    const auto fonts = findByExt( Desert::Runtime::FontScanRoots(), ".ttf" );
    ASSERT_EQ( fonts.size(), 1u ) << "the packed font tree is invisible to the font scan";
    EXPECT_EQ( fonts[0].filename(), "fake.ttf" );
    // ...and the path the scan produced actually READS, which is what FontService::Get does next.
    EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( fonts[0] ), "font-body" );

    const auto icons = findByExt( Desert::Runtime::IconScanRoots(), ".svg" );
    ASSERT_EQ( icons.size(), 1u ) << "the packed icon tree is invisible to the icon scan";
    EXPECT_EQ( icons[0].filename(), "fake.svg" );

    // Project content went in under the packaged AssetsRoot and comes back out of the remapped root.
    const auto assets = Common::Utils::FileSystem::ListFilesRecursive( Common::Constants::Path::ASSETS_PATH );
    ASSERT_EQ( assets.size(), 1u );
    EXPECT_EQ( assets[0].filename(), "level.desce" );
    EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( assets[0] ), "scene-body" );
}

// ---- The COOKED-CACHE relation ------------------------------------------------------------------------
//
// П2's defect, stated as the relation these tests pin: what the packager cooks into the archive must be
// what the runtime's cache lookups read back out of it. Both halves used to be individually "correct" —
// the census packed the whole Cooked/ tree, and the runtime kept a working cache — but the lookups were
// raw ifstreams, so a packaged game (no loose Cooked/ at all) recompiled every shader and rebaked every
// atlas at every cold start, with its warm cache sitting unread in the mounted pak.

// A cooked artifact of each kind, stored through the packager's own store seam, packed by
// BuildContentPak, and read back through the runtime's own lookup — in a bare directory whose only
// content is the archive, exactly what a player's machine has.
TEST( PackagedContent, CookedArtifactsTravelFromThePackagerToTheRuntimeLookup )
{
    EnvironmentGuard guard;

    const fs::path base = fs::temp_directory_path() / "desert_pkg_cooked";
    fs::remove_all( base );
    const fs::path proj = base / "proj";
    const fs::path pkg  = base / "pkg";

    WriteFile( proj / "T.deproj", "{\"Name\":\"T\",\"AssetsRoot\":\"GameAssets\",\"DefaultScene\":\"\"}" );
    SetEnv( "HOME", base.string() );
    fs::create_directories( proj / "GameAssets" );
    fs::current_path( proj );
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( proj / "T.deproj" ).string() ) );

    // The dev side stores one artifact of each kind, exactly as the cook does.
    const std::vector<uint32_t> spirv    = { 0x07230203u, 1u, 2u, 3u };
    const uint64_t              spirvKey = 0xA5A5A5A5DEADBEEFull;
    Desert::Core::StoreCachedSpirv( spirvKey, spirv );

    Desert::Text::BakedFont font;
    font.AtlasWidth        = 2;
    font.AtlasHeight       = 2;
    font.AtlasR8           = { 10, 20, 30, 40 };
    font.PixelHeight       = Desert::Text::kDefaultBakePixelHeight;
    const uint64_t fontKey = Desert::Text::FontCacheKey( { 1, 2, 3 }, font.PixelHeight, {} );
    Desert::Text::StoreBakedFont( Desert::Text::FontCachePath( fontKey ), font );

    Desert::Vector::BakedIcon icon;
    icon.Aspect = 2.0f;
    icon.Layers.push_back(
         { std::vector<uint8_t>( Desert::Vector::kIconCellDim * Desert::Vector::kIconCellDim, 7 ), 0x11223344u } );
    const uint64_t iconKey = Desert::Vector::IconCacheKey( { 4, 5, 6 } );
    Desert::Vector::StoreBakedIcon( Desert::Vector::IconCachePath( iconKey ), icon );

    const auto result = Desert::Editor::BuildContentPak();
    ASSERT_TRUE( result.Success ) << result.Message;

    // The packaged side: pak + descriptor, nothing loose.
    fs::create_directories( pkg );
    fs::copy_file( proj / "Content.dpak", pkg / "Content.dpak" );
    WriteFile( pkg / "Game.deproj", std::string( "{\"Name\":\"T\",\"AssetsRoot\":\"" ) +
                                         Desert::Editor::kPackagedAssetsRoot + "\",\"DefaultScene\":\"\"}" );
    fs::current_path( pkg );
    ASSERT_TRUE( Common::Utils::VFS::MountPak( pkg / "Content.dpak" ) );
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( pkg / "Game.deproj" ).string() ) );

    // The runtime's own lookups, byte for byte, with no loose Cooked/ anywhere.
    const auto loadedSpirv = Desert::Core::TryLoadCachedSpirv( spirvKey );
    ASSERT_TRUE( loadedSpirv.has_value() ) << "the packed SPIR-V cache is invisible to the runtime's cache lookup";
    EXPECT_EQ( *loadedSpirv, spirv );

    Desert::Text::BakedFont loadedFont;
    ASSERT_TRUE( Desert::Text::TryLoadBakedFont( Desert::Text::FontCachePath( fontKey ), loadedFont ) )
         << "the packed font atlas is invisible to the runtime's cache lookup";
    EXPECT_EQ( loadedFont.AtlasR8, font.AtlasR8 );
    EXPECT_EQ( loadedFont.PixelHeight, font.PixelHeight );

    Desert::Vector::BakedIcon loadedIcon;
    ASSERT_TRUE( Desert::Vector::TryLoadBakedIcon( Desert::Vector::IconCachePath( iconKey ), loadedIcon ) )
         << "the packed icon bake is invisible to the runtime's cache lookup";
    ASSERT_EQ( loadedIcon.Layers.size(), 1u );
    EXPECT_EQ( loadedIcon.Layers[0].Sdf, icon.Layers[0].Sdf );
    EXPECT_EQ( loadedIcon.Layers[0].RGBA, icon.Layers[0].RGBA );
    EXPECT_EQ( loadedIcon.Aspect, icon.Aspect );

    // The archive keys, spelled BY HAND. The load/store pair above shares one path function, so a
    // mutation of that function alone (renaming "ShaderCache", dropping the "Cooked" prefix) would
    // move both ends together and stay green — these three literals are the external contract that
    // must not drift, because every already-shipped archive spells its entries this way.
    EXPECT_TRUE(
         Common::Utils::VFS::ReadFile( pkg / "Cooked" / "ShaderCache" / std::format( "{:016x}.spv", spirvKey ) )
              .has_value() );
    EXPECT_TRUE(
         Common::Utils::VFS::ReadFile( pkg / "Cooked" / "FontCache" / std::format( "{:016x}.dfont", fontKey ) )
              .has_value() );
    EXPECT_TRUE(
         Common::Utils::VFS::ReadFile( pkg / "Cooked" / "IconCache" / std::format( "{:016x}.dicon", iconKey ) )
              .has_value() );
}

// The cook produces artifacts under the very keys the runtime computes when it loads the same shader —
// over a real (minimal) DSL shader, through the real preprocessor and the real compiler. A cook that
// hashed different inputs, assembled stages differently, or keyed for the wrong profile would leave
// this lookup cold, which is precisely the shipped-cache-dead-on-arrival failure П2 was.
TEST( PackagedContent, TheCookCompilesWhatTheRuntimeWillAskFor )
{
    EnvironmentGuard guard;

    const fs::path base = fs::temp_directory_path() / "desert_pkg_cook";
    fs::remove_all( base );
    const fs::path proj = base / "proj";

    const char* kProbeShader = "Shader \"CookProbe\"\n"
                               "{\n"
                               "    Domain Surface\n"
                               "    Vertex\n"
                               "    {\n"
                               "        In(0) vec3 a_Position;\n"
                               "        void main() { gl_Position = vec4( a_Position, 1.0 ); }\n"
                               "    }\n"
                               "    Fragment\n"
                               "    {\n"
                               "        Out(0) vec4 o_Color;\n"
                               "        void main() { o_Color = vec4( 1.0 ); }\n"
                               "    }\n"
                               "}\n";

    WriteFile( proj / "Resources" / "Shaders" / "CookProbe.shader", kProbeShader );
    WriteFile( proj / "T.deproj", "{\"Name\":\"T\",\"AssetsRoot\":\"GameAssets\",\"DefaultScene\":\"\"}" );
    SetEnv( "HOME", base.string() );
    fs::create_directories( proj / "GameAssets" );
    fs::current_path( proj );
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( proj / "T.deproj" ).string() ) );

    // The packager's cook, at this build's own profile (what BuildContentPak passes).
    const auto stats = Desert::Editor::CookContentCaches( Desert::Core::SpirvDebugInfoThisBuild() );
    EXPECT_EQ( stats.ShadersCompiled, 2u ) << "vertex + fragment of the one probe shader";
    EXPECT_EQ( stats.Failures, 0u );

    // The runtime's side of the relation: assemble the same stages the way VulkanShader::Reload does
    // and ask the cache with the runtime's own key overload. Every stage must already be there.
    const fs::path    shaderFile = fs::path( "Resources" ) / "Shaders" / "CookProbe.shader";
    const std::string content    = Common::Utils::FileSystem::ReadFileContent( shaderFile );
    ASSERT_FALSE( content.empty() );

    const auto stages =
         Desert::Core::Preprocess::ShaderPreprocess::PreProcessProgramPass( content, shaderFile, "" );
    ASSERT_EQ( stages.size(), 2u );
    for ( const auto& [stage, source] : stages )
    {
        const uint64_t key = Desert::Core::ComputeShaderCacheKey( stage, source, shaderFile );
        EXPECT_TRUE( Desert::Core::TryLoadCachedSpirv( key ).has_value() )
             << "the cook left the " << static_cast<int>( stage )
             << " stage cold — the runtime would recompile it at startup";
    }

    // And the cook is incremental: a second pass finds everything under its key and compiles nothing.
    const auto again = Desert::Editor::CookContentCaches( Desert::Core::SpirvDebugInfoThisBuild() );
    EXPECT_EQ( again.ShadersCompiled, 0u );
    EXPECT_EQ( again.ShadersCached, 2u );
}

// A cook that could not WRITE what it produced must not report it as cooked. Without this the
// packager's own summary is the defect in miniature: it says the shader was compiled, the pak packs
// the directory that does not contain it, and the first place anyone learns otherwise is a player's
// slow startup — which is exactly the failure П2 is about, arriving one artifact at a time.
//
// The store is made to fail the way a read-only install makes it fail: the directory the artifact
// must go in cannot be created, because a FILE already occupies that name.
TEST( PackagedContent, ACookThatCannotWriteDoesNotReportTheArtifactAsCooked )
{
    EnvironmentGuard guard;

    const fs::path base = fs::temp_directory_path() / "desert_pkg_unwritable";
    fs::remove_all( base );
    const fs::path proj = base / "proj";

    WriteFile( proj / "Resources" / "Shaders" / "CookProbe.shader",
               "Shader \"CookProbe\"\n"
               "{\n"
               "    Domain Surface\n"
               "    Vertex\n"
               "    {\n"
               "        In(0) vec3 a_Position;\n"
               "        void main() { gl_Position = vec4( a_Position, 1.0 ); }\n"
               "    }\n"
               "    Fragment\n"
               "    {\n"
               "        Out(0) vec4 o_Color;\n"
               "        void main() { o_Color = vec4( 1.0 ); }\n"
               "    }\n"
               "}\n" );
    WriteFile( proj / "T.deproj", "{\"Name\":\"T\",\"AssetsRoot\":\"GameAssets\",\"DefaultScene\":\"\"}" );
    SetEnv( "HOME", base.string() );
    fs::create_directories( proj / "GameAssets" );
    fs::current_path( proj );
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( proj / "T.deproj" ).string() ) );

    // Occupy Cooked/ShaderCache with a regular file, so create_directories cannot make the folder
    // and every store into it fails.
    const fs::path cacheDir = Desert::Core::SpirvCachePathForKey( 0 ).parent_path();
    fs::create_directories( cacheDir.parent_path() );
    WriteFile( cacheDir, "not a directory" );
    ASSERT_TRUE( fs::is_regular_file( cacheDir ) );

    const auto stats = Desert::Editor::CookContentCaches( Desert::Core::SpirvDebugInfoThisBuild() );

    EXPECT_EQ( stats.ShadersCompiled, 0u ) << "an artifact that never reached the disk was counted as cooked";
    EXPECT_EQ( stats.StoreFailures, 2u ) << "vertex + fragment, each produced and each unwritten";
    EXPECT_EQ( stats.Failures, 0u ) << "the shader compiles fine — this is a WRITE failure, not a bad shader";
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
