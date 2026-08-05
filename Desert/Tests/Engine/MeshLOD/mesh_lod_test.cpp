#include <Engine/Geometry/LODSelection.hpp>
#include <Engine/Geometry/MeshLOD.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

using Desert::Index;
using Desert::Submesh;
using Desert::Vertex;
using Desert::Geometry::BuildLODIndexBuffer;
using Desert::Geometry::SelectLOD;
using Desert::Geometry::SimplifyLODLevels;

namespace
{
    // An N*N-quad grid on the XY plane -> 2*N*N triangles, submesh-local 0-based indices. Big enough (>8 tris)
    // that meshopt actually simplifies.
    struct Grid
    {
        std::vector<Vertex> Vertices;
        std::vector<Index>  Indices;
        Submesh             Sub;
    };

    Grid MakeGrid( uint32_t n )
    {
        Grid g;
        for ( uint32_t y = 0; y <= n; ++y )
            for ( uint32_t x = 0; x <= n; ++x )
            {
                Vertex v{};
                v.Position = { static_cast<float>( x ), static_cast<float>( y ), 0.0f };
                g.Vertices.push_back( v );
            }
        const uint32_t stride = n + 1;
        for ( uint32_t y = 0; y < n; ++y )
            for ( uint32_t x = 0; x < n; ++x )
            {
                const uint32_t a = y * stride + x, b = a + 1, c = a + stride, d = c + 1;
                g.Indices.push_back( { a, c, b } );
                g.Indices.push_back( { b, c, d } );
            }
        g.Sub.Name         = "grid";
        g.Sub.VertexOffset = 0;
        g.Sub.VertexCount  = static_cast<uint32_t>( g.Vertices.size() );
        g.Sub.IndexOffset  = 0;
        g.Sub.IndexCount   = static_cast<uint32_t>( g.Indices.size() * 3 );
        return g;
    }

    bool SameTris( const std::vector<Index>& a, const std::vector<Index>& b )
    {
        if ( a.size() != b.size() )
            return false;
        for ( size_t i = 0; i < a.size(); ++i )
            if ( a[i].V1 != b[i].V1 || a[i].V2 != b[i].V2 || a[i].V3 != b[i].V3 )
                return false;
        return true;
    }
} // namespace

// The whole point of baking: cook (SimplifyLODLevels) + load (BuildLODIndexBuffer assemble) must produce the
// EXACT same GPU index buffer + per-submesh LOD ranges as generating at load. Behaviour-preserving.
TEST( MeshLOD, BakedEqualsGenerated )
{
    const Grid g = MakeGrid( 6 ); // 72 triangles

    // Generate-at-load (BakedLODs empty).
    std::vector<Submesh> genSubs = { g.Sub };
    const auto           gpuGen  = BuildLODIndexBuffer( g.Vertices, g.Indices, genSubs );

    // Cook: bake the levels, then assemble from them (BakedLODs populated).
    std::vector<float> pos;
    for ( const auto& v : g.Vertices )
    {
        pos.push_back( v.Position.x );
        pos.push_back( v.Position.y );
        pos.push_back( v.Position.z );
    }
    const auto           baked     = SimplifyLODLevels( pos.data(), g.Sub.VertexCount, g.Indices );
    std::vector<Submesh> bakedSubs = { g.Sub };
    bakedSubs[0].BakedLODs         = baked;
    const auto gpuBaked            = BuildLODIndexBuffer( g.Vertices, g.Indices, bakedSubs );

    EXPECT_TRUE( SameTris( gpuGen, gpuBaked ) ) << "baked GPU index buffer differs from generated";

    ASSERT_EQ( genSubs[0].LODs.size(), bakedSubs[0].LODs.size() );
    ASSERT_EQ( genSubs[0].LODs.size(), 4u ); // LOD0 + 3 ratios
    for ( size_t i = 0; i < genSubs[0].LODs.size(); ++i )
    {
        EXPECT_EQ( genSubs[0].LODs[i].IndexOffset, bakedSubs[0].LODs[i].IndexOffset ) << "LOD " << i;
        EXPECT_EQ( genSubs[0].LODs[i].IndexCount, bakedSubs[0].LODs[i].IndexCount ) << "LOD " << i;
    }
    // LOD0 is byte-identical to the base range.
    EXPECT_EQ( genSubs[0].LODs[0].IndexOffset, g.Sub.IndexOffset );
    EXPECT_EQ( genSubs[0].LODs[0].IndexCount, g.Sub.IndexCount );
}

