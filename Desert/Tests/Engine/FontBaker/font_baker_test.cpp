#include <Engine/Text/FontBaker.hpp>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
    // Roboto ships with the editor resources — a real Latin font to bake against.
    std::vector<uint8_t> LoadRoboto()
    {
        // The test binary runs from the workspace root (RunTests.sh), so the resource path is stable.
        const char* candidates[] = { "Editor/Resources/Fonts/Roboto-Regular.ttf",
                                     "../../../Editor/Resources/Fonts/Roboto-Regular.ttf" };
        for ( const char* c : candidates )
        {
            std::ifstream f( c, std::ios::binary );
            if ( !f )
                continue;
            return std::vector<uint8_t>( ( std::istreambuf_iterator<char>( f ) ),
                                         std::istreambuf_iterator<char>() );
        }
        return {};
    }
} // namespace

TEST( FontBaker, BakesRobotoSDFAtlas )
{
    const auto ttf = LoadRoboto();
    ASSERT_FALSE( ttf.empty() ) << "Roboto-Regular.ttf not found (run from workspace root)";

    const auto font = Desert::Text::BakeFontSDF( ttf.data(), ttf.size(), 48.0f );
    ASSERT_TRUE( font.Valid() );

    // Vertical metrics are sane (ascent up, descent down, positive line height).
    EXPECT_GT( font.Ascent, 0.0f );
    EXPECT_LT( font.Descent, 0.0f );
    EXPECT_GT( font.LineHeight(), 0.0f );

    // Printable ASCII was baked; 'A' has a real bitmap cell + a positive advance.
    ASSERT_TRUE( font.Glyphs.count( 'A' ) );
    const auto& A = font.Glyphs.at( 'A' );
    EXPECT_GT( A.Advance, 0.0f );
    EXPECT_GT( A.Width, 0.0f );
    EXPECT_GT( A.Height, 0.0f );
    EXPECT_LE( A.U1, 1.0f );
    EXPECT_LE( A.V1, 1.0f );
    EXPECT_LT( A.U0, A.U1 );

    // Space has an advance but no atlas cell.
    ASSERT_TRUE( font.Glyphs.count( ' ' ) );
    EXPECT_GT( font.Glyphs.at( ' ' ).Advance, 0.0f );
    EXPECT_FLOAT_EQ( font.Glyphs.at( ' ' ).Width, 0.0f );

    // The atlas actually contains ink: at least one texel reached the on-edge value.
    bool anyEdge = false;
    for ( uint8_t v : font.AtlasR8 )
        if ( v >= Desert::Text::kSdfOnEdgeValue )
        {
            anyEdge = true;
            break;
        }
    EXPECT_TRUE( anyEdge );
}

TEST( FontBaker, RejectsGarbage )
{
    const std::vector<uint8_t> junk( 128, 0xAB );
    EXPECT_FALSE( Desert::Text::BakeFontSDF( junk.data(), junk.size() ).Valid() );
    EXPECT_FALSE( Desert::Text::BakeFontSDF( nullptr, 0 ).Valid() );
}

TEST( FontBaker, SerializeRoundTrip )
{
    const auto ttf = LoadRoboto();
    ASSERT_FALSE( ttf.empty() );
    const auto font = Desert::Text::BakeFontSDF( ttf.data(), ttf.size(), 48.0f );
    ASSERT_TRUE( font.Valid() );

    const std::vector<uint8_t> blob = Desert::Text::SerializeBakedFont( font );
    ASSERT_FALSE( blob.empty() );

    Desert::Text::BakedFont restored;
    ASSERT_TRUE( Desert::Text::DeserializeBakedFont( blob.data(), blob.size(), restored ) );

    // Structural equality: metrics, atlas dimensions/pixels, and every glyph survive the round trip.
    EXPECT_EQ( restored.AtlasWidth, font.AtlasWidth );
    EXPECT_EQ( restored.AtlasHeight, font.AtlasHeight );
    EXPECT_FLOAT_EQ( restored.PixelHeight, font.PixelHeight );
    EXPECT_FLOAT_EQ( restored.Ascent, font.Ascent );
    EXPECT_FLOAT_EQ( restored.Descent, font.Descent );
    EXPECT_EQ( restored.AtlasR8, font.AtlasR8 );
    ASSERT_EQ( restored.Glyphs.size(), font.Glyphs.size() );
    ASSERT_TRUE( restored.Glyphs.count( 'A' ) );
    EXPECT_FLOAT_EQ( restored.Glyphs.at( 'A' ).Advance, font.Glyphs.at( 'A' ).Advance );
    EXPECT_FLOAT_EQ( restored.Glyphs.at( 'A' ).U1, font.Glyphs.at( 'A' ).U1 );
}

TEST( FontBaker, DeserializeRejectsCorruptBlob )
{
    Desert::Text::BakedFont out;
    // Empty, too-short, and wrong-magic inputs must all fail cleanly (never over-read).
    EXPECT_FALSE( Desert::Text::DeserializeBakedFont( nullptr, 0, out ) );
    const std::vector<uint8_t> junk( 32, 0xCD );
    EXPECT_FALSE( Desert::Text::DeserializeBakedFont( junk.data(), junk.size(), out ) );

    // A valid blob truncated to half its length is a miss, not a crash.
    const auto ttf = LoadRoboto();
    ASSERT_FALSE( ttf.empty() );
    const auto blob = Desert::Text::SerializeBakedFont( Desert::Text::BakeFontSDF( ttf.data(), ttf.size() ) );
    EXPECT_FALSE( Desert::Text::DeserializeBakedFont( blob.data(), blob.size() / 2, out ) );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
