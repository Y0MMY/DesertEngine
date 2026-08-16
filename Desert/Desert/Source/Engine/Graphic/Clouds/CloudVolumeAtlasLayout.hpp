#pragma once

#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeFormat.hpp>

#include <Common/Core/ResultStr.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Where N hero clouds live on the GPU, and the arithmetic that reads one of them without touching its
// neighbours (VOXEL_CLOUD_PATH.md §4.2, teamlead Q2 and Q4).
//
// WHY AN ATLAS AND NOT AN ARRAY OF SAMPLERS. `uniform sampler3D u_Volumes[N]` with a dynamic index
// needs `shaderSampledImageArrayDynamicIndexing`, which on MoltenVK lands on Metal argument buffers —
// supported, but a portability bet with nothing to win. One image with tiles laid out in a grid needs
// no feature bit at all, and the tile origin rides in the per-instance record the renderer already has
// to build.
//
// WHY THE ARITHMETIC IS HERE, IN C++, AND TESTED. Every 3D sampler this engine creates is LINEAR and
// REPEAT, and `VulkanImage.cpp` VERIFIES it — correct for the tiling noise that is 100% of today's
// volume usage, wrong for a bounded hero cloud. The teamlead's Q4 answer is to clamp in the shader
// rather than relax that policy, which makes the clamp OUR code rather than the sampler's, and code we
// wrote is code we can get wrong. So the mapping lives in one place, and the property that matters —
// a tap can never reach a neighbouring tile — is a test rather than a comment.
//
// THE TWO GUARANTEES, and they are deliberately redundant:
//
//   1. THE CLAMP. `CloudVolumeAtlasUvw` pins the sample point to the band [0.5, dim-0.5] texels inside
//      its own tile, so both taps of every trilinear pair have an index in [0, dim-1] of that tile.
//   2. THE GUARD SHELL. Every `.dvol` is baked with an empty outermost voxel shell (BakeCloudVolume
//      refuses to write one that is not). So even if a sampler's own rounding put a zero-weight tap one
//      texel past the band, that texel is the NEIGHBOUR's guard shell — which is empty too.
//
// One of these would do. Two of them mean the failure needs both to be wrong at once, and the failure
// in question is a stripe of one cloud appearing inside another, which nobody would diagnose quickly.

namespace Desert::Graphic
{
    // Q2, decided: eight tiles of 128x128x64 RGBA8, laid out 4 x 2 x 1 => a 512 x 256 x 64 atlas of
    // 32.00 MiB. The world extent a tile covers is NOT here — it is a per-instance transform, so a
    // closer fly-by re-bakes at a smaller extent without an atlas migration.
    //
    // A struct rather than loose constants so the tests can drive degenerate and asymmetric geometries
    // through exactly the code the engine runs, instead of a paraphrase of it.
    struct CloudVolumeAtlasLayout
    {
        uint32_t TileWidth  = 128;
        uint32_t TileHeight = 128;
        uint32_t TileDepth  = 64;

        uint32_t TilesX = 4;
        uint32_t TilesY = 2;
        uint32_t TilesZ = 1;
    };

    inline constexpr uint32_t CloudVolumeAtlasTileCount( const CloudVolumeAtlasLayout& layout )
    {
        return layout.TilesX * layout.TilesY * layout.TilesZ;
    }

    inline constexpr glm::uvec3 CloudVolumeAtlasDimensions( const CloudVolumeAtlasLayout& layout )
    {
        return glm::uvec3( layout.TileWidth * layout.TilesX, layout.TileHeight * layout.TilesY,
                           layout.TileDepth * layout.TilesZ );
    }

    inline uint64_t CloudVolumeAtlasBytes( const CloudVolumeAtlasLayout& layout )
    {
        const glm::uvec3 dims = CloudVolumeAtlasDimensions( layout );
        return Core::Formats::CalculateImageSize( dims.x, dims.y, dims.z, Core::Formats::ImageFormat::RGBA8F );
    }

