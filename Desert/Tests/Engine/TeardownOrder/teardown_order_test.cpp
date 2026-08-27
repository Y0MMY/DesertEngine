// Guards on how the engine SHUTS DOWN.
//
// WHY THIS SUITE EXISTS. Every headless capture used to end in exit status 139 after the PNG was already
// on disk, and the three faults behind it were all the same shape: something released a GPU handle, or
// locked a mutex, at a moment when the thing it was talking to no longer existed.
//
//   1. The application was owned by Common::Singleton<T>, a namespace-scope static, so it was destroyed
//      at __cxa_finalize -- after every library the Vulkan loader had dlopen'ed during main. Its
//      vkDeviceWaitIdle entered the validation layer and locked a destroyed shared_mutex.
//   2. Application's members were declared window-first and context-last, so they died context-first:
//      VulkanImage2D::Release() dereferenced an expired renderer context to reach the VMA allocator.
//   3. std::exit() was called from inside a running frame, which runs the static destructors underneath
//      the job system's live worker threads.
//
// None of the three is a function whose return value can be asserted. All three are two places that have
// to agree with each other, which is the defect class DEV_CONTRACT 2.3.1 says a unit test never catches
// and a RELATION test does. The relations below are therefore read out of the source files themselves.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // The repository root, found by walking up from the test binary's working directory, as
    // Tests/Engine/ShippedShaderPasses and Tests/Engine/ShaderCacheKey do.
    const std::filesystem::path& RepoRoot()
    {
        static const std::filesystem::path root = []() -> std::filesystem::path
        {
            std::filesystem::path here = std::filesystem::current_path();
            for ( int up = 0; up < 8 && !std::filesystem::exists( here / "Desert" / "Desert" / "Source" ); ++up )
                here = here.parent_path();
            return here;
        }();
        return root;
    }

    std::string ReadFile( const std::filesystem::path& path )
    {
        std::ifstream     in( path, std::ios::binary );
        std::stringstream out;
        out << in.rdbuf();
        return out.str();
    }

    // Every //-comment line removed. The relations below are about CODE, and three of the comments that
    // explain them quote the very construct they forbid.
    std::string StripLineComments( const std::string& text )
    {
        std::string       out;
        std::stringstream in( text );
        std::string       line;
        while ( std::getline( in, line ) )
        {
            const auto first = line.find_first_not_of( " \t" );
            if ( first != std::string::npos && line.compare( first, 2, "//" ) == 0 )
                continue;
            out += line;
            out += '\n';
        }
        return out;
    }

    // Names captured from every occurrence of `Get<Name>Service` in @p text.
    std::set<std::string> ServiceNames( const std::string& text )
    {
        std::set<std::string> names;
        for ( size_t at = text.find( "Get" ); at != std::string::npos; at = text.find( "Get", at + 1 ) )
        {
            const size_t end = text.find( "Service", at );
            if ( end == std::string::npos )
                continue;
            const std::string name = text.substr( at + 3, end - at - 3 );
            // A name, not a sentence: anything with punctuation in it came from prose, not a call.
            const bool isIdentifier =
                 !name.empty() && std::all_of( name.begin(), name.end(),
                                               []( unsigned char c ) { return std::isalnum( c ) != 0; } );
            if ( isIdentifier )
                names.insert( name );
        }
        return names;
    }

    // The text between the braces of the first function whose signature contains @p signature, with
    // //-comments removed. Empty string when the signature is not there at all.
    std::string FunctionBody( const std::string& source, const std::string& signature )
    {
        const size_t at = source.find( signature );
        if ( at == std::string::npos )
            return {};
        const size_t open = source.find( '{', at );
        if ( open == std::string::npos )
            return {};

        int    depth = 0;
        size_t i     = open;
        for ( ; i < source.size(); ++i )
        {
            if ( source[i] == '{' )
                ++depth;
            else if ( source[i] == '}' && --depth == 0 )
                break;
        }
        if ( i >= source.size() )
            return {};
        return StripLineComments( source.substr( open + 1, i - open - 1 ) );
    }

    bool IsBlank( const std::string& text )
    {
        return text.find_first_not_of( " \t\r\n" ) == std::string::npos;
    }

    std::vector<std::filesystem::path> ServiceSources()
    {
        std::vector<std::filesystem::path> files;
        const auto root = RepoRoot() / "Desert" / "Desert" / "Source" / "Engine" / "Runtime" / "Services";
        if ( !std::filesystem::exists( root ) )
            return files;
        for ( const auto& entry : std::filesystem::recursive_directory_iterator( root ) )
        {
            const auto& p = entry.path();
            if ( entry.is_regular_file() && p.extension() == ".cpp" &&
                 p.stem().string().size() > 7 &&
                 p.stem().string().compare( p.stem().string().size() - 7, 7, "Service" ) == 0 )
                files.push_back( p );
        }
        std::sort( files.begin(), files.end() );
        return files;
    }
} // namespace

