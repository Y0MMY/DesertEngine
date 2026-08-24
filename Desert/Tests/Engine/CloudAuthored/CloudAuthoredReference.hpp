#pragma once

// Compiles Editor/Resources/Shaders/Common/CloudAuthored.glslh and Common/CloudField.glslh AS C++,
// together with the two headers they require, and feeds the seam a REAL sculpted body: the very voxels
// Assets::GenerateCloudModellingVolume writes into a `.dcmv`, read through a trilinear filter that
// reproduces the sampler the device creates.
//
// WHY THAT MATTERS MORE HERE THAN ANYWHERE ELSE IN THIS PROGRAMME. Slot A is the first place where the
// cloud field reads DATA rather than computing a function, so "the volume and its reading agree" is a
// relation with two sides that can drift — the generator's voxel layout and the shader's addressing —
// and neither side can see the other. A test that re-implemented either would be a test of itself. So
// the generator is the engine's own, the addressing is the shader's own text, and what is asserted is
// that they meet.
//
// THE THREE CALLBACKS, and they are the same mechanism CloudFieldReference.hpp uses for the noise and
// the profile table:
//
//     CLOUD_SAMPLE_AUTHORED( uvw )   -> a trilinear, REPEAT-wrapped read of the baked ATLAS
//     CLOUD_AUTHORED_COUNT           -> the instance list this test set up
//     CLOUD_AUTHORED_SLAB_COUNT      -> how many bodies that atlas holds
//     CLOUD_AUTHORED_INSTANCE( i )   -> one of them
//
// The instance list is a mutable global rather than a parameter because the macro's expansion has to be
// an expression and the seam takes no argument for it — which is exactly the shape the storage block
// has on the GPU, so the test drives the same code path with the same indirection.

#include <Engine/Assets/CloudModellingCatalogue.hpp>
#include <Engine/Assets/CloudModellingVolume.hpp>
#include <Engine/Assets/CloudTypeData.hpp>
#include <Engine/Graphic/Clouds/CloudAuthoredPayload.hpp>
#include <Engine/Graphic/Clouds/CloudTypeShape.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <map>
#include <vector>

namespace Desert::Tests::CloudAuthoredRef
{
    namespace
    {
        using vec2 = glm::vec2;
        using vec3 = glm::vec3;
        using vec4 = glm::vec4;

        using uint = std::uint32_t;

        using glm::abs;
        using glm::clamp;
        using glm::dot;
        using glm::floor;
        using glm::length;
        using glm::max;
        using glm::min;
        using glm::mix;
        using glm::mod;
        using glm::pow;
        using glm::smoothstep;
        using glm::sqrt;

#include <Common/CloudNoise.glslh>
#include <Common/CloudGeometry.glslh>

        // ------------------------------------------------------------------------------------------
        // Producer P's two callbacks, identical in construction to CloudFieldReference.hpp's
        // ------------------------------------------------------------------------------------------
        //
        // They are here rather than shared with that header because the two suites are compiled as
        // separate binaries and the dialect namespace is anonymous by construction; what is shared is the
        // SOURCE they both drive, which is the shader text.

        constexpr uint  kVolumeSeed     = 1337u;
        constexpr float kCurlStrength   = 0.33f;
        constexpr float kWispyPeriodLF  = 2.0f;
        constexpr float kWispyPeriodHF  = 4.0f;
        constexpr float kBillowPeriodLF = 3.0f;
        constexpr float kBillowPeriodHF = 6.0f;

        using NoiseKey = std::array<std::uint32_t, 3>;

        std::map<NoiseKey, vec4>& NoiseCache()
        {
            static std::map<NoiseKey, vec4> cache;
            return cache;
        }

        vec4 CloudSampleBakedVolumeCached( vec3 texturePosition )
        {
            NoiseKey key{};
            std::memcpy( key.data(), &texturePosition.x, sizeof( float ) );
            std::memcpy( key.data() + 1, &texturePosition.y, sizeof( float ) );
            std::memcpy( key.data() + 2, &texturePosition.z, sizeof( float ) );

            auto&      cache = NoiseCache();
            const auto it    = cache.find( key );
            if ( it != cache.end() )
                return it->second;

            const vec4 value =
                 CloudNoiseVolumeChannels( texturePosition, kVolumeSeed, kCurlStrength, kWispyPeriodLF,
                                           kWispyPeriodHF, kBillowPeriodLF, kBillowPeriodHF );
            cache.emplace( key, value );
            return value;
        }

#define CLOUD_SAMPLE_NOISE( p ) CloudSampleBakedVolumeCached( p )

