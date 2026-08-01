// Unit tests for the pure CPU 2D draw-list geometry builder (the batcher's front end). No GPU / Vulkan /
// ECS — just verifies that primitives emit the expected vertices, indices and state batches.

#include <Engine/Graphic/Render2D/DrawList2D.hpp>

#include <gtest/gtest.h>

using Desert::Graphic::Render2D::DrawList2D;

namespace
{
    constexpr float kEps = 1e-4f;
}

TEST( DrawList2D, StartsEmpty )
{
    DrawList2D dl;
    EXPECT_TRUE( dl.Empty() );
    EXPECT_TRUE( dl.GetVertices().empty() );
    EXPECT_TRUE( dl.GetIndices().empty() );
    EXPECT_TRUE( dl.GetCommands().empty() );
}

TEST( DrawList2D, AddRectFilledEmitsQuad )
{
    DrawList2D dl;
    dl.AddRectFilled( { 10.0f, 20.0f }, { 110.0f, 70.0f }, { 0.2f, 0.4f, 0.6f, 1.0f } );

    ASSERT_EQ( dl.GetVertices().size(), 4u );
    ASSERT_EQ( dl.GetIndices().size(), 6u );
    ASSERT_EQ( dl.GetCommands().size(), 1u );
    EXPECT_FALSE( dl.Empty() );

    const auto& v = dl.GetVertices();
    // Corner order: TL, TR, BR, BL.
    EXPECT_NEAR( v[0].Position.x, 10.0f, kEps );
    EXPECT_NEAR( v[0].Position.y, 20.0f, kEps );
    EXPECT_NEAR( v[2].Position.x, 110.0f, kEps );
    EXPECT_NEAR( v[2].Position.y, 70.0f, kEps );

    // UVs span the full 0..1 range so a sprite/atlas maps edge to edge.
    EXPECT_NEAR( v[0].UV.x, 0.0f, kEps );
    EXPECT_NEAR( v[0].UV.y, 0.0f, kEps );
    EXPECT_NEAR( v[2].UV.x, 1.0f, kEps );
    EXPECT_NEAR( v[2].UV.y, 1.0f, kEps );

    // Colour is carried per-vertex.
    for ( const auto& vert : v )
    {
        EXPECT_NEAR( vert.Color.r, 0.2f, kEps );
        EXPECT_NEAR( vert.Color.g, 0.4f, kEps );
        EXPECT_NEAR( vert.Color.b, 0.6f, kEps );
        EXPECT_NEAR( vert.Color.a, 1.0f, kEps );
    }

    // Two triangles referencing the four corners.
    const std::vector<uint32_t> expected = { 0, 1, 2, 2, 3, 0 };
    EXPECT_EQ( dl.GetIndices(), expected );

    const auto& cmd = dl.GetCommands()[0];
    EXPECT_EQ( cmd.Texture, nullptr ); // solid => white texture
    EXPECT_EQ( cmd.IndexOffset, 0u );
    EXPECT_EQ( cmd.IndexCount, 6u );
}

TEST( DrawList2D, ConsecutiveSolidRectsMergeIntoOneBatch )
{
    DrawList2D dl;
    dl.AddRectFilled( { 0.0f, 0.0f }, { 10.0f, 10.0f }, { 1, 1, 1, 1 } );
    dl.AddRectFilled( { 20.0f, 0.0f }, { 30.0f, 10.0f }, { 1, 0, 0, 1 } );

    EXPECT_EQ( dl.GetVertices().size(), 8u );
    EXPECT_EQ( dl.GetIndices().size(), 12u );
    // Same state (white texture) => a single draw command spanning both quads.
    ASSERT_EQ( dl.GetCommands().size(), 1u );
    EXPECT_EQ( dl.GetCommands()[0].IndexCount, 12u );

    // Second quad's indices are offset by its base vertex (4).
    const auto& idx = dl.GetIndices();
    EXPECT_EQ( idx[6], 4u );
    EXPECT_EQ( idx[8], 6u );
}

TEST( DrawList2D, ResetClearsGeometryKeepsUsable )
{
    DrawList2D dl;
    dl.AddRectFilled( { 0, 0 }, { 1, 1 }, { 1, 1, 1, 1 } );
    dl.Reset();

    EXPECT_TRUE( dl.Empty() );
    EXPECT_TRUE( dl.GetCommands().empty() );

    dl.AddRectFilled( { 0, 0 }, { 2, 2 }, { 1, 1, 1, 1 } );
    EXPECT_EQ( dl.GetVertices().size(), 4u );
    EXPECT_EQ( dl.GetCommands().size(), 1u );
    EXPECT_EQ( dl.GetIndices()[0], 0u ); // indices re-based after reset
}

TEST( DrawList2D, AddImageCarriesUVsAndTexture )
{
    DrawList2D  dl;
    int         tex = 0; // any non-null opaque id
    const void* id  = &tex;
    dl.AddImage( id, { 0.0f, 0.0f }, { 100.0f, 100.0f }, { 0.25f, 0.5f }, { 0.75f, 1.0f }, { 1, 1, 1, 1 } );

    ASSERT_EQ( dl.GetVertices().size(), 4u );
    ASSERT_EQ( dl.GetCommands().size(), 1u );
    EXPECT_EQ( dl.GetCommands()[0].Texture, id );

    const auto& v = dl.GetVertices();
    EXPECT_NEAR( v[0].UV.x, 0.25f, kEps ); // TL = uv0
    EXPECT_NEAR( v[0].UV.y, 0.5f, kEps );
    EXPECT_NEAR( v[2].UV.x, 0.75f, kEps ); // BR = uv1
    EXPECT_NEAR( v[2].UV.y, 1.0f, kEps );
    EXPECT_NEAR( v[1].UV.x, 0.75f, kEps ); // TR = (uv1.x, uv0.y)
    EXPECT_NEAR( v[1].UV.y, 0.5f, kEps );
}

