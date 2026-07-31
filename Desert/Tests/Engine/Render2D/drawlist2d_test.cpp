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

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
