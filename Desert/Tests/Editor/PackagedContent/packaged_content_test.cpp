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
#include <Editor/Packaging/PackageTarget.hpp>
#include <Editor/Packaging/PackagedContentTrees.hpp>

#include <Engine/Core/ShaderCompiler/ShaderCacheKey.hpp>
#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>
#include <Engine/Core/ShaderCompiler/ShaderSpirvCache.hpp>
#include <Engine/Project/ProjectContext.hpp>
#include <Engine/Runtime/Services/ServiceScanRoots.hpp>
#include <Engine/Text/FontCache.hpp>
#include <Engine/Vector/IconBake.hpp>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/VFS.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../../TestSupport/result_assert.hpp"

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
    const auto mounted = Common::Utils::VFS::MountPak( pkg / "Content.dpak" );
    ASSERT_TRUE( mounted.IsSuccess() ) << mounted.GetError();
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
    DESERT_EXPECT_RESULT_EQ( Common::Utils::FileSystem::ReadFileContent( fonts[0] ), "font-body" );

    const auto icons = findByExt( Desert::Runtime::IconScanRoots(), ".svg" );
    ASSERT_EQ( icons.size(), 1u ) << "the packed icon tree is invisible to the icon scan";
    EXPECT_EQ( icons[0].filename(), "fake.svg" );

    // Project content went in under the packaged AssetsRoot and comes back out of the remapped root.
    const auto assets = Common::Utils::FileSystem::ListFilesRecursive( Common::Constants::Path::ASSETS_PATH );
    ASSERT_EQ( assets.size(), 1u );
    EXPECT_EQ( assets[0].filename(), "level.desce" );
    DESERT_EXPECT_RESULT_EQ( Common::Utils::FileSystem::ReadFileContent( assets[0] ), "scene-body" );
}