TEST( TeardownOrder, TheRepositoryRootWasFound )
{
    ASSERT_TRUE( std::filesystem::exists( RepoRoot() / "Desert" / "Desert" / "Source" ) )
         << "walked up from " << std::filesystem::current_path().string() << " and found no source tree; "
            "every other test in this suite would pass vacuously";
}

// RELATION: the members of Application die in reverse declaration order, and the window owns the
// swapchain, its framebuffers and their images -- objects that belong to the device and are freed through
// the context's VMA allocator. So the window must be declared LAST of the three.
//
// This is the relation whose violation was the segfault: with m_Window declared first it died last, and
// VulkanImage2D::Release() went looking for an allocator whose owner had already been destroyed.
TEST( TeardownOrder, ApplicationDeclaresTheWindowAfterTheDeviceAndTheContext )
{
    const std::string header =
         ReadFile( RepoRoot() / "Desert" / "Desert" / "Source" / "Engine" / "Core" / "Application.hpp" );
    ASSERT_FALSE( header.empty() ) << "Application.hpp not found or empty";

    // Matched on the member TYPE, not on the member name: `return m_Window;` inside the accessor is an
    // earlier occurrence of the name and would make this test read the wrong order.
    const size_t context = header.find( "shared_ptr<Graphic::RendererContext>" );
    const size_t device  = header.find( "shared_ptr<Device>" );
    const size_t window  = header.find( "shared_ptr<Window>" );

    ASSERT_NE( context, std::string::npos ) << "no m_RendererContext declaration in Application.hpp";
    ASSERT_NE( device, std::string::npos ) << "no m_Device declaration in Application.hpp";
    ASSERT_NE( window, std::string::npos ) << "no m_Window declaration in Application.hpp";

    EXPECT_LT( context, device ) << "m_Device is declared before m_RendererContext, so the context dies "
                                    "first and the device's teardown reaches a freed allocator";
    EXPECT_LT( device, window ) << "m_Window is declared before m_Device, so the window -- and the "
                                   "swapchain images it owns -- dies AFTER the device that owns them";
}

// RELATION: ResourceRegistry exposes N services and ClearAll() has to release all N. Each is a
// function-local static, destroyed at __cxa_finalize, i.e. after ~Application has destroyed the VkDevice:
// a service that ClearAll() forgets is a GPU handle released through a dangling allocator.
//
// Two places that must agree, in one file, with nothing that makes them agree by construction.
TEST( TeardownOrder, EveryResourceServiceIsClearedByClearAll )
{
    const auto dir = RepoRoot() / "Desert" / "Desert" / "Source" / "Engine" / "Runtime";
    const std::string header = StripLineComments( ReadFile( dir / "ResourceRegistry.hpp" ) );
    const std::string source = ReadFile( dir / "ResourceRegistry.cpp" );
    ASSERT_FALSE( header.empty() ) << "ResourceRegistry.hpp not found or empty";

    const std::set<std::string> exposed = ServiceNames( header );
    ASSERT_GE( exposed.size(), 10u ) << "read only " << exposed.size()
                                     << " service getters out of ResourceRegistry.hpp -- the parse, not "
                                        "the registry, is what is wrong";

    const std::string clearAll = FunctionBody( source, "ResourceRegistry::ClearAll" );
    ASSERT_FALSE( clearAll.empty() ) << "ResourceRegistry::ClearAll has no body";

    const std::set<std::string> cleared = ServiceNames( clearAll );

    for ( const std::string& name : exposed )
        EXPECT_TRUE( cleared.count( name ) == 1 )
             << "ResourceRegistry exposes Get" << name << "Service() but ClearAll() never clears it; its "
                "GPU objects would be released after the device is destroyed";
}

