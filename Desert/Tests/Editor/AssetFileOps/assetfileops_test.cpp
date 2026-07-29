#include <gtest/gtest.h>

#include <Editor/Core/AssetFileOps.hpp>

#include <set>
#include <string>

using Desert::Editor::AssetFileOps::UniqueName;

namespace
{
    auto InSet( const std::set<std::string>& taken )
    {
        return [&taken]( const std::string& n ) { return taken.count( n ) != 0; };
    }
} // namespace

TEST( AssetFileOps, UniqueNameKeepsFreeName )
{
    std::set<std::string> taken;
    EXPECT_EQ( UniqueName( "Texture", ".png", InSet( taken ) ), "Texture.png" );
}

TEST( AssetFileOps, UniqueNameAvoidsCollision )
{
    std::set<std::string> taken = { "Texture.png" };
    EXPECT_EQ( UniqueName( "Texture", ".png", InSet( taken ) ), "Texture 2.png" );

    taken.insert( "Texture 2.png" );
    taken.insert( "Texture 3.png" );
    EXPECT_EQ( UniqueName( "Texture", ".png", InSet( taken ) ), "Texture 4.png" );
}

TEST( AssetFileOps, UniqueNameHandlesNoExtension )
{
    std::set<std::string> taken = { "Folder" };
    EXPECT_EQ( UniqueName( "Folder", "", InSet( taken ) ), "Folder 2" );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
