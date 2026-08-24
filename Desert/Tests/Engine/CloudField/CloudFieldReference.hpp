#pragma once

// Compiles Editor/Resources/Shaders/Common/CloudField.glslh AS C++, together with the two headers it
// requires: Common/CloudNoise.glslh (the noise it is fed) and Common/CloudGeometry.glslh (the types and
// the units it shares).
//
// THE NOISE CALLBACK IS THE POINT OF THE SEAM. CloudField.glslh declares no sampler; it asks for the four
// octaves through CLOUD_SAMPLE_NOISE, which the march defines as a fetch of the baked volume and this
// header defines as a call into the very function that GENERATES it — CloudNoiseVolumeChannels, the same
// text Engine/Assets/CloudNoiseVolumeGenerator.cpp compiles to write the file. A test written against a
// different field would be a test of a different sky.
//
// The two differences from the GPU path are deliberate and stated rather than discovered:
//   * the volume is RGBA8, so the GPU sees the field quantized to 1/255 and trilinearly interpolated
//     between 128 voxels per axis, while this evaluates it analytically. Nothing asserted here is finer
//     than that quantization.
//   * the parameters are the volume asset's defaults, because that is what the shipped
//     CloudNoise_Default.dcnv carries; a different seed moves individual clouds and not one of the
//     statistics measured here.
//
// THE RESULTS ARE MEMOIZED. The coverage measurement evaluates the same grid of positions once per
// Coverage setting, and the field depends only on the position — so the cache turns a six-fold cost into
// a one-fold one and changes no answer. It lives here rather than in the test because the macro has to be
// defined before CloudField.glslh is included.

// THE MODELLING VOLUME ARRIVES THE SAME WAY THE NOISE DOES, and that is what makes the second half of
// this header a test of the GPU's arithmetic rather than of a re-implementation. CLOUD_SAMPLE_MODELLING is
// defined below as a TRILINEAR, REPEAT-WRAPPED read of the very bytes Assets::BakeCloudProceduralVolume
// hands the device — the same filter and the same wrap mode every sampler in this engine is created with —
// so `what the generator writes` and `what the shader reads` can be compared directly, which is the one
// relation a volume nobody can inspect on the GPU would otherwise never be checked on.
//
// IT REPLACED A PROFILE TABLE, and the replacement is the whole of phase Э5 at this seam. The table was
// `f(height in the envelope, how deep inside the patch)` multiplied by a threshold on the Alligator noise
// — whose field is `best - second` and is therefore ZERO wherever two feature points contribute equally,
// so no setting of any slider could fuse two lobes. Everything in this header below the callbacks is
// unchanged; the tests that measured the table's own shape moved to
// Desert/Tests/Engine/CloudProceduralField, which owns the generator that now decides it.

#include <Engine/Assets/CloudProceduralVolume.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Assets/CloudTypeData.hpp>
#include <Engine/Graphic/Clouds/CloudTypeShape.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace Desert::Tests::CloudFieldRef
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

        // ONE call into the ONE function that writes the volume — Common/CloudNoise.glslh's
        // CloudNoiseVolumeChannels — rather than four calls this file has to keep in step with the
        // generator's. The four-line copy that used to be here mirrored the compute bake and was a second
        // statement of the channel layout; it is exactly the shape of duplication that agrees with itself
        // until the first tuning pass.
        //
        // The parameters are the defaults of Engine/Assets/CloudNoiseVolume.hpp's CloudNoiseVolumeParams,
        // which is what the shipped CloudNoise_Default.dcnv was baked with. A different seed moves
        // individual clouds and not one of the statistics measured here.
        constexpr uint  kVolumeSeed     = 1337u;
        constexpr float kCurlStrength   = 0.33f;
        constexpr float kWispyPeriodLF  = 2.0f;
        constexpr float kWispyPeriodHF  = 4.0f;
        constexpr float kBillowPeriodLF = 3.0f;
        constexpr float kBillowPeriodHF = 6.0f;

        vec4 CloudEvaluateBakedVolume( vec3 texturePosition )
        {
            return CloudNoiseVolumeChannels( texturePosition, kVolumeSeed, kCurlStrength, kWispyPeriodLF,
                                             kWispyPeriodHF, kBillowPeriodLF, kBillowPeriodHF );
        }

        // Exact-key memoization: the key is the bit pattern of the three coordinates, so two callers that
        // ask for the same position get the same answer and nothing is ever interpolated.
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

            const vec4 value = CloudEvaluateBakedVolume( texturePosition );
            cache.emplace( key, value );
            return value;
        }

