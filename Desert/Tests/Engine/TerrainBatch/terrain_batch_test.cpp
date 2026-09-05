// The terrain batching contract (Engine/Graphic/Systems/Scene/Terrain/TerrainBatch.hpp), tested as the
// RELATION it is: N terrains with different per-draw data must end up in N different rows / materials,
// and terrains that are the same must SHARE — both directions, because the defect this guards against
// ("first Apply of the frame wins, every later terrain silently draws with somebody else's resources")
// was invisible from either side alone. The census of what is per-draw versus per-material lives in the
// ShaderCacheKey suite, which reflects the real Terrain.shader SPIR-V; this suite drives the pure C++
// half that decides which terrain gets which material and which row.

#include <gtest/gtest.h>

#include <Engine/Graphic/Systems/Scene/Terrain/TerrainBatch.hpp>

using Desert::Graphic::MaterialOverrides;
using Desert::Graphic::System::TerrainInstance;
using Desert::Graphic::System::TerrainTextureKey;

namespace
{
    MaterialOverrides WithTextures( std::vector<std::pair<std::string, uint64_t>> textures )
    {
        MaterialOverrides overrides;
        overrides.Textures = std::move( textures );
        return overrides;
    }
} // namespace

// ---- The key separates what must not share ---------------------------------------------------------

TEST( TerrainTextureKey, DifferentTexturesGetDifferentMaterials )
{
    const auto checker = TerrainTextureKey( WithTextures( { { "u_RockTex", 42 } } ), nullptr );
    const auto plain   = TerrainTextureKey( WithTextures( {} ), nullptr );
    const auto other   = TerrainTextureKey( WithTextures( { { "u_RockTex", 43 } } ), nullptr );

    EXPECT_NE( checker, plain ) << "a terrain with a texture override shared the textureless material";
    EXPECT_NE( checker, other ) << "two different textures in one sampler collapsed into one material";
}

TEST( TerrainTextureKey, TheSamplerNameIsPartOfTheIdentity )
{
    // The same handle in a different slot is a different picture on screen: grass everywhere versus
    // rock everywhere. A key of handles alone would batch them together.
    const auto rock  = TerrainTextureKey( WithTextures( { { "u_RockTex", 42 } } ), nullptr );
    const auto grass = TerrainTextureKey( WithTextures( { { "u_GrassTex", 42 } } ), nullptr );
    EXPECT_NE( rock, grass );
}

TEST( TerrainTextureKey, TheSplatMapIsPartOfTheIdentity )
{
    // The splat map is runtime-owned (painted, no asset handle), so it enters by address — two terrains
    // painted separately must not share the descriptors that hold the paint.
    int        a = 0, b = 0;
    const auto withA     = TerrainTextureKey( WithTextures( {} ), &a );
    const auto withB     = TerrainTextureKey( WithTextures( {} ), &b );
    const auto unpainted = TerrainTextureKey( WithTextures( {} ), nullptr );

    EXPECT_NE( withA, withB );
    EXPECT_NE( withA, unpainted );
}

// ---- ...and only what must not share ---------------------------------------------------------------

TEST( TerrainTextureKey, TheSameTexturesInAnotherOrderShareOneMaterial )
{
    // Two terrains naming the same set in a different order are the same texture set. Failing this
    // direction is quieter than the other — it only allocates a redundant material — but it is the
    // exact drift MeshRenderer's GenericTextureKey sorts against, and the two keys follow one rule.
    const auto ab = TerrainTextureKey( WithTextures( { { "u_GrassTex", 7 }, { "u_RockTex", 9 } } ), nullptr );
    const auto ba = TerrainTextureKey( WithTextures( { { "u_RockTex", 9 }, { "u_GrassTex", 7 } } ), nullptr );
    EXPECT_EQ( ab, ba );
}

TEST( TerrainTextureKey, AnUnsetSlotIsNoSlot )
{
    // Handle 0 means "keep the fallback", which every terrain shares; it must not split the batch.
    const auto explicitZero = TerrainTextureKey( WithTextures( { { "u_RockTex", 0 } } ), nullptr );
    const auto absent       = TerrainTextureKey( WithTextures( {} ), nullptr );
    EXPECT_EQ( explicitZero, absent );
}

// ---- The instance row layout is the shader's -------------------------------------------------------

TEST( TerrainInstanceRow, TheRowIsSevenSixteenByteSlots )
{
    // The GLSL `TerrainInstance` in Terrain.shader is the second statement of this layout; the
    // static_asserts in TerrainBatch.hpp hold the offsets and this holds the stride the GPU indexes by.
    // The SPIR-V side of the same relation is asserted in the ShaderCacheKey suite.
    EXPECT_EQ( sizeof( TerrainInstance ), 112u );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
