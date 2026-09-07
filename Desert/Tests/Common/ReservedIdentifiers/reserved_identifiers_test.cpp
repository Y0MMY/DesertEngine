// THE MACROS WINDOWS DEFINES AWAY, AND WHY THIS IS A TEST RATHER THAN A CONVENTION.
//
// windef.h still carries `#define far` and `#define near` — empty, for 16-bit memory models nobody has
// targeted since 1995. Any translation unit that reaches windows.h therefore turns `const float far = x;`
// into `const float = x;`, and MSVC reports "no variable declared before '='" followed by a cascade of
// C2059/C2065/C2440 that reads like a broken test rather than an eaten identifier.
//
// Three suites had it at once (CloudShadow, CloudLighting, CloudGeometry) plus LensFlare, all copied from
// one example comment in Units.hpp that used `far` as its variable name. None of it is visible on macOS or
// Linux. The cost of finding it the other way is a Windows CI job: ~35 minutes to first error, and it
// blocks every other merge behind it because a red Windows is no evidence for anything.
//
// So the check runs HERE, on every platform, in milliseconds, over the source text — the same technique
// SettingConsumers uses. It reads the tree rather than compiling it, which is the only way to assert
// something about a compiler nobody in this repository runs locally.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    // Walk up until the marker is found: the test binary's working directory is not fixed.
    fs::path RepoRoot()
    {
        fs::path p = fs::current_path();
        for ( int i = 0; i < 8; ++i )
        {
            if ( fs::exists( p / "Desert" / "Common" ) && fs::exists( p / "Editor" ) )
                return p;
            p = p.parent_path();
        }
        return {};
    }

    std::vector<fs::path> SourceFiles( const fs::path& root )
    {
        std::vector<fs::path> out;
        std::error_code       ec;
        for ( const fs::path& sub : { fs::path( "Desert" ), fs::path( "Editor" ), fs::path( "Runtime" ) } )
        {
            for ( const auto& e : fs::recursive_directory_iterator( root / sub, ec ) )
            {
                if ( !e.is_regular_file() )
                    continue;
                const std::string ext = e.path().extension().string();
                if ( ext != ".cpp" && ext != ".hpp" && ext != ".h" )
                    continue;
                // Generated code is not authored here, and ThirdParty is not ours to rename.
                const std::string s = e.path().generic_string();
                if ( s.find( "/Generated/" ) != std::string::npos ||
                     s.find( "/ThirdParty/" ) != std::string::npos )
                    continue;
                out.push_back( e.path() );
            }
        }
        return out;
    }

    // A DECLARATION of one of these names, not a mention. Comments and string literals are left alone on
    // purpose — prose says "the far side" legitimately, and rewriting words inside comments is its own
    // defect in this repository.
    //
    // THE TYPE IS NOT ENUMERATED, AND THAT IS THE WHOLE POINT OF THIS VERSION. The first one listed the
    // types it knew — const/float/int/auto/vec3/glm::… — and MISSED `std::vector<Scored> near;` in the
    // control channel, which Windows then rejected an hour downstream. That is the same mistake twice: my
    // original hand grep also required `const <one lowercase word>` and could not see `const glm::vec3 far`.
    // A guard that enumerates what it knows about only ever catches what somebody already thought of.
    //
    // So: ANY identifier-ish text, possibly template/namespace/pointer/reference decorated, immediately
    // followed by the reserved name and then by something a declaration ends with. The first token is
    // deliberately loose — `x = near;` is caught too, and a false positive here costs one rename while a
    // miss costs a Windows CI job.
    const std::regex& DeclarationOfReservedName()
    {
        static const std::regex re(
             R"([A-Za-z_][A-Za-z0-9_:<>,& \t]*[ \t*&>][ \t]*(far|near)[ \t]*(=|;|\)|,|\[))" );
        return re;
    }
} // namespace

// Every authored source file, one regex. Fails with the file, the line and the name.
TEST( ReservedIdentifiers, NoSourceDeclaresAVariableWindowsWillEat )
{
    const fs::path root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "could not locate the repository root from " << fs::current_path();

    const auto files = SourceFiles( root );
    // Guards the guard: a wrong working directory would otherwise make this a green pass over zero files.
    ASSERT_GT( files.size(), 200u ) << "only " << files.size() << " source files found — the sweep is wrong";

    std::vector<std::string> offenders;
    for ( const fs::path& file : files )
    {
        std::ifstream in( file );
        std::string   line;
        int           number = 0;
        while ( std::getline( in, line ) )
        {
            ++number;
            const std::string trimmed = line.substr(
                 line.find_first_not_of( " \t" ) == std::string::npos ? 0 : line.find_first_not_of( " \t" ) );
            if ( trimmed.rfind( "//", 0 ) == 0 || trimmed.rfind( "*", 0 ) == 0 )
                continue;
            // Cheap reject first. std::regex over every line of every source file costs 70+ seconds; the
            // substring test throws away >99% of them and brings the whole suite under two. A guard nobody
            // wants to run is a guard that gets excluded from the sweep.
            if ( line.find( "far" ) == std::string::npos && line.find( "near" ) == std::string::npos )
                continue;

            // A TRAILING COMMENT IS PROSE AND MUST NOT BE SCANNED. `glm::vec4 GhostParams; // z = size
            // near, w = size far` is a lens-flare comment, and the loosened pattern above matched it — a
            // guard that reports prose teaches people to ignore it, and "rewriting words inside comments"
            // is already a filed defect here. Cut at `//` and match only what the compiler will see.
            const std::string code = line.substr( 0, line.find( "//" ) );
            if ( std::regex_search( code, DeclarationOfReservedName() ) )
                offenders.push_back( fs::relative( file, root ).generic_string() + ":" + std::to_string( number ) +
                                     "  " + trimmed );
        }
    }

    std::string report;
    for ( const std::string& o : offenders )
        report += "\n  " + o;

    EXPECT_TRUE( offenders.empty() )
         << offenders.size()
         << " declaration(s) use a name windef.h #defines away. MSVC will read the type with no variable "
            "after it and report a syntax error nowhere near the cause. Rename them (farKm, nearPlane, "
            "reach...):"
         << report;
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