#define CLOUD_SAMPLE_NOISE( p ) CloudSampleBakedVolumeCached( p )

        // ------------------------------------------------------------------------------------------
        // The procedural modelling volume, exactly as the device would see it
        // ------------------------------------------------------------------------------------------

        // WHICH SKY THE BOUND VOLUME IS. A test that wants another one calls CloudModellingVolumeSelect
        // and the next read comes from the new bake — the same thing the renderer does when the artist
        // drops a different `.decloudtype` into a slot, and from the same single source
        // (Assets::BakeCloudProceduralVolume).
        struct ModellingVolumeState
        {
            std::vector<unsigned char>                 Voxels;
            Desert::Assets::CloudProceduralFieldParams Params;
            glm::vec2                                  OriginKm{ 0.0f };
        };

        ModellingVolumeState& ModellingVolume()
        {
            static ModellingVolumeState state;
            return state;
        }

        /// The parameters this suite bakes with: one region, centred on the origin, at the component's
        /// own defaults. The species are handed in, so a test drives exactly the set a layer would carry.
        Desert::Assets::CloudProceduralFieldParams
        CloudModellingParams( const Desert::Graphic::CloudTypeShape* shapes, std::uint32_t count, float coverage,
                              float contrast, vec3 windDirection )
        {
            Desert::Assets::CloudProceduralFieldParams params;

            params.RegionSizeKm = 48.0f;

            const Desert::Graphic::CloudEnvelopeKm envelope =
                 Desert::Graphic::CloudTypeSetEnvelopeKm( shapes, count );

            params.LayerBottomKm    = std::max( envelope.BottomKm, 0.0f );
            params.LayerThicknessKm = std::max( envelope.TopKm - params.LayerBottomKm, 0.001f );

            const float latticeKm = 3.0f;

            params.BlendRadiusKm     = 0.02f * latticeKm;
            params.ProfileDepthKm    = 0.12f * latticeKm;
            params.Coverage          = coverage;
            params.CoverageContrast  = contrast;
            params.Seed              = 1u;
            params.WindAxis          = glm::vec2( windDirection.x, windDirection.z );
            params.ResolvableChordKm = Desert::Graphic::CloudFinestResolvableChordKm( 256.0f );

            for ( std::uint32_t slot = 0; slot < count; ++slot )
            {
                Desert::Assets::CloudProceduralSpecies species;
                species.Shape      = shapes[slot];
                species.CellKm     = latticeKm * std::max( shapes[slot].PlacementScale, 1e-3f );
                species.Anisotropy = std::max( shapes[slot].PlacementAnisotropy, 1e-3f );
                params.Species.push_back( species );
            }

            return params;
        }

        void CloudModellingVolumeSelectSet( const Desert::Graphic::CloudTypeShape* shapes, std::uint32_t count,
                                            float coverage, float contrast, vec3 windDirection )
        {
            ModellingVolumeState& state = ModellingVolume();

            state.Params   = CloudModellingParams( shapes, count, coverage, contrast, windDirection );
            state.OriginKm = Desert::Assets::CloudProceduralRegionOriginKm( state.Params, 0.0f, 0.0f );

            const auto baked = Desert::Assets::BakeCloudProceduralVolume( state.Params, state.OriginKm );
            state.Voxels     = baked ? baked.GetValue() : std::vector<unsigned char>{};
        }

        /// The same bake over a layer WIDER than the species' own band, which is what makes the vertical
        /// clamp observable: the volume's top rows are then empty air and its bottom rows are cloud, so a
        /// read at a height fraction of 1 that wrapped onto the floor would come back with cloud in it.
        ///
        /// A layer is normally the union of its types' bands exactly (Graphic::CloudTypeSetEnvelopeKm), so
        /// this arrangement does not arise by itself — which is precisely why the property needs a fixture
        /// that produces it rather than a sample of an ordinary sky.
        void CloudModellingVolumeSelectOverLayer( const Desert::Graphic::CloudTypeShape* shapes,
                                                  std::uint32_t count, float coverage, float bottomKm,
                                                  float thicknessKm )
        {
            ModellingVolumeState& state = ModellingVolume();

            state.Params = CloudModellingParams( shapes, count, coverage, 1.0f, vec3( 1.0f, 0.0f, 0.0f ) );

            state.Params.LayerBottomKm    = bottomKm;
            state.Params.LayerThicknessKm = thicknessKm;

            state.OriginKm = Desert::Assets::CloudProceduralRegionOriginKm( state.Params, 0.0f, 0.0f );

            const auto baked = Desert::Assets::BakeCloudProceduralVolume( state.Params, state.OriginKm );
            state.Voxels     = baked ? baked.GetValue() : std::vector<unsigned char>{};
        }

        // A TRILINEAR, REPEAT-wrapped fetch — the filter and the address mode VulkanImage3D creates for
        // every sampled volume, written out here because the difference between this and a nearest fetch
        // is exactly the half-texel error the relation test exists to catch.
        vec4 CloudSampleModellingTexture( vec3 uvw )
        {
            const std::vector<unsigned char>& voxels = ModellingVolume().Voxels;
            if ( voxels.empty() )
                return vec4( 0.0f );

            constexpr int width  = static_cast<int>( Desert::Assets::kCloudProceduralVolumeWidth );
            constexpr int height = static_cast<int>( Desert::Assets::kCloudProceduralVolumeHeight );
            constexpr int depth  = static_cast<int>( Desert::Assets::kCloudProceduralVolumeDepth );

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
                                    Desert::Assets::kCloudProceduralBytesPerVoxel;
                return vec4( voxels[base] / 255.0f, voxels[base + 1] / 255.0f, voxels[base + 2] / 255.0f,
                             voxels[base + 3] / 255.0f );
            };

            const auto plane = [&]( int iz )
            {
                const vec4 top    = texel( x0, y0, iz ) * ( 1.0f - fx ) + texel( x1, y0, iz ) * fx;
                const vec4 bottom = texel( x0, y1, iz ) * ( 1.0f - fx ) + texel( x1, y1, iz ) * fx;
                return top * ( 1.0f - fy ) + bottom * fy;
            };

            return plane( z0 ) * ( 1.0f - fz ) + plane( z1 ) * fz;
        }