        std::vector<float>& ProfileTable()
        {
            static std::vector<float> texels =
                 Desert::Graphic::CloudBuildProfileTable( Desert::Assets::CloudTypeDefaultShape() );
            return texels;
        }

        vec4 CloudSampleProfileTexture( vec2 uv )
        {
            const std::vector<float>& texels = ProfileTable();

            constexpr int width  = static_cast<int>( Desert::Graphic::kCloudProfileTableAltitudeTexels );
            constexpr int height = static_cast<int>( Desert::Graphic::kCloudProfileTablePatternTexels );

            const float x = uv.x * static_cast<float>( width ) - 0.5f;
            const float y = uv.y * static_cast<float>( height ) - 0.5f;

            const float fx = x - std::floor( x );
            const float fy = y - std::floor( y );

            const auto wrap = []( float coordinate, int extent )
            {
                const int index = static_cast<int>( std::floor( coordinate ) ) % extent;
                return index < 0 ? index + extent : index;
            };

            const int x0 = wrap( x, width );
            const int y0 = wrap( y, height );
            const int x1 = ( x0 + 1 ) % width;
            const int y1 = ( y0 + 1 ) % height;

            const auto texel = [&]( int ix, int iy )
            {
                const size_t base =
                     ( static_cast<size_t>( iy ) * width + ix ) * Desert::Graphic::kCloudProfileTableChannels;
                return vec4( texels[base], texels[base + 1], texels[base + 2], texels[base + 3] );
            };

            const vec4 top    = texel( x0, y0 ) * ( 1.0f - fx ) + texel( x1, y0 ) * fx;
            const vec4 bottom = texel( x0, y1 ) * ( 1.0f - fx ) + texel( x1, y1 ) * fx;

            return top * ( 1.0f - fy ) + bottom * fy;
        }

#define CLOUD_SAMPLE_PROFILE( uv ) CloudSampleProfileTexture( uv )

        // ------------------------------------------------------------------------------------------
        // Producer A: the baked body, and the device's own filter over it
        // ------------------------------------------------------------------------------------------

        /// The shipped example, baked ONCE per test binary. About a second and a half in a debug build,
        /// which is why it is a lazy static rather than a fixture member: every test in this suite reads
        /// the same body, and baking it per test would turn a two-second suite into a minute.
        const Desert::Assets::CloudModellingVolumeData& Body()
        {
            static const Desert::Assets::CloudModellingVolumeData body = []
            {
                Desert::Assets::CloudModellingVolumeData data;
                data.Recipe = Desert::Assets::CloudModellingDefaultRecipe();

                auto voxels = Desert::Assets::GenerateCloudModellingVolume( data.Recipe );
                if ( voxels )
                    data.Voxels = voxels.ExtractValue();
                return data;
            }();
            return body;
        }

        /// A second and a third body, so this suite can drive the atlas with bodies that are DIFFERENT
        /// and not merely repeated. Two of the catalogue's genera, chosen because the arch and the
        /// cumulonimbus disagree everywhere: what a test wants from a second slab is that reading the
        /// wrong one is visible, and two similar clouds would hide exactly that.
        const std::vector<unsigned char>& CatalogueBody( Desert::Assets::CloudModellingSpecies species )
        {
            static std::map<uint32_t, std::vector<unsigned char>> baked;

            const uint32_t key = static_cast<uint32_t>( species );
            const auto     it  = baked.find( key );
            if ( it != baked.end() )
                return it->second;

            auto voxels = Desert::Assets::GenerateCloudModellingVolume(
                 Desert::Assets::CloudModellingCatalogueRecipe( species ) );
            return baked.emplace( key, voxels ? voxels.ExtractValue() : std::vector<unsigned char>{} )
                 .first->second;
        }

        // THE ATLAS THE SEAM READS, in the shape the device has it: one byte array holding N bodies end to
        // end along the depth axis, assembled by the engine's own Assets::AssembleCloudModellingAtlas
        // rather than by a copy written here — a test that re-implemented the packing would be a test of
        // itself, which is the same rule the generator is driven by above.
        std::vector<unsigned char> g_Atlas;
        int                        g_AuthoredSlabCount = 0;