// ── A SCRIPT REFERENCE NAMES ONE FILE, LOOSE AND PACKAGED (I9) ───────────────────────────────────────
//
// THE RELATION, and it is a relation rather than a property of either side: the string a scene stores to
// name its `.lua` must resolve, in the development tree and in a mounted archive, to THE SAME FILE.
// Asserting only that it resolves in the editor is what let the defect live — that half was always true.
//
// WHAT WAS WRONG. Every other reference in a `.desce` is an AssetHandle hashed from `<tag>:<path relative
// to that root>`, so it survives the packager rebasing content under <package>/Assets/. A script slot was
// the one kind of content that named ITSELF with the rooted spelling the editor happened to be standing
// in, and a rooted spelling does not survive the rebase. I8 measured it on a mounted archive: the stored
// spelling gave Exists=0, the same file through the scripts root gave Exists=1. This is that measurement,
// turned into a suite, and it carries BOTH halves — the negative control below is the pre-migration
// spelling, and it must still fail, because a silence proves nothing until the noise is shown.
//
// The AssetsRoot is deliberately NOT "Assets", so the packaged root genuinely differs from the dev one
// and a test that merely echoed the dev layout could not pass.
TEST( PackagedContent, AScriptReferenceResolvesToTheSameFileLooseAndPackaged )
{
    EnvironmentGuard guard;

    const fs::path base = fs::temp_directory_path() / "desert_pkg_script_ref";
    fs::remove_all( base );
    const fs::path proj = base / "proj";
    const fs::path pkg  = base / "pkg";

    const std::string body = "-- MoveAlongX\nProperties = { Speed = 3 }\n";
    WriteFile( proj / "GameAssets" / "Scripts" / "Examples" / "MoveAlongX.lua", body );
    WriteFile( proj / "GameAssets" / "Scenes" / "level.desce", "scene-body" );
    WriteFile( proj / "T.deproj", "{\"Name\":\"T\",\"AssetsRoot\":\"GameAssets\",\"DefaultScene\":\"\"}" );

    SetEnv( "HOME", base.string() );
    fs::current_path( proj );
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( proj / "T.deproj" ).string() ) );

    // ---- the DEV side. The reference is minted exactly the way the Details panel's script picker mints
    // it: enumerate the census row for scripts, then StableKeyForPath over what the enumeration returned.
    std::vector<fs::path> found;
    for ( const auto& p : Common::Utils::FileSystem::ListFilesRecursive( Common::Constants::Path::SCRIPT_PATH ) )
        if ( p.extension() == ".lua" )
            found.push_back( p );
    ASSERT_EQ( found.size(), 1u ) << "the scripts census row does not see the project's own script";

    const std::string stored = Common::AssetHandle::StableKeyForPath( found[0] );
    EXPECT_EQ( stored, "assets:Scripts/Examples/MoveAlongX.lua" )
         << "the stored form must be root-tagged and relative, or it cannot survive the rebase";

    const fs::path loosePath = Common::AssetHandle::PathForStableKey( stored );
    ASSERT_TRUE( Common::Utils::FileSystem::Exists( loosePath ) ) << loosePath.string();
    DESERT_EXPECT_RESULT_EQ( Common::Utils::FileSystem::ReadFileContent( loosePath ), body );

    // Key -> path -> identity round-trips under THIS root: resolving the reference and re-deriving an
    // asset identity from what came back gives the identity the reference itself hashes to.
    EXPECT_EQ( Common::AssetHandle::FromCookedPath( loosePath ), Common::AssetHandle::FromKey( stored ) );

    // The spelling a v15 scene carried: the file as seen from the editor's working directory. Kept so the
    // negative control below is the ACTUAL old value and not an invented one.
    const fs::path preMigrationSpelling = fs::relative( loosePath, proj );
    ASSERT_FALSE( preMigrationSpelling.empty() );
    EXPECT_TRUE( Common::Utils::FileSystem::Exists( preMigrationSpelling ) )
         << "the old spelling resolved in the dev tree - that half was never the defect";

    // ---- the PACKAGED side: a bare directory holding only the archive and the regenerated descriptor.
    const auto result = Desert::Editor::BuildContentPak();
    ASSERT_TRUE( result.Success ) << result.Message;

    fs::create_directories( pkg );
    fs::copy_file( proj / "Content.dpak", pkg / "Content.dpak" );
    WriteFile( pkg / "Game.deproj", std::string( "{\"Name\":\"T\",\"AssetsRoot\":\"" ) +
                                         Desert::Editor::kPackagedAssetsRoot + "\",\"DefaultScene\":\"\"}" );

    fs::current_path( pkg );
    const auto mounted = Common::Utils::VFS::MountPak( pkg / "Content.dpak" );
    ASSERT_TRUE( mounted.IsSuccess() ) << mounted.GetError();
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( pkg / "Game.deproj" ).string() ) );

    const fs::path packagedPath = Common::AssetHandle::PathForStableKey( stored );
    ASSERT_TRUE( Common::Utils::FileSystem::Exists( packagedPath ) )
         << "the stored script reference resolves to nothing in the package: " << packagedPath.string();

    // THE RELATION ITSELF: one reference, two roots, the same file.
    DESERT_EXPECT_RESULT_EQ( Common::Utils::FileSystem::ReadFileContent( packagedPath ), body );

    // ...and the identity round trip holds under the PACKAGED root too, which is what puts a script on
    // the same footing as a texture or a mesh.
    //
    // NOT `FromCookedPath(loosePath) == FromCookedPath(packagedPath)`, which is what this assertion said
    // first and which failed for an honest reason worth writing down: a path-derived handle is only
    // meaningful while the project that path belongs to is OPEN. With the packaged project open, the dev
    // tree's absolute path lies under no content root at all, so StableKeyForPath hands it back verbatim
    // and it hashes to something else (measured: 3427774758061914252 vs 2091480530661102989). The
    // invariant is between the stored KEY and whatever that key resolves to here — never between two
    // absolute paths from two different roots.
    EXPECT_EQ( Common::AssetHandle::FromCookedPath( packagedPath ), Common::AssetHandle::FromKey( stored ) );

    // ...and the test is not vacuous: the two resolutions really are different places on disk, so the
    // equality above is a property of the reference and not of the layout having stayed put.
    EXPECT_NE( loosePath, packagedPath );

    // ---- the NEGATIVE CONTROL. The pre-migration spelling, unchanged, against the same mounted archive.
    // It must NOT resolve; if it did, this whole suite would be measuring nothing.
    EXPECT_FALSE( Common::Utils::FileSystem::Exists( preMigrationSpelling ) )
         << "the rooted spelling '" << preMigrationSpelling.string()
         << "' resolved inside the package, so this test cannot tell a fixed reference from a broken one";
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
    const auto mounted = Common::Utils::VFS::MountPak( pkg / "Content.dpak" );
    ASSERT_TRUE( mounted.IsSuccess() ) << mounted.GetError();
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
    const fs::path shaderFile = fs::path( "Resources" ) / "Shaders" / "CookProbe.shader";

    // Ф3 made the primitive return a ResultStr. Asserting on the read ITSELF rather than on an empty
    // string is the point of that change: a probe file this test cannot read is a broken fixture and
    // must say so by name, not fail three lines later as "the shader has no stages".
    const auto contentRead = Common::Utils::FileSystem::ReadFileContent( shaderFile );
    ASSERT_TRUE( static_cast<bool>( contentRead ) ) << contentRead.GetError();
    const std::string& content = contentRead.GetValue();
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