#define CLOUD_SAMPLE_MODELLING( p ) CloudSampleModellingTexture( p )

        // ------------------------------------------------------------------------------------------
        // SLOT A, DECLARED EMPTY — this suite drives producer P
        // ------------------------------------------------------------------------------------------
        //
        // The seam calls both producers now, so the authored one's four callbacks have to exist for this
        // file to compile at all. They are bound to an EMPTY list here, deliberately: what this suite
        // measures is the procedural field's own statistics — its quantiles, its coverage, its erosion —
        // and every one of them is a number about a sky with no hero cloud in it.
        //
        // THAT MAKES THIS SUITE THE REGRESSION TEST FOR "P DID NOT CHANGE". Every assertion in it was
        // written before slot A existed and none of them was touched; if the union, the cutout or the
        // early-out had altered the procedural answer by so much as a quantisation step, these numbers
        // would have moved. Producer A has its own suite, Desert/Tests/Engine/CloudAuthored.
#define CLOUD_AUTHORED_COUNT 0
#define CLOUD_AUTHORED_SLAB_COUNT 0
#define CLOUD_AUTHORED_INSTANCE( i ) CloudAuthoredNoInstance()
#define CLOUD_SAMPLE_AUTHORED( uvw ) vec4( 0.0f, 0.0f, 0.0f, 0.0f )

