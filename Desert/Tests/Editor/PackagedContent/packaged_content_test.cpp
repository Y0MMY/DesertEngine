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
#include <Editor/Packaging/PackagedContentTrees.hpp>

#include <Engine/Project/ProjectContext.hpp>
#include <Engine/Runtime/Services/ServiceScanRoots.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/VFS.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
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

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