// ---- The HOST-TARGET relation -----------------------------------------------------------------------
//
// П6's other half. `PackageGame` used to state macOS in four independent places — the Runtime binary's
// name, the .app layout, the bash launcher and the build script named in its "not found" error — so on a
// Windows host it looked for a file that host can never produce (`Runtime`, not `Runtime.exe`) and told
// the reader to run a macOS shell script. All four now come out of one description
// (Editor/Packaging/PackageTarget.hpp), which is the same description the Build Settings panel shows.
//
// These two tests are what makes that a fact rather than an intention, and they check the produced
// ARTEFACTS rather than the constants: Desert/Tests/Editor/BuildSettingsConsumers already pins the
// table's own relations, and a table that is right while the packager ignores it is precisely the state
// this repair was opened from. PackageGame had no test of any kind before them.

TEST( PackagedContent, PackageGameProducesTheLauncherAndBinaryTheHostDescriptionNames )
{
    EnvironmentGuard guard;

    const Desert::Editor::TargetPlatformInfo& host = Desert::Editor::HostPlatformInfo();

    const fs::path base = fs::temp_directory_path() / "desert_pkg_hosttarget";
    fs::remove_all( base );
    const fs::path proj = base / "proj";

    WriteFile( proj / "GameAssets" / "Scenes" / "level.desce", "scene-body" );
    WriteFile( proj / "T.deproj", "{\"Name\":\"T\",\"AssetsRoot\":\"GameAssets\",\"DefaultScene\":\"\"}" );

    // The Runtime the packager copies. It looks one directory ABOVE the editor's cwd, which is why the
    // project sits inside `base` rather than being `base`.
    WriteFile( base / "build" / "Bin" / "Release" / host.RuntimeBinary, "not really a binary" );

    SetEnv( "HOME", base.string() );
    fs::current_path( proj );
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( proj / "T.deproj" ).string() ) );

    // The PLAIN layout, because it is the one every host has — the .app branch is macOS-only by
    // construction and asking for it elsewhere is refused (with a log line) rather than obeyed.
    Desert::Editor::PackageOptions options;
    options.OutputDir    = ( base / "out" ).string();
    options.Config       = "Release";
    options.MacAppBundle = false;

    const auto result = Desert::Editor::PackageGame( options );
    ASSERT_TRUE( result.Success ) << result.Message;

    const fs::path root = fs::path( result.PackageDir );
    EXPECT_TRUE( fs::exists( root / host.RuntimeBinary ) )
         << "the packaged player binary is not named what this host names it; a package whose executable "
            "has the wrong name cannot be started on the machine it was made for";
    EXPECT_TRUE( fs::exists( root / host.LauncherName ) )
         << "the package has no " << host.LauncherName
         << " — the launcher was written for a different host's shell";

    // ...and it is that host's shell, not merely that host's file name. The two can disagree, and a
    // `run.bat` full of bash is the failure the file name alone would not catch.
    const auto launcherRead = Common::Utils::FileSystem::ReadFileContent( root / host.LauncherName );
    ASSERT_TRUE( launcherRead.IsSuccess() )
         << "the launcher script did not read back: " << launcherRead.GetError();
    const std::string launcher = launcherRead.GetValue();
    ASSERT_FALSE( launcher.empty() );
    if ( host.Platform == Desert::Editor::TargetPlatform::Windows )
        EXPECT_NE( launcher.find( "@echo off" ), std::string::npos ) << launcher;
    else
        EXPECT_NE( launcher.find( "#!/usr/bin/env bash" ), std::string::npos ) << launcher;

    // The launcher has to actually name the binary beside it, or the package starts nothing.
    EXPECT_NE( launcher.find( host.RuntimeBinary ), std::string::npos ) << launcher;
}