#include <Common/CloudAuthored.glslh>

        // Never called: the loop that would call it runs zero times. It exists because a macro has to
        // expand to something that compiles, and returning a zeroed instance is the only expansion that
        // cannot be mistaken for a real one if the count ever stops being zero by accident.
        CloudAuthoredInstance CloudAuthoredNoInstance()
        {
            CloudAuthoredInstance instance;
            instance.Row0      = vec4( 0.0f );
            instance.Row1      = vec4( 0.0f );
            instance.Row2      = vec4( 0.0f );
            instance.BoundsMin = vec4( 0.0f );
            instance.BoundsMax = vec4( 0.0f );
            return instance;
        }

#include <Common/CloudField.glslh>

        // ------------------------------------------------------------------------------------------
        // The species arrays, filled the way Graphic::PackCloudParams fills them
        // ------------------------------------------------------------------------------------------
        //
        // THE PACKER ITSELF IS NOT REACHABLE FROM HERE — CloudPayload.hpp pulls in the component, the
        // reflection macros and the atmosphere, none of which this GPU-free suite links — so what is
        // shared instead is the PURE HALF of it: Graphic::CloudSpeciesPlacementBasis and the two
        // per-species products. Those are the parts that can be wrong; the rest of the packer is a
        // transcription that ComponentReflection drives directly against the real function.
        void CloudBindSpecies( CloudFieldParams& params, const Desert::Graphic::CloudTypeShape* shapes,
                               std::uint32_t count, vec3 windDirection, float coverage = 0.35f,
                               float contrast = 1.0f )
        {
            params.SpeciesCount = static_cast<int>( count );

            for ( std::uint32_t slot = 0; slot < CLOUD_SPECIES_SLOTS; ++slot )
            {
                if ( slot >= count )
                {
                    params.SpeciesEdge[slot] = vec4( 0.0f );
                    continue;
                }

                const Desert::Graphic::CloudTypeShape& shape = shapes[slot];

                params.SpeciesEdge[slot] = vec4( shape.DetailCharacter, shape.DetailFactor, shape.DensityFactor,
                                                 shape.ExtinctionFactor );
            }

            // THE PLACEMENT BASIS IS NOT BOUND HERE BECAUSE IT NO LONGER EXISTS. It told the march how to
            // read the coverage noise in each species' own frame; the lumps are placed on a lattice in
            // that frame at BAKE time now, so what the march is handed is where the volume is instead.
            CloudModellingVolumeSelectSet( shapes, count, coverage, contrast, windDirection );

            params.RegionOriginKm  = ModellingVolume().OriginKm;
            params.InvRegionSizeKm = 1.0f / ModellingVolume().Params.RegionSizeKm;
        }

        /// The altitude fraction in the layer the bound volume was baked over — what the march computes
        /// with CloudHeightFraction and hands to the seam. Exposed so a test can ask about an ABSOLUTE
        /// altitude, which is what every meteorological assertion in this suite is written in.
        float CloudLayerHeightFraction( float altitudeKm )
        {
            const Desert::Assets::CloudProceduralFieldParams& p = ModellingVolume().Params;
            return ( altitudeKm - p.LayerBottomKm ) / std::max( p.LayerThicknessKm, 1e-6f );
        }

        /// Bind one species into the table AND into the params, which is what every single-type test in
        /// this suite wants and what a layer with one slot filled actually does.
        void CloudBindSingleSpecies( CloudFieldParams& params, const Desert::Graphic::CloudTypeShape& shape )
        {
            CloudBindSpecies( params, &shape, 1u, vec3( 1.0f, 0.0f, 0.0f ) );
        }

    } // namespace
} // namespace Desert::Tests::CloudFieldRef
