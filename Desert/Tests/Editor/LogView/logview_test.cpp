#include <gtest/gtest.h>

#include <Editor/Core/LogView.hpp>

using namespace Desert::Editor;

TEST( LogView, LogMatchesIsCaseInsensitiveSubstring )
{
    EXPECT_TRUE( LogMatches( "[Project] Opened Foo", "project" ) );
    EXPECT_TRUE( LogMatches( "[Project] Opened Foo", "OPENED" ) );
    EXPECT_TRUE( LogMatches( "anything", "" ) ); // empty query matches all
    EXPECT_FALSE( LogMatches( "hello", "world" ) );
    EXPECT_FALSE( LogMatches( "hi", "longer than line" ) );
}

TEST( LogView, CollapseMergesConsecutiveDuplicates )
{
    std::vector<std::pair<std::string, int>> lines = {
        { "tick", 0 }, { "tick", 0 }, { "tick", 0 }, { "warn", 1 }, { "tick", 0 }
    };
    const auto runs = CollapseConsecutive( lines );

    ASSERT_EQ( runs.size(), 3u );
    EXPECT_EQ( runs[0].Text, "tick" );
    EXPECT_EQ( runs[0].Count, 3 );
    EXPECT_EQ( runs[1].Text, "warn" );
    EXPECT_EQ( runs[1].Count, 1 );
    // The trailing "tick" is a separate event, not merged with the earlier run.
    EXPECT_EQ( runs[2].Text, "tick" );
    EXPECT_EQ( runs[2].Count, 1 );
}

TEST( LogView, CollapseDoesNotMergeAcrossLevels )
{
    std::vector<std::pair<std::string, int>> lines = { { "msg", 0 }, { "msg", 2 } };
    const auto runs = CollapseConsecutive( lines );
    ASSERT_EQ( runs.size(), 2u );
    EXPECT_EQ( runs[0].Level, 0 );
    EXPECT_EQ( runs[1].Level, 2 );
}

TEST( LogView, CollapseEmptyIsEmpty )
{
    EXPECT_TRUE( CollapseConsecutive( {} ).empty() );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
