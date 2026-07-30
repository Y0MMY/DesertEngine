// Unit tests for the Godot-Control-style UI layout solver (anchors + offsets + min-size, and the letterboxed
// canvas fit). Pure math — no GPU / ECS.

#include <Engine/UI/UILayout.hpp>

#include <gtest/gtest.h>

using Desert::UI::CanvasRect;
using Desert::UI::Rect;
using Desert::UI::ResolveRect;

namespace
{
    ::testing::AssertionResult RectNear( const Rect& a, const Rect& b, float eps = 1e-3f )
    {
        if ( std::abs( a.X - b.X ) > eps || std::abs( a.Y - b.Y ) > eps || std::abs( a.W - b.W ) > eps ||
             std::abs( a.H - b.H ) > eps )
            return ::testing::AssertionFailure()
                   << "(" << a.X << "," << a.Y << "," << a.W << "," << a.H << ") vs (" << b.X << "," << b.Y << ","
                   << b.W << "," << b.H << ")";
        return ::testing::AssertionSuccess();
    }

    const Rect kParent{ 0.0f, 0.0f, 1280.0f, 720.0f };
} // namespace

TEST( UILayout, FullStretchMatchesParent )
{
    const Rect r = ResolveRect( { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, kParent );
    EXPECT_TRUE( RectNear( r, kParent ) );
}

TEST( UILayout, StretchWithMargins )
{
    // Full anchors, 16 px inset on every side.
    const Rect r = ResolveRect( { 0, 0 }, { 1, 1 }, { 16, 16 }, { -16, -16 }, { 0, 0 }, kParent );
    EXPECT_TRUE( RectNear( r, { 16, 16, 1248, 688 } ) );
}

TEST( UILayout, FixedTopLeft )
{
    // Zero anchors (top-left corner), positioned + sized purely by offsets.
    const Rect r = ResolveRect( { 0, 0 }, { 0, 0 }, { 10, 20 }, { 110, 70 }, { 0, 0 }, kParent );
    EXPECT_TRUE( RectNear( r, { 10, 20, 100, 50 } ) );
}

TEST( UILayout, Centered )
{
    // Anchored to the parent centre, a 200x80 box centred on it.
    const Rect r = ResolveRect( { 0.5f, 0.5f }, { 0.5f, 0.5f }, { -100, -40 }, { 100, 40 }, { 0, 0 }, kParent );
    EXPECT_TRUE( RectNear( r, { 640 - 100, 360 - 40, 200, 80 } ) );
}

TEST( UILayout, CustomMinimumSizeClamps )
{
    // A 10x10 box but with a 120x40 minimum.
    const Rect r = ResolveRect( { 0, 0 }, { 0, 0 }, { 0, 0 }, { 10, 10 }, { 120, 40 }, kParent );
    EXPECT_TRUE( RectNear( r, { 0, 0, 120, 40 } ) );
}

TEST( UILayout, CanvasFitLetterboxesWide )
{
    // 1280x720 design into a 1920x720 (too wide) viewport -> full height, centred horizontally.
    const Rect r = CanvasRect( 1280.0f, 720.0f, 1920.0f, 720.0f );
    EXPECT_TRUE( RectNear( r, { ( 1920 - 1280 ) * 0.5f, 0.0f, 1280.0f, 720.0f } ) );
}

TEST( UILayout, CanvasFitScalesDown )
{
    // 1280x720 design into a 640x360 viewport -> scaled to exactly fit (0.5x), no letterbox.
    const Rect r = CanvasRect( 1280.0f, 720.0f, 640.0f, 360.0f );
    EXPECT_TRUE( RectNear( r, { 0.0f, 0.0f, 640.0f, 360.0f } ) );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