    // Tile index -> the voxel coordinate of the tile's corner. X varies fastest, then Y, then Z — the
    // same order as the voxel payload itself, so a reader who knows one knows the other.
    inline glm::uvec3 CloudVolumeAtlasTileOrigin( const CloudVolumeAtlasLayout& layout, uint32_t tileIndex )
    {
        const uint32_t x = tileIndex % layout.TilesX;
        const uint32_t y = ( tileIndex / layout.TilesX ) % layout.TilesY;
        const uint32_t z = tileIndex / ( layout.TilesX * layout.TilesY );

        return glm::uvec3( x * layout.TileWidth, y * layout.TileHeight, z * layout.TileDepth );
    }

    // The exact inverse of CloudVolumeAtlasTileOrigin. Not decoration: the renderer packs an origin into
    // its instance record and the tile bookkeeping works in indices, so the two directions have to agree
    // or a released tile is not the tile that gets reused.
    inline uint32_t CloudVolumeAtlasTileIndex( const CloudVolumeAtlasLayout& layout, const glm::uvec3& origin )
    {
        const uint32_t x = origin.x / layout.TileWidth;
        const uint32_t y = origin.y / layout.TileHeight;
        const uint32_t z = origin.z / layout.TileDepth;

        return ( z * layout.TilesY + y ) * layout.TilesX + x;
    }

    // Local [0,1]^3 across the tile's own box -> the UVW to hand a `texture()` call on the atlas.
    //
    // The clamp is the whole point (see the header note). `local` arrives from a world->local transform
    // and is NOT assumed to be in range: a ray steps outside the instance box constantly, and the answer
    // there must be a legal in-tile texel rather than whatever the REPEAT sampler would have wrapped to.
    inline glm::vec3 CloudVolumeAtlasUvw( const CloudVolumeAtlasLayout& layout, uint32_t tileIndex,
                                          const glm::vec3& local )
    {
        const glm::uvec3 origin = CloudVolumeAtlasTileOrigin( layout, tileIndex );
        const glm::vec3  tile =
             glm::vec3( static_cast<float>( layout.TileWidth ), static_cast<float>( layout.TileHeight ),
                        static_cast<float>( layout.TileDepth ) );

        // [0,1] over the tile becomes [0, dim] in the tile's own texel space, then the half-texel band.
        glm::vec3 texel = glm::clamp( local, glm::vec3( 0.0f ), glm::vec3( 1.0f ) ) * tile;
        texel           = glm::clamp( texel, glm::vec3( 0.5f ), tile - glm::vec3( 0.5f ) );

        const glm::uvec3 atlas = CloudVolumeAtlasDimensions( layout );
        return ( glm::vec3( origin ) + texel ) / glm::vec3( static_cast<float>( atlas.x ),
                                                            static_cast<float>( atlas.y ),
                                                            static_cast<float>( atlas.z ) );
    }

    // The inverse of CloudVolumeAtlasUvw, exact for every `local` the clamp did not move — i.e. for
    // local in [0.5/dim, 1 - 0.5/dim] on each axis. Outside that band the forward map is deliberately
    // many-to-one, so no inverse exists and this returns the clamped representative.
    inline glm::vec3 CloudVolumeAtlasLocal( const CloudVolumeAtlasLayout& layout, uint32_t tileIndex,
                                            const glm::vec3& uvw )
    {
        const glm::uvec3 origin = CloudVolumeAtlasTileOrigin( layout, tileIndex );
        const glm::uvec3 atlas  = CloudVolumeAtlasDimensions( layout );

        const glm::vec3 atlasTexel = uvw * glm::vec3( static_cast<float>( atlas.x ), static_cast<float>( atlas.y ),
                                                      static_cast<float>( atlas.z ) );
        const glm::vec3 texel      = atlasTexel - glm::vec3( origin );

        return texel / glm::vec3( static_cast<float>( layout.TileWidth ), static_cast<float>( layout.TileHeight ),
                                  static_cast<float>( layout.TileDepth ) );
    }

    // The set of atlas voxels a trilinear fetch at `uvw` reads with NON-ZERO weight, as an inclusive
    // box. A tap whose interpolation weight is exactly zero is not a read — that distinction is what
    // lets the clamp band reach the last half-texel of a tile instead of stopping short of it.
    struct CloudVolumeAtlasFootprint
    {
        glm::ivec3 Min{ 0 };
        glm::ivec3 Max{ 0 };
    };