// The refusal, and it is a refusal this suite can produce on any host: no Runtime was built.
//
// What is asserted is that the message names something the reader can RUN. It used to name
// scripts/MacOS/BuildMacOS.sh unconditionally, so on Windows it answered a question about a file that
// could never exist with an instruction that could never help.
TEST( PackagedContent, AMissingRuntimeIsRefusedByNamingThisHostsOwnBuildScript )
{
    EnvironmentGuard guard;

    const Desert::Editor::TargetPlatformInfo& host = Desert::Editor::HostPlatformInfo();

    const fs::path base = fs::temp_directory_path() / "desert_pkg_noruntime";
    fs::remove_all( base );
    const fs::path proj = base / "proj";

    WriteFile( proj / "T.deproj", "{\"Name\":\"T\",\"AssetsRoot\":\"GameAssets\",\"DefaultScene\":\"\"}" );
    fs::create_directories( proj / "GameAssets" );
    SetEnv( "HOME", base.string() );
    fs::current_path( proj );
    ASSERT_TRUE( Desert::Project::ProjectContext::Open( ( proj / "T.deproj" ).string() ) );

    Desert::Editor::PackageOptions options;
    options.OutputDir = ( base / "out" ).string();
    options.Config    = "Release"; // nothing was built into base/build/Bin/Release

    const auto result = Desert::Editor::PackageGame( options );
    ASSERT_FALSE( result.Success ) << "a package was produced with no Runtime binary to put in it";
    EXPECT_NE( result.Message.find( host.BuildScript ), std::string::npos )
         << "the refusal does not name a script this host can run: " << result.Message;
    EXPECT_NE( result.Message.find( host.RuntimeBinary ), std::string::npos )
         << "the refusal does not say which file was missing: " << result.Message;
}

// ── EVERY DECLARED ROOT SHIPS, OR SAYS OUT LOUD WHY IT IS NOT CONTENT ─────────────────────────────
//
// WHY THE CENSUS AT THE TOP OF THIS FILE WAS NOT ENOUGH, stated as what it missed. It walks the roots
// ONE function lists — `ServiceScanRoots` — which is why fonts and icons are safe and why nothing in
// this repository ever noticed that `Resources/Scripts/` existed, held every Lua example the editor
// offered, and was in NO tree the packager builds an archive from. Six scripts, three committed scenes
// naming one, and a packaged game that would have loaded none of them: measured by I8, found by hand,
// caught by no check. A root only enters that census by being scanned by a service; a root that some
// PANEL scans, or that a component field names, enters nothing.
//
// So the relation is turned the other way round and anchored at the DECLARATION instead. Constants.hpp
// is where a root comes into existence, so that is the list that cannot drift: every path constant it
// declares must either be covered by a tree in PackagedContentTrees() — itself or an ancestor of it —
// or carry a written reason why it is not a thing a game contains.
//
// THE ROW THAT DOES THE WORK IS RESOURCE_PATH'S. The engine tree is NOT shipped wholesale; three named
// subtrees below it are. So `Resources/Videos/` added tomorrow, scanned by whoever adds it, has exactly
// two ways past this suite: become a packed tree, or say in one sentence why a game does not need it.
// Neither is something you do by accident, which is the whole point — the previous answer was "nothing
// happens, and you find out when somebody packages the game".
namespace
{
    enum class RootVerdict
    {
        Packaged,   ///< a packed tree, or inside one
        NotContent, ///< deliberately not in the archive, for the stated reason
    };

