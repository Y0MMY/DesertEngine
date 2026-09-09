// Unit tests for the pure CPU 2D draw-list geometry builder (the batcher's front end). No GPU / Vulkan /
// ECS — just verifies that primitives emit the expected vertices, indices and state batches.

#include <Engine/Graphic/Render2D/DrawList2D.hpp>

#include <gtest/gtest.h>

#include <cmath>

using Desert::Graphic::Render2D::DrawList2D;
namespace R2D = Desert::Graphic::Render2D;

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

TEST( DrawList2D, RoundedRectFansFromCentre )
{
    DrawList2D dl;
    dl.AddRectFilled( { 0, 0 }, { 100, 100 }, { 1, 1, 1, 1 }, 12.0f );

    // Centre vertex + 4 corners * (segments+1) perimeter vertices (kSeg=6 -> 7 each).
    EXPECT_EQ( dl.GetVertices().size(), 1u + 4u * 7u );
    // One fan triangle per perimeter vertex.
    EXPECT_EQ( dl.GetIndices().size(), ( 4u * 7u ) * 3u );
    ASSERT_EQ( dl.GetCommands().size(), 1u );
    EXPECT_EQ( dl.GetCommands()[0].Texture, nullptr );
}

TEST( DrawList2D, ZeroRoundingStaysSharpQuad )
{
    DrawList2D dl;
    dl.AddRectFilled( { 0, 0 }, { 10, 10 }, { 1, 1, 1, 1 }, 0.0f );
    EXPECT_EQ( dl.GetVertices().size(), 4u ); // sharp path
    EXPECT_EQ( dl.GetIndices().size(), 6u );
}

// --- Glass (backdrop blur) ------------------------------------------------------------------------
// A glass rect carries its rect / radius / blur in push constants, so it must ALWAYS get its own draw
// command: merging it with a neighbour would silently draw that neighbour with this rect's parameters.

TEST( DrawList2D, GlassRectEmitsOwnCommandWithParams )
{
    DrawList2D dl;
    dl.AddGlassRect( { 10.0f, 20.0f }, { 110.0f, 70.0f }, { 0.1f, 0.2f, 0.3f, 0.4f }, 8.0f, 0.5f );

    ASSERT_EQ( dl.GetVertices().size(), 4u );
    ASSERT_EQ( dl.GetIndices().size(), 6u );
    ASSERT_EQ( dl.GetCommands().size(), 1u );

    const auto& cmd = dl.GetCommands()[0];
    EXPECT_TRUE( cmd.Glass );
    EXPECT_FALSE( cmd.Text );
    EXPECT_EQ( cmd.Texture, nullptr );
    EXPECT_EQ( cmd.IndexOffset, 0u );
    EXPECT_EQ( cmd.IndexCount, 6u );
    EXPECT_NEAR( cmd.GlassRect.x, 10.0f, kEps );
    EXPECT_NEAR( cmd.GlassRect.y, 20.0f, kEps );
    EXPECT_NEAR( cmd.GlassRect.z, 110.0f, kEps );
    EXPECT_NEAR( cmd.GlassRect.w, 70.0f, kEps );
    EXPECT_NEAR( cmd.GlassRound, 8.0f, kEps );
    EXPECT_NEAR( cmd.GlassLod, 0.5f, kEps );

    // The tint reaches the shader through the vertex colour.
    EXPECT_NEAR( dl.GetVertices()[0].Color.a, 0.4f, kEps );
}

TEST( DrawList2D, GlassNeverMergesWithNeighbours )
{
    DrawList2D dl;
    dl.AddRectFilled( { 0.0f, 0.0f }, { 10.0f, 10.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } );
    dl.AddGlassRect( { 0.0f, 0.0f }, { 10.0f, 10.0f }, { 1.0f, 1.0f, 1.0f, 0.2f } );
    dl.AddGlassRect( { 20.0f, 0.0f }, { 30.0f, 10.0f }, { 1.0f, 1.0f, 1.0f, 0.2f } );
    dl.AddRectFilled( { 40.0f, 0.0f }, { 50.0f, 10.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } );

    // solid | glass | glass | solid — four separate commands, each anchored at its own indices.
    ASSERT_EQ( dl.GetCommands().size(), 4u );
    EXPECT_FALSE( dl.GetCommands()[0].Glass );
    EXPECT_TRUE( dl.GetCommands()[1].Glass );
    EXPECT_TRUE( dl.GetCommands()[2].Glass );
    EXPECT_FALSE( dl.GetCommands()[3].Glass );
    for ( uint32_t i = 0; i < 4; ++i )
    {
        EXPECT_EQ( dl.GetCommands()[i].IndexOffset, i * 6u );
        EXPECT_EQ( dl.GetCommands()[i].IndexCount, 6u );
    }
}