    inline CloudVolumeAtlasFootprint CloudVolumeAtlasTrilinearFootprint( const CloudVolumeAtlasLayout& layout,
                                                                         const glm::vec3&              uvw )
    {
        const glm::uvec3 atlas     = CloudVolumeAtlasDimensions( layout );
        const glm::vec3 atlasTexel = uvw * glm::vec3( static_cast<float>( atlas.x ), static_cast<float>( atlas.y ),
                                                      static_cast<float>( atlas.z ) );

        CloudVolumeAtlasFootprint footprint;
        for ( int axis = 0; axis < 3; ++axis )
        {
            const float shifted  = atlasTexel[axis] - 0.5f;
            const float lower    = std::floor( shifted );
            const float fraction = shifted - lower;

            footprint.Min[axis] = static_cast<int32_t>( lower );
            footprint.Max[axis] = footprint.Min[axis] + ( fraction > 0.0f ? 1 : 0 );
        }
        return footprint;
    }

    // ---- Filling a tile ---------------------------------------------------------------------------

    // Copies one baked volume into `atlas`, which must already be the full atlas payload. Row by row,
    // because the atlas is wider than a tile and a volume's rows are not contiguous inside it.
    inline Common::BoolResultStr CloudVolumeAtlasWriteTile( const CloudVolumeAtlasLayout& layout,
                                                            uint32_t tileIndex, const CloudVolume& volume,
                                                            std::vector<unsigned char>& atlas )
    {
        if ( tileIndex >= CloudVolumeAtlasTileCount( layout ) )
            return Common::MakeFormattedError<bool>( "Cloud volume atlas tile {} is outside the {} tiles the "
                                                     "atlas has",
                                                     tileIndex, CloudVolumeAtlasTileCount( layout ) );

        if ( volume.Header.Width != layout.TileWidth || volume.Header.Height != layout.TileHeight ||
             volume.Header.Depth != layout.TileDepth )
            return Common::MakeFormattedError<bool>(
                 "A {}x{}x{} .dvol does not fit the atlas's {}x{}x{} tile. The atlas geometry is fixed "
                 "(teamlead Q2); re-bake the volume at the tile size.",
                 volume.Header.Width, volume.Header.Height, volume.Header.Depth, layout.TileWidth,
                 layout.TileHeight, layout.TileDepth );

        const uint64_t expected = CloudVolumePayloadBytes( volume.Header );
        if ( volume.Voxels.size() != expected )
            return Common::MakeFormattedError<bool>( "The .dvol payload is {} bytes but its header describes {}",
                                                     volume.Voxels.size(), expected );

        const glm::uvec3 atlasDims  = CloudVolumeAtlasDimensions( layout );
        const uint64_t   atlasBytes = CloudVolumeAtlasBytes( layout );
        if ( atlas.size() != atlasBytes )
            return Common::MakeFormattedError<bool>(
                 "The atlas buffer is {} bytes but a {}x{}x{} RGBA8 atlas needs {}", atlas.size(), atlasDims.x,
                 atlasDims.y, atlasDims.z, atlasBytes );

        const glm::uvec3 origin   = CloudVolumeAtlasTileOrigin( layout, tileIndex );
        const glm::uvec3 dims     = CloudVolumeAtlasDimensions( layout );
        const size_t     rowBytes = static_cast<size_t>( layout.TileWidth ) * kCloudVolumeChannels;

        for ( uint32_t z = 0; z < layout.TileDepth; ++z )
        {
            for ( uint32_t y = 0; y < layout.TileHeight; ++y )
            {
                const size_t source = CloudVolumeVoxelIndex( volume.Header, 0, y, z );
                const size_t destination =
                     ( ( static_cast<size_t>( origin.z + z ) * dims.y + ( origin.y + y ) ) * dims.x + origin.x ) *
                     kCloudVolumeChannels;

                std::memcpy( atlas.data() + destination, volume.Voxels.data() + source, rowBytes );
            }
        }

        return Common::MakeSuccess( true );
    }
} // namespace Desert::Graphic
