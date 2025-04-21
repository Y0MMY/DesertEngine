#include <gtest/gtest.h>

#include <Common/Core/Result.hpp>

TEST( Result, err )
{
    struct dummy
    {
    };
    Common::Result<dummy> res = Common::MakeError<dummy>( "Test Message" );
    EXPECT_EQ( res.IsSuccess(), false );
    EXPECT_EQ( res.GetError(), "Test Message" );

    Common::Result<dummy> res2 = Common::MakeFormattedError<dummy>( "Test Formated {}", "Message" );
    EXPECT_EQ( res2.IsSuccess(), false );
    EXPECT_EQ( res2.GetError(), "Test Formated Message" );
}

TEST( Result, succ )
{
    struct dummy
    {
        int a;
    };
    dummy                 dum{ 112233 };
    Common::Result<dummy> res = Common::MakeSuccess<dummy>( dum );
    EXPECT_EQ( res.IsSuccess(), true );
    EXPECT_EQ( res.GetValue().a, 112233);

}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}