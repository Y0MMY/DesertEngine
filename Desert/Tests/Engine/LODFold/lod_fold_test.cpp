#include <Editor/Import/LODFold.hpp>

#include <gtest/gtest.h>

using Desert::Editor::FoldExternalLODMeshes;
namespace Ser = Desert::Assets::Serialization;

namespace
{
    Ser::StaticVertexData V( float x )
    {
        Ser::StaticVertexData v{};
        v.Position = { x, 0.0f, 0.0f };
        return v;
    }

    // A submesh whose vertices/indices are appended to `data`, indices submesh-local (0-based).
    void AddSubmesh( Ser::MeshAssetData& data, const std::string& name, uint32_t vertCount,
                     const std::vector<Ser::IndexData>& localTris )
    {
        Ser::SubmeshData s;
        s.Name         = name;
        s.VertexOffset = static_cast<uint32_t>( data.StaticVertices.size() );
        s.VertexCount  = vertCount;
        s.IndexOffset  = static_cast<uint32_t>( data.Indices.size() * 3 );
        s.IndexCount   = static_cast<uint32_t>( localTris.size() * 3 );
        for ( uint32_t i = 0; i < vertCount; ++i )
            data.StaticVertices.push_back( V( static_cast<float>( i ) ) );
        for ( const auto& t : localTris )
            data.Indices.push_back( t );
        data.Submeshes.push_back( std::move( s ) );
    }
} // namespace

TEST( LODFold, FoldsLodSiblingIntoBase )
{
    Ser::MeshAssetData data;
    // Base "Rock": 4 verts, 2 tris. LOD1 "Rock_LOD1": 3 verts, 1 tri.
    AddSubmesh( data, "Rock", 4, { { 0, 1, 2 }, { 0, 2, 3 } } );
    AddSubmesh( data, "Rock_LOD1", 3, { { 0, 1, 2 } } );

    const int folded = FoldExternalLODMeshes( data );

    EXPECT_EQ( folded, 1 );
    ASSERT_EQ( data.Submeshes.size(), 1u ); // sibling removed
    const auto& s = data.Submeshes[0];
    EXPECT_EQ( s.Name, "Rock" );    // suffix stripped
    EXPECT_EQ( s.VertexCount, 7u ); // 4 base + 3 LOD verts, contiguous
    EXPECT_EQ( s.IndexCount, 6u );  // base LOD0 unchanged (2 tris)
    EXPECT_EQ( data.StaticVertices.size(), 7u );

    ASSERT_EQ( s.LODs.size(), 1u );
    ASSERT_EQ( s.LODs[0].size(), 1u ); // one triangle
    // The LOD triangle's indices are offset by the base vertex count (4) so they hit the appended LOD verts,
    // still inside the submesh's vertex range (drawn with baseVertex = VertexOffset).
    EXPECT_EQ( s.LODs[0][0].V1, 4u );
    EXPECT_EQ( s.LODs[0][0].V2, 5u );
    EXPECT_EQ( s.LODs[0][0].V3, 6u );
}

TEST( LODFold, LeavesPlainMeshesUntouched )
{
    Ser::MeshAssetData data;
    AddSubmesh( data, "Wall", 4, { { 0, 1, 2 }, { 0, 2, 3 } } );
    AddSubmesh( data, "Floor", 4, { { 0, 1, 2 }, { 0, 2, 3 } } );

    EXPECT_EQ( FoldExternalLODMeshes( data ), 0 );
    EXPECT_EQ( data.Submeshes.size(), 2u );
    EXPECT_TRUE( data.Submeshes[0].LODs.empty() );
}

TEST( LODFold, SkinnedIsIgnored )
{
    Ser::MeshAssetData data;
    data.IsSkinned = true;
    AddSubmesh( data, "Char", 4, { { 0, 1, 2 } } );
    AddSubmesh( data, "Char_LOD1", 3, { { 0, 1, 2 } } );
    EXPECT_EQ( FoldExternalLODMeshes( data ), 0 );
    EXPECT_EQ( data.Submeshes.size(), 2u ); // untouched
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