TEST( DrawList2D, DifferentTexturesSplitBatches )
{
    DrawList2D dl;
    int        a = 0, b = 0;

    dl.AddRectFilled( { 0, 0 }, { 1, 1 }, { 1, 1, 1, 1 } ); // solid (null texture)
    dl.AddImage( &a, { 0, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 1, 1, 1, 1 } );
    dl.AddImage( &b, { 0, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 1, 1, 1, 1 } );

    // Three distinct textures (null, &a, &b) => three commands, each 6 indices at increasing offsets.
    ASSERT_EQ( dl.GetCommands().size(), 3u );
    EXPECT_EQ( dl.GetCommands()[0].Texture, nullptr );
    EXPECT_EQ( dl.GetCommands()[1].Texture, &a );
    EXPECT_EQ( dl.GetCommands()[2].Texture, &b );
    EXPECT_EQ( dl.GetCommands()[0].IndexOffset, 0u );
    EXPECT_EQ( dl.GetCommands()[1].IndexOffset, 6u );
    EXPECT_EQ( dl.GetCommands()[2].IndexOffset, 12u );
}

TEST( DrawList2D, AddTextMarksBatchAndSplitsFromImage )
{
    DrawList2D dl;
    int        atlas = 0;

    // Same texture id, but image vs text are distinct GPU states (different pipeline) => two batches.
    dl.AddImage( &atlas, { 0, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 1, 1, 1, 1 } );
    dl.AddText( &atlas, { 0, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 1, 1, 1, 1 } );
    dl.AddText( &atlas, { 2, 0 }, { 3, 1 }, { 0, 0 }, { 1, 1 }, { 1, 1, 1, 1 } );

    ASSERT_EQ( dl.GetCommands().size(), 2u );
    EXPECT_FALSE( dl.GetCommands()[0].Text );
    EXPECT_TRUE( dl.GetCommands()[1].Text );
    EXPECT_EQ( dl.GetCommands()[1].Texture, &atlas );
    // The two glyph quads share one text batch.
    EXPECT_EQ( dl.GetCommands()[1].IndexCount, 12u );
}

TEST( DrawList2D, MultiColorRectGradesTopToBottom )
{
    DrawList2D dl;
    dl.AddRectFilledMultiColor( { 0, 0 }, { 10, 10 }, { 1, 0, 0, 1 }, { 0, 0, 1, 1 } );

    ASSERT_EQ( dl.GetVertices().size(), 4u );
    const auto& v = dl.GetVertices();
    EXPECT_NEAR( v[0].Color.r, 1.0f, kEps ); // TL top colour
    EXPECT_NEAR( v[1].Color.r, 1.0f, kEps ); // TR top colour
    EXPECT_NEAR( v[2].Color.b, 1.0f, kEps ); // BR bottom colour
    EXPECT_NEAR( v[3].Color.b, 1.0f, kEps ); // BL bottom colour
}

TEST( DrawList2D, RectOutlineEmitsFourBars )
{
    DrawList2D dl;
    dl.AddRect( { 0, 0 }, { 100, 50 }, { 1, 1, 1, 1 }, 2.0f );

    // Four filled bars, all solid (white) => one merged batch of 4 quads.
    EXPECT_EQ( dl.GetVertices().size(), 16u );
    EXPECT_EQ( dl.GetIndices().size(), 24u );
    ASSERT_EQ( dl.GetCommands().size(), 1u );
    EXPECT_EQ( dl.GetCommands()[0].IndexCount, 24u );
}

TEST( DrawList2D, ClipRectSplitsBatchAndRestores )
{
    DrawList2D dl;
    dl.AddRectFilled( { 0, 0 }, { 10, 10 }, { 1, 1, 1, 1 } ); // unclipped -> cmd 0
    dl.PushClipRect( { 0, 0 }, { 5, 5 } );
    dl.AddRectFilled( { 0, 0 }, { 10, 10 }, { 1, 1, 1, 1 } ); // clipped -> cmd 1
    dl.PopClipRect();
    dl.AddRectFilled( { 0, 0 }, { 10, 10 }, { 1, 1, 1, 1 } ); // unclipped again -> cmd 2

    ASSERT_EQ( dl.GetCommands().size(), 3u );
    EXPECT_LE( dl.GetCommands()[0].ClipRect.z, 0.0f ); // no clip
    EXPECT_NEAR( dl.GetCommands()[1].ClipRect.z, 5.0f, kEps );
    EXPECT_NEAR( dl.GetCommands()[1].ClipRect.w, 5.0f, kEps );
    EXPECT_LE( dl.GetCommands()[2].ClipRect.z, 0.0f );
}

TEST( DrawList2D, NestedClipIntersects )
{
    DrawList2D dl;
    dl.PushClipRect( { 0, 0 }, { 100, 100 } );
    dl.PushClipRect( { 50, 50 }, { 200, 200 } ); // intersect -> (50,50)-(100,100)
    dl.AddRectFilled( { 0, 0 }, { 10, 10 }, { 1, 1, 1, 1 } );
    dl.PopClipRect();
    dl.PopClipRect();

    ASSERT_EQ( dl.GetCommands().size(), 1u );
    const auto& clip = dl.GetCommands()[0].ClipRect;
    EXPECT_NEAR( clip.x, 50.0f, kEps );
    EXPECT_NEAR( clip.y, 50.0f, kEps );
    EXPECT_NEAR( clip.z, 50.0f, kEps ); // width 100-50
    EXPECT_NEAR( clip.w, 50.0f, kEps );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