    struct DeclaredRoot
    {
        const char*     Name; // exactly as Constants.hpp spells it
        const fs::path* Live; // the live constant, so a remap is followed rather than re-typed
        RootVerdict     What;
        const char*     Reason; // NotContent rows only; empty for the others
    };

    // Index over Constants.hpp's declarations. The completeness of THIS table is not trusted — the
    // first test below derives the real set from the header and refuses anything missing.
    const std::vector<DeclaredRoot>& DeclaredRoots()
    {
        namespace P                                  = Common::Constants::Path;
        static const std::vector<DeclaredRoot> roots = {
             // --- engine resources: never remapped, and only these three travel ---
             { "RESOURCE_PATH", &P::RESOURCE_PATH, RootVerdict::NotContent,
               "the engine tree's ROOT, and it is not shipped wholesale - only the three named subtrees "
               "below it are. Anything new placed under it is invisible to the packager until it becomes "
               "a tree of its own here AND in PackagedContentTrees(); Resources/Scripts/ was exactly that "
               "and shipped in nothing for as long as it existed." },
             { "SHADERDIR_PATH", &P::SHADERDIR_PATH, RootVerdict::Packaged, "" },
             { "FONTS_PATH", &P::FONTS_PATH, RootVerdict::Packaged, "" },
             { "ICONS_PATH", &P::ICONS_PATH, RootVerdict::Packaged, "" },

             // --- project content: every row is derived from the assets or cooked root, and both of
             //     those are packed trees, so the whole census travels by construction ---
             { "ASSETS_PATH", &P::ASSETS_PATH, RootVerdict::Packaged, "" },
             { "MESH_PATH", &P::MESH_PATH, RootVerdict::Packaged, "" },
             { "MATERIAL_PATH", &P::MATERIAL_PATH, RootVerdict::Packaged, "" },
             { "TEXTUREDIR_PATH", &P::TEXTUREDIR_PATH, RootVerdict::Packaged, "" },
             { "SKYBOX_PATH", &P::SKYBOX_PATH, RootVerdict::Packaged, "" },
             { "SCENE_PATH", &P::SCENE_PATH, RootVerdict::Packaged, "" },
             { "PREFAB_PATH", &P::PREFAB_PATH, RootVerdict::Packaged, "" },
             { "SCRIPT_PATH", &P::SCRIPT_PATH, RootVerdict::Packaged, "" },
             { "COLLECTIONS_PATH", &P::COLLECTIONS_PATH, RootVerdict::Packaged, "" },
             { "CLOUD_NOISE_PATH", &P::CLOUD_NOISE_PATH, RootVerdict::Packaged, "" },
             { "CLOUD_TYPE_PATH", &P::CLOUD_TYPE_PATH, RootVerdict::Packaged, "" },
             { "CLOUD_VOLUME_PATH", &P::CLOUD_VOLUME_PATH, RootVerdict::Packaged, "" },
             { "CLOUD_LAYOUT_PATH", &P::CLOUD_LAYOUT_PATH, RootVerdict::Packaged, "" },
             { "COOKED_PATH", &P::COOKED_PATH, RootVerdict::Packaged, "" },
             { "MESH_PATH_COOKED", &P::MESH_PATH_COOKED, RootVerdict::Packaged, "" },
             { "TEXTURE_PATH_COOKED", &P::TEXTURE_PATH_COOKED, RootVerdict::Packaged, "" },
        };
        return roots;
    }

    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 8; ++up )
        {
            std::ifstream probe( prefix + "Desert/Common/Source/Common/Core/Constants.hpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    // Every path constant Constants.hpp DECLARES, by name. A declaration is
    // `inline const std::filesystem::path[&] <NAME> =` — the `=` is what separates a declaration from
    // `Dir( ContentDir d )`, which has the same prefix and is a function.
    std::vector<std::string> DeclaredRootNamesInTheHeader( const std::string& source )
    {
        static const std::string kPrefix = "inline const std::filesystem::path";

        std::vector<std::string> names;
        for ( std::size_t at = source.find( kPrefix ); at != std::string::npos;
              at             = source.find( kPrefix, at + 1 ) )
        {
            std::size_t i = at + kPrefix.size();
            while ( i < source.size() && ( source[i] == '&' || source[i] == ' ' ) )
                ++i;
            const std::size_t nameStart = i;
            while ( i < source.size() &&
                    ( std::isalnum( static_cast<unsigned char>( source[i] ) ) != 0 || source[i] == '_' ) )
                ++i;
            if ( i == nameStart )
                continue;
            const std::string name = source.substr( nameStart, i - nameStart );

            std::size_t after = i;
            while ( after < source.size() && source[after] == ' ' )
                ++after;
            if ( after < source.size() && source[after] == '=' )
                names.push_back( name );
        }
        return names;
    }

    // Is `path` the packed tree `tree`, or inside it? Component-wise, because these paths carry a
    // trailing separator and a string prefix test would also match "Resources/AssetsOther/".
    bool IsAtOrInside( const fs::path& path, const fs::path& tree )
    {
        const fs::path rel = path.lexically_normal().lexically_relative( tree.lexically_normal() );
        if ( rel.empty() )
            return false;
        return *rel.begin() != "..";
    }
} // namespace

// 1. NO UNDECLARED ROW AND NO UNREGISTERED ROOT. The header is the source of truth in both directions:
//    a constant added there without a row here fails, and a row here whose constant is gone fails too.
TEST( PackagedContent, EveryRootConstantTheHeaderDeclaresIsInThePackagingRegister )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "the repository root was not found from the working directory";

    std::ifstream in( root + "Desert/Common/Source/Common/Core/Constants.hpp" );
    ASSERT_TRUE( in.is_open() );
    std::ostringstream buffer;
    buffer << in.rdbuf();

    const std::vector<std::string> declared = DeclaredRootNamesInTheHeader( buffer.str() );
    ASSERT_FALSE( declared.empty() ) << "no path constant was found in Constants.hpp, which cannot be "
                                        "true - the parser is broken, not the header";

    std::set<std::string> registered;
    for ( const DeclaredRoot& row : DeclaredRoots() )
        registered.insert( row.Name );

    for ( const std::string& name : declared )
    {
        EXPECT_EQ( registered.count( name ), 1u )
             << name
             << " is a content root the engine can read from and nothing says whether a PACKAGED GAME "
                "gets it. Add a row: Packaged (and a tree in PackagedContentTrees() that covers it), or "
                "NotContent with the reason a game does not need it.";
    }

    const std::set<std::string> present( declared.begin(), declared.end() );
    for ( const DeclaredRoot& row : DeclaredRoots() )
    {
        EXPECT_EQ( present.count( row.Name ), 1u )
             << row.Name << " is registered here but Constants.hpp no longer declares it - a stale row.";
    }
}

// 2. THE VERDICT IS TRUE, not merely written. Checked under the PACKAGED remap, because that is the
//    only world in which the answer matters and the dev-time spellings would flatter it.
TEST( PackagedContent, EveryRootCalledContentIsCoveredByATreeThePackagerPacks )
{
    EnvironmentGuard guard;

    const fs::path pkg = fs::temp_directory_path() / "desert_pkg_rootcensus";
    Common::Constants::Path::SetProjectRoot( pkg, Desert::Editor::kPackagedAssetsRoot );

    const auto trees = Desert::Editor::PackagedContentTrees();

    for ( const DeclaredRoot& row : DeclaredRoots() )
    {
        bool covered = false;
        for ( const auto& tree : trees )
            covered = covered || IsAtOrInside( *row.Live, *tree.Tree );

        if ( row.What == RootVerdict::Packaged )
        {
            EXPECT_TRUE( covered ) << row.Name << " (" << row.Live->string()
                                   << ") is registered as content that ships, and no tree in "
                                      "PackagedContentTrees() contains it. Everything under it is "
                                      "missing from the archive, and the failure lands on a player.";
        }
        else
        {
            EXPECT_STRNE( row.Reason, "" ) << row.Name << " is excluded from the package with no reason given.";
        }
    }
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
