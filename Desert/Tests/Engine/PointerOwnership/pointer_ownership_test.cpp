#include "pointer_ownership_scan.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace
{
    using Desert::Tests::PointerCensus::Form;
    using Desert::Tests::PointerCensus::Member;
} // namespace

// A dumping main, used to WRITE the register and kept so the next stage can widen it the same way.
// It is not a test and it is not reached by RUN_ALL_TESTS.
TEST( PointerOwnership, DISABLED_DumpEveryMember )
{
    const std::string root = Desert::Tests::PointerCensus::RepoRoot();
    ASSERT_FALSE( root.empty() );
    for ( const Member& m : Desert::Tests::PointerCensus::ScanMembers( root ) )
        std::printf( "%s|%d|%s|%s|%s|%s\n", Desert::Tests::PointerCensus::FormName( m.Kind ), m.Line,
                     m.File.c_str(), m.Class.c_str(), m.Name.c_str(), m.Decl.c_str() );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