// RELATION: a service's Clear() is what ClearAll() relies on, so an EMPTY Clear() makes the test above
// pass while releasing nothing. Four of them were empty bodies -- Texture, Material, Shader and Skybox,
// which are the four holding the heaviest GPU objects in the engine. A stub that satisfies its caller by
// name is worse than a missing function, because the caller looks correct.
TEST( TeardownOrder, NoResourceServiceClearIsAnEmptyBody )
{
    const auto files = ServiceSources();
    ASSERT_GE( files.size(), 10u ) << "found only " << files.size() << " *Service.cpp files";

    for ( const auto& file : files )
    {
        const std::string source = ReadFile( file );
        const std::string name   = file.stem().string();
        const std::string body   = FunctionBody( source, name + "::Clear" );

        EXPECT_FALSE( body.empty() ) << file.string() << ": no " << name << "::Clear() definition";
        EXPECT_FALSE( IsBlank( body ) )
             << file.string() << ": " << name
             << "::Clear() has an empty body, so ResourceRegistry::ClearAll() releases nothing here";
    }
}

// RELATION: std::exit() runs the static destructors on the calling thread. Called from inside a frame it
// runs them underneath the job system's live workers and underneath a live VkDevice -- `--scene <missing>`
// exited 134 ("the engine aborted") instead of the 2 it meant to report, because nine worker threads threw
// "recursive_mutex lock failed: Invalid argument" on the way out.
//
// The only place it is legitimate is CreateApplication, which runs before the application, the job system
// and the device exist and so has nothing to tear down in order. Anywhere else, Application::Close(status)
// is the way out.
TEST( TeardownOrder, StdExitAppearsOnlyBeforeTheApplicationExists )
{
    const std::vector<std::filesystem::path> trees = {
        RepoRoot() / "Desert" / "Desert" / "Source", RepoRoot() / "Desert" / "Common" / "Source",
        RepoRoot() / "Editor" / "Source", RepoRoot() / "Runtime" / "Source" };

    // Both hold a CreateApplication and nothing else that runs during a frame.
    const std::set<std::string> allowed = { "Sandbox.hpp", "Main.cpp" };

    size_t scanned = 0;
    for ( const auto& tree : trees )
    {
        ASSERT_TRUE( std::filesystem::exists( tree ) ) << tree.string() << " does not exist";
        for ( const auto& entry : std::filesystem::recursive_directory_iterator( tree ) )
        {
            const auto& p = entry.path();
            if ( !entry.is_regular_file() || ( p.extension() != ".cpp" && p.extension() != ".hpp" ) )
                continue;
            ++scanned;
            if ( allowed.count( p.filename().string() ) == 1 )
                continue;

            const std::string code = StripLineComments( ReadFile( p ) );
            EXPECT_EQ( code.find( "std::exit(" ), std::string::npos )
                 << p.string() << " calls std::exit() outside CreateApplication. Inside a running frame "
                                  "that destroys the job system's mutexes under its own worker threads; "
                                  "use Application::Close(status) instead.";
        }
    }

    EXPECT_GT( scanned, 100u ) << "only " << scanned << " sources scanned -- the walk, not the engine, is "
                                  "what is wrong";
}

// Only gtest is linked, not gtest_main — every suite in this tree brings its own entry point.
int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