TEST( DrawList2D, GlassClampsBlurAndIgnoresDegenerateRects )
{
    DrawList2D dl;
    dl.AddGlassRect( { 0.0f, 0.0f }, { 10.0f, 10.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, -4.0f, 3.0f );
    ASSERT_EQ( dl.GetCommands().size(), 1u );
    EXPECT_NEAR( dl.GetCommands()[0].GlassRound, 0.0f, kEps ); // negative radius is meaningless
    EXPECT_NEAR( dl.GetCommands()[0].GlassLod, 1.0f, kEps );   // blur is a 0..1 dial

    // An empty (or inverted) rect draws nothing at all — no stray command, no stray geometry.
    dl.AddGlassRect( { 50.0f, 50.0f }, { 50.0f, 80.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } );
    dl.AddGlassRect( { 90.0f, 50.0f }, { 10.0f, 80.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } );
    EXPECT_EQ( dl.GetCommands().size(), 1u );
    EXPECT_EQ( dl.GetVertices().size(), 4u );
}

TEST( DrawList2D, TriangleEmitsOneTri )
{
    DrawList2D dl;
    dl.AddTriangleFilled( { 0, 0 }, { 10, 0 }, { 5, 10 }, { 1, 1, 1, 1 } );
    EXPECT_EQ( dl.GetVertices().size(), 3u );
    EXPECT_EQ( dl.GetIndices().size(), 3u );
    ASSERT_EQ( dl.GetCommands().size(), 1u );
    EXPECT_EQ( dl.GetCommands()[0].Texture, nullptr );
}

// ==================================================================================================
// THE RENDER TRANSFORM (Ю8)
//
// The whole design decision of that task is asserted by the first test below and by
// `ATransformDoesNotBreakTheBatch`: a transform is applied to the POSITIONS as they are emitted, not
// carried as GPU state. Everything else follows from those two — no draw call is added, the batch key
// is untouched, and a canvas that transforms nothing emits the bytes it always emitted.
// ==================================================================================================

// The invariant the task's byte-identity rests on, asserted at the only place it can be: with nothing
// pushed, a position is STORED, not transformed. EXPECT_EQ on floats is deliberate — "near" would pass
// for a value that had been through a matrix, which is exactly what must not have happened.
TEST( DrawList2D, WithNoTransformPushedPositionsAreStoredVerbatim )
{
    DrawList2D dl;
    EXPECT_FALSE( dl.HasTransform() );

    // Values chosen so a round trip through any multiply would show: none is exactly representable as
    // a short binary fraction.
    dl.AddRectFilled( { 0.1f, 0.3f }, { 100.7f, 70.9f }, { 1, 1, 1, 1 } );
    const auto& v = dl.GetVertices();
    ASSERT_EQ( v.size(), 4u );
    EXPECT_EQ( v[0].Position.x, 0.1f );
    EXPECT_EQ( v[0].Position.y, 0.3f );
    EXPECT_EQ( v[2].Position.x, 100.7f );
    EXPECT_EQ( v[2].Position.y, 70.9f );
}

// A 90-degree turn is the one rotation whose result can be written down exactly, so it is the one that
// can be asserted rather than approximated. Positive = clockwise in this y-down space.
TEST( DrawList2D, AQuarterTurnClockwiseMovesTheCornersWhereItShould )
{
    DrawList2D dl;
    // A 100x40 rect at the origin, turned about its own centre (50,20).
    dl.PushTransform( R2D::MakeTransform2D( { 50.0f, 20.0f }, 90.0f, { 1.0f, 1.0f } ) );
    EXPECT_TRUE( dl.HasTransform() );
    dl.AddRectFilled( { 0.0f, 0.0f }, { 100.0f, 40.0f }, { 1, 1, 1, 1 } );
    dl.PopTransform();
    EXPECT_FALSE( dl.HasTransform() );

    const auto& v = dl.GetVertices();
    ASSERT_EQ( v.size(), 4u );
    // Clockwise by 90 about (50,20): the top-left corner (0,0) goes to (70,-30).
    EXPECT_NEAR( v[0].Position.x, 70.0f, 1e-3f );
    EXPECT_NEAR( v[0].Position.y, -30.0f, 1e-3f );
    // ...and the bottom-right corner (100,40) to (30,70).
    EXPECT_NEAR( v[2].Position.x, 30.0f, 1e-3f );
    EXPECT_NEAR( v[2].Position.y, 70.0f, 1e-3f );

    // Popped: the next primitive is stored verbatim again.
    dl.AddRectFilled( { 5.0f, 6.0f }, { 7.0f, 8.0f }, { 1, 1, 1, 1 } );
    EXPECT_EQ( dl.GetVertices()[4].Position.x, 5.0f );
    EXPECT_EQ( dl.GetVertices()[4].Position.y, 6.0f );
}

// THE MEASUREMENT BEHIND THE DESIGN DECISION. Batches break on texture, text mode and clip
// (CurrentCommand), and a transform is none of those. Two rects that would have shared a command still
// share it when one of them is turned — which is what a per-batch matrix could not have given, because
// a push constant is per draw.
TEST( DrawList2D, ATransformDoesNotBreakTheBatch )
{
    DrawList2D dl;
    dl.AddRectFilled( { 0, 0 }, { 10, 10 }, { 1, 1, 1, 1 } );
    dl.PushTransform( R2D::MakeTransform2D( { 5.0f, 5.0f }, 30.0f, { 2.0f, 0.5f } ) );
    dl.AddRectFilled( { 20, 0 }, { 30, 10 }, { 1, 1, 1, 1 } );
    dl.PopTransform();
    dl.AddRectFilled( { 40, 0 }, { 50, 10 }, { 1, 1, 1, 1 } );

    ASSERT_EQ( dl.GetCommands().size(), 1u ) << "a transform opened a draw call of its own";
    EXPECT_EQ( dl.GetCommands()[0].IndexCount, 18u );
}

// Nesting is what makes a parent carry its children, and the ORDER is the load-bearing half: the outer
// matrix must be applied AFTER the inner one. Asserted with two transforms that do not commute — a
// translation and a scale.
TEST( DrawList2D, NestedTransformsComposeOuterAfterInner )
{
    // Outer: scale x2 about the origin. Inner: scale x1 about a pivot 100 to the right, i.e. a pure
    // rotation by 180 there, which lands the origin at 200.
    const glm::mat3 outer = R2D::MakeTransform2D( { 0.0f, 0.0f }, 0.0f, { 2.0f, 2.0f } );
    const glm::mat3 inner = R2D::MakeTransform2D( { 100.0f, 0.0f }, 180.0f, { 1.0f, 1.0f } );

    DrawList2D dl;
    dl.PushTransform( outer );
    dl.PushTransform( inner );
    dl.AddRectFilled( { 0.0f, 0.0f }, { 10.0f, 10.0f }, { 1, 1, 1, 1 } );
    dl.PopTransform();
    dl.PopTransform();

    // inner: (0,0) -> (200,0). outer: (200,0) -> (400,0). Composed the other way round the outer scale
    // would run first and the answer would be 200 — which is why this pair was chosen: they do not
    // commute, so the assertion is about the ORDER and not merely about both being applied.
    EXPECT_NEAR( dl.GetVertices()[0].Position.x, 400.0f, 1e-2f );
    EXPECT_NEAR( dl.GetVertices()[0].Position.y, 0.0f, 1e-2f );

    // One pop is one level: after the inner pop the outer must still be in force.
    dl.PushTransform( outer );
    dl.PushTransform( inner );
    dl.PopTransform();
    dl.AddRectFilled( { 0.0f, 0.0f }, { 10.0f, 10.0f }, { 1, 1, 1, 1 } );
    EXPECT_NEAR( dl.GetVertices()[4].Position.x, 0.0f, 1e-2f );
    dl.PopTransform();
    EXPECT_FALSE( dl.HasTransform() );
}

// The clip is a scissor, and hardware scissors are axis-aligned — so a rotated clipper is stored as the
// box AROUND it, and stored in SCREEN space rather than in the space it was written in.
TEST( DrawList2D, AClipUnderATransformIsStoredAsItsScreenBoundingBox )
{
    DrawList2D dl;
    dl.PushTransform( R2D::MakeTransform2D( { 50.0f, 50.0f }, 45.0f, { 1.0f, 1.0f } ) );
    dl.PushClipRect( { 0.0f, 0.0f }, { 100.0f, 100.0f } );
    dl.AddRectFilled( { 0, 0 }, { 100, 100 }, { 1, 1, 1, 1 } );
    dl.PopClipRect();
    dl.PopTransform();

    ASSERT_EQ( dl.GetCommands().size(), 1u );
    const glm::vec4 clip = dl.GetCommands()[0].ClipRect;
    // A 100x100 square turned 45 degrees about its centre spans 100*sqrt(2) each way, still centred
    // on (50,50).
    const float diag = 100.0f * std::sqrt( 2.0f );
    EXPECT_NEAR( clip.z, diag, 1e-2f );
    EXPECT_NEAR( clip.w, diag, 1e-2f );
    EXPECT_NEAR( clip.x, 50.0f - diag * 0.5f, 1e-2f );
    EXPECT_NEAR( clip.y, 50.0f - diag * 0.5f, 1e-2f );

    // Untransformed, the same call stores the rect itself — the neutral case is not merely close to the
    // old behaviour, it IS it.
    DrawList2D plain;
    plain.PushClipRect( { 3.5f, 4.5f }, { 13.5f, 24.5f } );
    plain.AddRectFilled( { 0, 0 }, { 1, 1 }, { 1, 1, 1, 1 } );
    ASSERT_EQ( plain.GetCommands().size(), 1u );
    EXPECT_EQ( plain.GetCommands()[0].ClipRect.x, 3.5f );
    EXPECT_EQ( plain.GetCommands()[0].ClipRect.y, 4.5f );
    EXPECT_EQ( plain.GetCommands()[0].ClipRect.z, 10.0f );
    EXPECT_EQ( plain.GetCommands()[0].ClipRect.w, 20.0f );
}

// Glass is the ONE primitive a moved vertex cannot transform, because its mask is an SDF the fragment
// shader evaluates over screen positions. So the command carries the way BACK from the screen, and its
// rect stays in its own space. Untransformed that inverse is the identity and the feather is one.
TEST( DrawList2D, GlassCarriesTheWayBackFromTheScreen )
{
    DrawList2D plain;
    plain.AddGlassRect( { 10.0f, 20.0f }, { 110.0f, 70.0f }, { 1, 1, 1, 0.5f }, 8.0f, 1.0f );
    ASSERT_EQ( plain.GetCommands().size(), 1u );
    EXPECT_TRUE( R2D::IsIdentity2D( plain.GetCommands()[0].GlassInverse ) );
    EXPECT_EQ( plain.GetCommands()[0].GlassFeather, 1.0f );
    EXPECT_EQ( plain.GetCommands()[0].GlassRect.x, 10.0f );

    DrawList2D dl;
    // Scaled by 4 in both axes: one screen pixel is a quarter of a pixel in the rect's own space.
    dl.PushTransform( R2D::MakeTransform2D( { 0.0f, 0.0f }, 0.0f, { 4.0f, 4.0f } ) );
    dl.AddGlassRect( { 10.0f, 20.0f }, { 110.0f, 70.0f }, { 1, 1, 1, 0.5f }, 8.0f, 1.0f );
    dl.PopTransform();

    ASSERT_EQ( dl.GetCommands().size(), 1u );
    const auto& cmd = dl.GetCommands()[0];
    EXPECT_NEAR( cmd.GlassFeather, 0.25f, 1e-4f );
    // The rect is unchanged (own space) while the vertices moved (screen space) — that pair IS the fix.
    EXPECT_EQ( cmd.GlassRect.x, 10.0f );
    EXPECT_NEAR( dl.GetVertices()[0].Position.x, 40.0f, 1e-3f );
    // And the inverse really is the inverse: a screen point maps back onto the vertex it came from.
    const glm::vec2 back = R2D::TransformPoint2D( cmd.GlassInverse, { 40.0f, 80.0f } );
    EXPECT_NEAR( back.x, 10.0f, 1e-3f );
    EXPECT_NEAR( back.y, 20.0f, 1e-3f );
}

// Reset is per frame, so a transform left pushed by a walk that returned early must not reach the next
// frame's first vertex.
TEST( DrawList2D, ResetDropsAPushedTransform )
{
    DrawList2D dl;
    dl.PushTransform( R2D::MakeTransform2D( { 0.0f, 0.0f }, 0.0f, { 3.0f, 3.0f } ) );
    dl.Reset();
    EXPECT_FALSE( dl.HasTransform() );
    dl.AddRectFilled( { 7.0f, 9.0f }, { 8.0f, 10.0f }, { 1, 1, 1, 1 } );
    EXPECT_EQ( dl.GetVertices()[0].Position.x, 7.0f );
}

// A scale of zero on an axis is authorable, and the inverse of that transform does not exist. What must
// not happen is an infinity travelling into a pointer position.
TEST( DrawList2D, ADegenerateTransformInvertsToSomethingFinite )
{
    const glm::mat3 flat = R2D::MakeTransform2D( { 40.0f, 60.0f }, 0.0f, { 0.0f, 1.0f } );
    const glm::vec2 p    = R2D::TransformPoint2D( R2D::InverseTransform2D( flat ), { 1234.0f, 5678.0f } );
    EXPECT_TRUE( std::isfinite( p.x ) ) << "an infinity from a singular inverse reached a pointer position";
    EXPECT_TRUE( std::isfinite( p.y ) ) << "an infinity from a singular inverse reached a pointer position";
    EXPECT_EQ( R2D::MeanScale2D( flat ), 0.0f );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