        /// What g_Atlas was last built from, so that resetting to the ordinary one-body case between two
        /// thousand probes does not re-assemble four megabytes two thousand times.
        std::vector<const std::vector<unsigned char>*> g_AtlasSources;

        void SetAtlas( const std::vector<const std::vector<unsigned char>*>& bodies )
        {
            if ( bodies == g_AtlasSources )
                return;

            const auto assembled = Desert::Assets::AssembleCloudModellingAtlas( bodies );
            g_Atlas              = assembled ? assembled.GetValue() : std::vector<unsigned char>{};
            g_AuthoredSlabCount  = assembled ? static_cast<int>( bodies.size() ) : 0;
            g_AtlasSources       = assembled ? bodies : std::vector<const std::vector<unsigned char>*>{};
        }

        /// The ordinary case: one body, one slab — which is what A0 and A1 shipped and what the six-point
        /// protocol renders.
        void SetSingleBodyAtlas()
        {
            SetAtlas( { &Body().Voxels } );
        }

        /**
         * A trilinear, REPEAT-wrapped fetch of the baked voxels — the filter and the address mode
         * VulkanImage3D creates for every volume this engine uploads.
         *
         * WRITTEN OUT RATHER THAN APPROXIMATED, because the half-texel offset is the whole point: the
         * shader clamps its coordinate to the texel centres before it fetches (CloudAuthoredClampUvw), and
         * a nearest-neighbour reference would agree with a broken clamp exactly as often as with a correct
         * one.
         */
        vec4 CloudSampleAuthoredVolume( vec3 uvw )
        {
            const std::vector<unsigned char>& body = g_Atlas;

            constexpr int width  = static_cast<int>( Desert::Assets::kCloudModellingVolumeWidth );
            constexpr int height = static_cast<int>( Desert::Assets::kCloudModellingVolumeHeight );
            const int     depth  = static_cast<int>( Desert::Assets::kCloudModellingVolumeDepth ) *
                              std::max( g_AuthoredSlabCount, 1 );

            const float x = uvw.x * static_cast<float>( width ) - 0.5f;
            const float y = uvw.y * static_cast<float>( height ) - 0.5f;
            const float z = uvw.z * static_cast<float>( depth ) - 0.5f;

            const float fx = x - std::floor( x );
            const float fy = y - std::floor( y );
            const float fz = z - std::floor( z );

            const auto wrap = []( float coordinate, int extent )
            {
                const int index = static_cast<int>( std::floor( coordinate ) ) % extent;
                return index < 0 ? index + extent : index;
            };

            const int x0 = wrap( x, width );
            const int y0 = wrap( y, height );
            const int z0 = wrap( z, depth );
            const int x1 = ( x0 + 1 ) % width;
            const int y1 = ( y0 + 1 ) % height;
            const int z1 = ( z0 + 1 ) % depth;

            const auto texel = [&]( int ix, int iy, int iz )
            {
                const size_t base = ( ( static_cast<size_t>( iz ) * height + iy ) * width + ix ) *
                                    Desert::Assets::kCloudModellingBytesPerVoxel;
                return vec4( static_cast<float>( body[base + 0] ) / 255.0f,
                             static_cast<float>( body[base + 1] ) / 255.0f,
                             static_cast<float>( body[base + 2] ) / 255.0f,
                             static_cast<float>( body[base + 3] ) / 255.0f );
            };

            const auto lerp3 = []( const vec4& a, const vec4& b, float t ) { return a * ( 1.0f - t ) + b * t; };

            const vec4 c00 = lerp3( texel( x0, y0, z0 ), texel( x1, y0, z0 ), fx );
            const vec4 c10 = lerp3( texel( x0, y1, z0 ), texel( x1, y1, z0 ), fx );
            const vec4 c01 = lerp3( texel( x0, y0, z1 ), texel( x1, y0, z1 ), fx );
            const vec4 c11 = lerp3( texel( x0, y1, z1 ), texel( x1, y1, z1 ), fx );

            return lerp3( lerp3( c00, c10, fy ), lerp3( c01, c11, fy ), fz );
        }

#define CLOUD_SAMPLE_AUTHORED( uvw ) CloudSampleAuthoredVolume( uvw )