// Assembly arithmetic in isolation (no meshopt): hand-crafted baked levels -> correct offsets/counts, and an
// empty level reuses the previous range (monotonic).
TEST( MeshLOD, AssembleOffsetsAndReuse )
{
    // 10-triangle base submesh.
    std::vector<Index>  base( 10, Index{ 0, 1, 2 } );
    std::vector<Vertex> verts( 8 ); // >0, positions unused on the baked path
    Submesh             s;
    s.VertexOffset = 0;
    s.VertexCount  = 8;
    s.IndexOffset  = 0;
    s.IndexCount   = 30;
    s.BakedLODs    = {
         std::vector<Index>( 4, Index{ 0, 1, 2 } ), // L1: 4 tris
         std::vector<Index>( 2, Index{ 0, 1, 2 } ), // L2: 2 tris
         std::vector<Index>{},                      // L3: empty -> reuse L2
    };

    std::vector<Submesh> subs = { s };
    const auto           gpu  = BuildLODIndexBuffer( verts, base, subs );

    ASSERT_EQ( subs[0].LODs.size(), 4u );
    EXPECT_EQ( subs[0].LODs[0].IndexOffset, 0u );
    EXPECT_EQ( subs[0].LODs[0].IndexCount, 30u );
    EXPECT_EQ( subs[0].LODs[1].IndexOffset, 30u ); // after 10 base tris (30 indices)
    EXPECT_EQ( subs[0].LODs[1].IndexCount, 12u );  // 4 tris
    EXPECT_EQ( subs[0].LODs[2].IndexOffset, 42u ); // after 14 tris
    EXPECT_EQ( subs[0].LODs[2].IndexCount, 6u );   // 2 tris
    EXPECT_EQ( subs[0].LODs[3].IndexOffset, 42u ); // empty -> reused L2
    EXPECT_EQ( subs[0].LODs[3].IndexCount, 6u );
    EXPECT_EQ( gpu.size(), 16u ); // 10 base + 4 + 2
}

// ---------------------------------------------------------------------------------------------------
// LOD SELECTION (Geometry::SelectLOD) — the policy the renderer draws with AND the Details panel
// reports. Both read the same function, so these cases pin what "drawing LOD n" means.
// ---------------------------------------------------------------------------------------------------

namespace
{
    // One submesh with a symmetric AABB of the given half-size (unit-ish mesh centred on the origin).
    std::vector<Submesh> BoundedSubmesh( float halfSize )
    {
        Submesh s{};
        s.BoundingBox.Min = glm::vec3( -halfSize );
        s.BoundingBox.Max = glm::vec3( halfSize );
        return { s };
    }

    glm::mat4 AtOrigin( float scale = 1.0f )
    {
        return glm::scale( glm::mat4( 1.0f ), glm::vec3( scale ) );
    }
} // namespace

TEST( LODSelection, ForcedLevelWinsOverEverything )
{
    const auto subs = BoundedSubmesh( 50.0f );
    // Far away (would be the coarsest level) but pinned to LOD 1.
    EXPECT_EQ( SelectLOD( AtOrigin(), subs, glm::vec3( 0.0f, 0.0f, 100000.0f ), /*forced*/ 1, /*bias*/ 0 ), 1u );
    // The bias is ignored while a level is forced.
    EXPECT_EQ( SelectLOD( AtOrigin(), subs, glm::vec3( 0.0f, 0.0f, 100000.0f ), /*forced*/ 0, /*bias*/ 3 ), 0u );
}

TEST( LODSelection, CoarsensWithDistance )
{
    const auto subs = BoundedSubmesh( 50.0f ); // radius ~86.6 cm

    // (not named near/far: those are legacy macros in the Windows SDK headers)
    const uint32_t closeUp = SelectLOD( AtOrigin(), subs, glm::vec3( 0.0f, 0.0f, 100.0f ), -1, 0 );
    const uint32_t middle  = SelectLOD( AtOrigin(), subs, glm::vec3( 0.0f, 0.0f, 800.0f ), -1, 0 );
    const uint32_t distant = SelectLOD( AtOrigin(), subs, glm::vec3( 0.0f, 0.0f, 100000.0f ), -1, 0 );

    EXPECT_EQ( closeUp, 0u );
    EXPECT_GT( middle, closeUp );
    EXPECT_EQ( distant, 3u ); // clamped to the coarsest level the policy may pick
}

TEST( LODSelection, SizeAwareAndBiasShifts )
{
    const auto small = BoundedSubmesh( 50.0f );
    const auto big   = BoundedSubmesh( 5000.0f );
    const auto eye   = glm::vec3( 0.0f, 0.0f, 5000.0f );

    // A bigger object keeps finer detail at the same distance...
    EXPECT_LT( SelectLOD( AtOrigin(), big, eye, -1, 0 ), SelectLOD( AtOrigin(), small, eye, -1, 0 ) );
    // ...and so does the same object scaled up by its transform.
    EXPECT_LT( SelectLOD( AtOrigin( 100.0f ), small, eye, -1, 0 ), SelectLOD( AtOrigin(), small, eye, -1, 0 ) );

    // The bias shifts the automatic pick and stays inside [0, kMaxAutoLOD].
    const uint32_t base = SelectLOD( AtOrigin(), small, eye, -1, 0 );
    EXPECT_EQ( SelectLOD( AtOrigin(), small, eye, -1, 1 ), std::min( base + 1u, 3u ) );
    EXPECT_EQ( SelectLOD( AtOrigin(), small, eye, -1, -9 ), 0u );
    EXPECT_EQ( SelectLOD( AtOrigin(), small, eye, -1, 9 ), 3u );
}

TEST( LODSelection, EmptyMeshIsLODZero )
{
    EXPECT_EQ( SelectLOD( AtOrigin(), {}, glm::vec3( 0.0f, 0.0f, 100000.0f ), -1, 0 ), 0u );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
