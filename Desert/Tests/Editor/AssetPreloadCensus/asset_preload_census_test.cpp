// EVERY `Preload*` THE ASSET LAYER DECLARES IS CALLED BY BOTH LAYERS THAT START THE ENGINE.
//
// WHY THIS SUITE EXISTS, AND IT IS THE STRONGEST ARGUMENT IN IT. `AssetPreloader::PreloadCloudLayouts`
// was written with the painted layout, scanned `Clouds/Layouts`, registered every `.dclayout` with the
// service, and was CALLED BY NOBODY. Both layers list the preloads themselves, one at a time, in an
// order they need to control; the new one was added to a `PreloadAllAssets()` helper that had had no
// caller since 2023. So the whole feature was dead end to end from the day it shipped: every scene
// binding a painting logged "referenced but not registered" and rendered its sky procedurally, and every
// test of the format, the bake and the panel passed, because not one of them starts a layer.
//
// That is the defect shape this project keeps paying for — both ends correct, the link between them
// missing — and it is exactly the kind no unit test of either end can see. The relation is between a
// class's DECLARATIONS and two call sites in files no header includes, so it is asserted by reading the
// sources, the way TextureSourceFormatCensus reads them next door.
//
// WHAT WOULD MAKE THIS RED, and each is a real mistake: adding a `Preload*` and wiring it into one layer
// only (a packaged game silently missing that content); adding one and wiring it into neither; deleting
// a call from a layer while the method stays.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // The two files that start the engine. There is no third: the editor's startup stage list and the
    // runtime's straight-line sequence are the only places a preload is ever asked for.
    constexpr const char* kPreloaderHeader = "Desert/Desert/Source/Engine/Assets/AssetPreloader.hpp";
    constexpr const char* kLayers[] = { "Editor/Source/EditorLayer.cpp", "Runtime/Source/RuntimeLayer.cpp" };

    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + kPreloaderHeader );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadFile( const std::string& path )
    {
        std::ifstream in( path, std::ios::binary );
        if ( !in )
            return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Declarations, not definitions: the header is the census, and a method declared and never defined
    // would not link anyway. Comment lines are skipped so that a method NAMED in a comment — this
    // header carries one, explaining the deleted `PreloadAllAssets` — is not read as a declaration.
    std::vector<std::string> DeclaredPreloads( const std::string& header )
    {
        std::vector<std::string> names;
        const std::regex         pattern( R"(^\s*void\s+(Preload[A-Za-z0-9_]*)\s*\()" );
        std::istringstream       lines( header );
        std::string              line;
        while ( std::getline( lines, line ) )
        {
            const size_t first = line.find_first_not_of( " \t" );
            if ( first != std::string::npos && line.compare( first, 2, "//" ) == 0 )
                continue;

            std::smatch match;
            if ( std::regex_search( line, match, pattern ) )
                names.push_back( match[1].str() );
        }
        return names;
    }
} // namespace

TEST( AssetPreloadCensus, TheHeaderStillDeclaresPreloadsAtAll )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "could not locate the repository root from the working directory";

    const std::vector<std::string> declared = DeclaredPreloads( ReadFile( root + kPreloaderHeader ) );

    // A census that found nothing would pass the test below for the wrong reason — the failure mode of
    // every source-scanning suite, and the one it has to rule out about itself first.
    EXPECT_GE( declared.size(), 5u ) << "the scan found " << declared.size() << " Preload* declarations in "
                                     << kPreloaderHeader
                                     << ", which means the parse stopped matching rather than that the "
                                        "asset layer shrank";
}

TEST( AssetPreloadCensus, EveryPreloadIsCalledByBothLayers )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() );

    const std::vector<std::string> declared = DeclaredPreloads( ReadFile( root + kPreloaderHeader ) );
    ASSERT_FALSE( declared.empty() );

    for ( const char* layer : kLayers )
    {
        const std::string source = ReadFile( root + layer );
        ASSERT_FALSE( source.empty() ) << "could not read " << layer;

        for ( const std::string& name : declared )
            EXPECT_NE( source.find( name + "()" ), std::string::npos )
                 << layer << " never calls AssetPreloader::" << name
                 << "(). A preload nothing calls is content that silently never loads: the scenes that "
                    "reference it log one line and render without it, and no test of the asset, the "
                    "format or the panel can see it. Add the call, or delete the method.";
    }
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