        // The instance list the seam reads, in the shape the storage block has on the GPU: an array and a
        // count, reached through a macro rather than passed as an argument.
        int                                       g_AuthoredCount = 0;
        Desert::Graphic::CloudAuthoredInstanceGpu g_AuthoredInstances[Desert::Graphic::kCloudAuthoredSlots]{};

#define CLOUD_AUTHORED_COUNT g_AuthoredCount
#define CLOUD_AUTHORED_SLAB_COUNT g_AuthoredSlabCount
#define CLOUD_AUTHORED_INSTANCE( i ) CloudAuthoredInstanceAt( i )

#include <Common/CloudAuthored.glslh>

        /**
         * The bridge between the C++ payload and the dialect's own struct — and the ASSERTION that the two
         * are one layout.
         *
         * It is a `memcpy` and not a field-by-field copy on purpose. Graphic::CloudAuthoredInstanceGpu is
         * what the renderer writes into the storage buffer and `CloudAuthoredInstance` is what the shader
         * reads out of it; on the device that IS a reinterpretation of the same bytes, so a test that
         * copied member by member would be testing a translation nothing performs. Copying the bytes means
         * a member inserted into one and not the other is caught here rather than in a frame.
         */
        CloudAuthoredInstance CloudAuthoredInstanceAt( int index )
        {
            static_assert( sizeof( CloudAuthoredInstance ) == sizeof( Desert::Graphic::CloudAuthoredInstanceGpu ),
                           "The dialect's instance and the payload's instance are one layout." );

            CloudAuthoredInstance instance;
            std::memcpy( &instance, &g_AuthoredInstances[index], sizeof( instance ) );
            return instance;
        }

#include <Common/CloudField.glslh>

        // ------------------------------------------------------------------------------------------
        // Setting the scene
        // ------------------------------------------------------------------------------------------

        /// Empties the instance list and NOTHING else. The atlas stays bound, which is what a sweep over
        /// thousands of positions needs: re-assembling a three-body atlas per probe is twelve megabytes of
        /// memcpy per probe, and a suite that took eight minutes to say something true is a suite nobody
        /// runs.
        void ClearInstanceList()
        {
            g_AuthoredCount = 0;
            for ( auto& instance : g_AuthoredInstances )
                instance = Desert::Graphic::CloudAuthoredInstanceGpu{};
        }

        /// Empties the instance list AND puts the atlas back to the one-body case, because a test that
        /// left three slabs bound would hand the next one a body it never asked for — and the whole point
        /// of the slab index is that reading the wrong one is silent.
        void ClearInstances()
        {
            ClearInstanceList();
            SetSingleBodyAtlas();
        }

        void AddInstance( const Desert::Graphic::CloudAuthoredInstanceGpu& instance )
        {
            g_AuthoredInstances[g_AuthoredCount] = instance;
            ++g_AuthoredCount;
        }

        /// The layer the procedural producer is measured against here: the built-in cumulus congestus,
        /// which is what an empty slot resolves to and therefore what Clouds_Demo renders.
        CloudFieldParams DefaultParams()
        {
            const Desert::Graphic::CloudTypeShape& shape = Desert::Assets::CloudTypeDefaultShape();

            CloudFieldParams params;
            params.WeatherTileKm    = 12.0f;
            params.Coverage         = 0.24f;
            params.CoverageContrast = 1.0f;
            params.DetailTileKm     = 1.2f;
            params.DetailStrength   = 0.35f;
            params.DensityScale     = 1.0f;
            params.SpeciesCount     = 1;
            params.WindOffsetKm     = vec3( 0.0f );

            for ( int slot = 0; slot < CLOUD_SPECIES_SLOTS; ++slot )
            {
                params.SpeciesEdge[slot]      = vec4( 0.0f );
                params.SpeciesPlacement[slot] = vec4( 0.0f );
            }

            params.SpeciesEdge[0] =
                 vec4( shape.DetailCharacter, shape.DetailFactor, shape.DensityFactor, shape.ExtinctionFactor );

            const Desert::Graphic::CloudPlacementBasis basis =
                 Desert::Graphic::CloudSpeciesPlacementBasis( shape, 1.0f, 0.0f );
            params.SpeciesPlacement[0] = vec4( basis.AlongX, basis.AlongZ, basis.AcrossX, basis.AcrossZ );

            return params;
        }
    } // namespace
} // namespace Desert::Tests::CloudAuthoredRef
