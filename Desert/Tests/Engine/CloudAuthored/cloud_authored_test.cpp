// Slot A of the cloud field's seam: the sculpted body, its container, the union that joins it to the
// procedural producer, and the cutout that keeps the procedural producer out of it.
//
// WHAT THIS SUITE IS FOR, said as the defect it prevents. The seam has two sides that can drift and
// nothing else checks them:
//
//   * the GENERATOR writes voxels and the SHADER addresses them — two statements of one layout, in two
//     languages, neither able to see the other. `VolumeAndItsReadingAgree` is that relation.
//   * the C++ payload writes bytes into a storage buffer and the shader reads a struct out of it. The
//     reference header asserts that by copying the BYTES rather than the members.
//   * the union is claimed to be order-independent, which is the property that lets an artist sculpt in
//     any order and a scene list its hero clouds in any order. Two tests assert it: one on the bake
//     (shuffle the lumps) and one on the seam (swap the instances).
//
// AND ONE CLAIM THAT IS THE WHOLE POINT OF THE PHASE. The procedural field cannot make a fused mass —
// the Alligator's `best - second` lays a zero between every pair of cells by construction — so the
// accepting frame of Э4 is a cloud that IS one connected body. `TheBodyIsOneConnectedMass` measures
// exactly that, on the shipped volume, by flood fill.
//
// GPU-free: the shader text is compiled as C++ (see CloudAuthoredReference.hpp).

#include "CloudAuthoredReference.hpp"

#include <Common/Core/Constants.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace Desert::Tests::CloudAuthoredRef;

namespace
{
    namespace Assets  = Desert::Assets;
    namespace Graphic = Desert::Graphic;

    constexpr float kLayerBottomKm    = 2.2f; // the shipped cumulus congestus' base
    constexpr float kLayerThicknessKm = 3.6f; // ... and 5.8 - 2.2, its own envelope

    /// A hero cloud placed at a field-space position, with an optional rotation about Y and a uniform
    /// scale. Built through the packer the renderer uses, never by hand: what is under test includes the
    /// packer.
    Graphic::CloudAuthoredInstanceGpu MakeInstance( glm::vec3 centreFieldKm, float scale = 1.0f,
                                                    float yawRadians = 0.0f, float strength = 1.0f,
                                                    bool cutout = false )
    {
        Desert::ECS::HeroCloudData data;
        data.Strength                = strength;
        data.SuppressProceduralField = cutout;
        data.DetailFactor            = 1.0f;
        data.DensityFactor           = 1.0f;
        data.ExtinctionFactor        = 1.0f;

        const glm::vec3 worldCm{ centreFieldKm.x * Graphic::kCloudWorldUnitsPerKm,
                                 ( centreFieldKm.y + kLayerBottomKm ) * Graphic::kCloudWorldUnitsPerKm,
                                 centreFieldKm.z * Graphic::kCloudWorldUnitsPerKm };

        const float c = std::cos( yawRadians );
        const float s = std::sin( yawRadians );

        glm::mat4 transform( 1.0f );
        transform[0] = glm::vec4( c * scale, 0.0f, -s * scale, 0.0f );
        transform[1] = glm::vec4( 0.0f, scale, 0.0f, 0.0f );
        transform[2] = glm::vec4( s * scale, 0.0f, c * scale, 0.0f );
        transform[3] = glm::vec4( worldCm, 1.0f );

        const auto packed =
             Graphic::PackCloudAuthoredInstance( transform, Body().Recipe.SizeKm, kLayerBottomKm, data );
        EXPECT_TRUE( packed.Valid );
        return packed.Instance;
    }

    /// A field position where the PROCEDURAL producer has something in it, found rather than assumed: the
    /// coverage the shipped scene uses puts cloud over about a quarter of the sky, so a hardcoded point
    /// would be a point that stops being inside a cloud the first time anything is retuned.
    bool FindProceduralPoint( const CloudFieldParams& params, float heightFraction, glm::vec3& outPositionKm )
    {
        for ( int ix = -40; ix <= 40; ++ix )
        {
            for ( int iz = -40; iz <= 40; ++iz )
            {
                const glm::vec3 position( static_cast<float>( ix ) * 0.5f, heightFraction * kLayerThicknessKm,
                                          static_cast<float>( iz ) * 0.5f );

                ClearInstances();
                if ( SampleCloudField( params, heightFraction, position ).Profile > 0.2f )
                {
                    outPositionKm = position;
                    return true;
                }
            }
        }
        return false;
    }

    /// A point inside @p gpu's body where the AUTHORED profile is small, the ENVELOPE is saturated, and
    /// the PROCEDURAL field is bigger than the authored one — which is the only place in the sky where the
    /// cutout is observable at all.
    ///
    /// WHY IT HAS TO BE SEARCHED FOR, and this is the lesson the first version of these tests taught. At
    /// the body's centre the authored profile is 1, so `max` keeps it whether the cutout fired or not and
    /// the test passes with the cutout deleted. Winding the body down with Strength does not help either:
    /// Strength scales the cutout WITH the body by design, so a faint body has a faint cutout. What is
    /// left is the body's own OUTER SHELL — barely inside the surface, so the profile is a few per cent,
    /// and well inside the dilated envelope, so the cutout is full.
    bool FindCutoutPoint( const CloudFieldParams& params, const Graphic::CloudAuthoredInstanceGpu& gpu,
                          float fraction, glm::vec3& outPointKm, float& outAuthored )
    {
        ClearInstances();
        AddInstance( gpu );
        const CloudAuthoredInstance instance = CloudAuthoredInstanceAt( 0 );

        for ( int ix = 0; ix <= 40; ++ix )
        {
            for ( int iz = 0; iz <= 40; ++iz )
            {
                const glm::vec3 point( mix( gpu.BoundsMin.x, gpu.BoundsMax.x, static_cast<float>( ix ) / 40.0f ),
                                       fraction * kLayerThicknessKm,
                                       mix( gpu.BoundsMin.z, gpu.BoundsMax.z, static_cast<float>( iz ) / 40.0f ) );

                if ( !CloudAuthoredInVolume( CloudAuthoredLocalUvw( instance, point ) ) )
                    continue;

                const vec4 voxel =
                     CLOUD_SAMPLE_AUTHORED( CloudAuthoredClampUvw( CloudAuthoredLocalUvw( instance, point ) ) );

                if ( voxel.w < 0.98f || voxel.x <= 0.0f || voxel.x > 0.35f )
                    continue;

                ClearInstances();
                const float procedural = SampleCloudField( params, fraction, point ).Profile;

                ClearInstances();
                AddInstance( gpu );

                if ( procedural < voxel.x + 0.15f )
                    continue;

                outPointKm  = point;
                outAuthored = voxel.x;
                return true;
            }
        }
        return false;
    }

    size_t VoxelIndex( uint32_t x, uint32_t y, uint32_t z )
    {
        return ( ( static_cast<size_t>( z ) * Assets::kCloudModellingVolumeHeight + y ) *
                      Assets::kCloudModellingVolumeWidth +
                 x ) *
               Assets::kCloudModellingBytesPerVoxel;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// The container and the generator
// ---------------------------------------------------------------------------------------------------

TEST( CloudModellingVolume, TheShippedRecipeBakes )
{
    const auto& body = Body();
    ASSERT_EQ( body.Voxels.size(), Assets::kCloudModellingVoxelBytes );
    EXPECT_EQ( body.Voxels.size(), 128u * 64u * 128u * 4u );

    // 4.00 MiB exactly, which is the arithmetic PLAN_AUTHORED_CLOUDS.md section 2 is built on.
    EXPECT_NEAR( static_cast<double>( body.Voxels.size() ) / ( 1024.0 * 1024.0 ), 4.0, 1e-9 );
}

TEST( CloudModellingVolume, TheContainerRoundTrips )
{
    Assets::CloudModellingVolumeData data = Body();

    const std::vector<unsigned char> encoded = Assets::EncodeCloudModellingVolume( data );

    // The file's size is a formula and not an observation: a header that grows without
    // kCloudModellingHeaderSize moving would pass a test that meant nothing.
    EXPECT_EQ( encoded.size(), Assets::kCloudModellingHeaderSize +
                                    data.Recipe.Blobs.size() * Assets::kCloudModellingBlobBytes +
                                    Assets::kCloudModellingVoxelBytes );

    const auto decoded = Assets::DecodeCloudModellingVolume( encoded );
    ASSERT_TRUE( decoded ) << decoded.GetError();

    const Assets::CloudModellingVolumeData& read = decoded.GetValue();
    EXPECT_EQ( read.Voxels, data.Voxels );
    EXPECT_EQ( read.Recipe.Blobs.size(), data.Recipe.Blobs.size() );
    EXPECT_FLOAT_EQ( read.Recipe.SizeKm.x, data.Recipe.SizeKm.x );
    EXPECT_FLOAT_EQ( read.Recipe.SizeKm.y, data.Recipe.SizeKm.y );
    EXPECT_FLOAT_EQ( read.Recipe.SizeKm.z, data.Recipe.SizeKm.z );
    EXPECT_FLOAT_EQ( read.Recipe.BlendRadiusKm, data.Recipe.BlendRadiusKm );
    EXPECT_FLOAT_EQ( read.Recipe.ProfileDepthKm, data.Recipe.ProfileDepthKm );
    EXPECT_FLOAT_EQ( read.Recipe.EnvelopeMarginKm, data.Recipe.EnvelopeMarginKm );

    for ( size_t i = 0; i < read.Recipe.Blobs.size(); ++i )
    {
        EXPECT_FLOAT_EQ( read.Recipe.Blobs[i].CentreKm.y, data.Recipe.Blobs[i].CentreKm.y ) << "lump " << i;
        EXPECT_FLOAT_EQ( read.Recipe.Blobs[i].RadiiKm.x, data.Recipe.Blobs[i].RadiiKm.x ) << "lump " << i;
        EXPECT_FLOAT_EQ( read.Recipe.Blobs[i].DetailType, data.Recipe.Blobs[i].DetailType ) << "lump " << i;
        EXPECT_FLOAT_EQ( read.Recipe.Blobs[i].DensityScale, data.Recipe.Blobs[i].DensityScale ) << "lump " << i;
    }
}

TEST( CloudModellingVolume, ACorruptPayloadIsRefusedRatherThanRead )
{
    std::vector<unsigned char> encoded = Assets::EncodeCloudModellingVolume( Body() );

    // One bit, in the middle of the voxels. Length is unchanged, so only the checksum can catch it — and
    // the failure it would otherwise produce is a cloud with a wrong edge, which reads as a tuning
    // problem rather than as a corrupt file.
    encoded[encoded.size() / 2] ^= 0x01u;

    const auto decoded = Assets::DecodeCloudModellingVolume( encoded );
    EXPECT_FALSE( decoded );
    EXPECT_NE( decoded.GetError().find( "checksum" ), std::string::npos ) << decoded.GetError();
}

TEST( CloudModellingVolume, TheJoinIsOrderIndependent )
{
    // COMMUTATIVE AND ASSOCIATIVE is the first of the three properties the exponential smooth-min was
    // chosen for (PLAN_AUTHORED_CLOUDS.md section 3), and it is what lets an artist sculpt in any order
    // and a baker store no order. Asserted on the BYTES, which is the strongest form available: not
    // "close enough", identical.
    Assets::CloudModellingVolumeRecipe shuffled = Body().Recipe;

    std::mt19937 rng( 20260820u );
    std::shuffle( shuffled.Blobs.begin(), shuffled.Blobs.end(), rng );

    const auto rebaked = Assets::GenerateCloudModellingVolume( shuffled );
    ASSERT_TRUE( rebaked ) << rebaked.GetError();

    size_t      differing = 0;
    int         maxDelta  = 0;
    const auto& before    = Body().Voxels;
    const auto& after     = rebaked.GetValue();
    for ( size_t i = 0; i < before.size(); ++i )
    {
        const int delta = std::abs( static_cast<int>( before[i] ) - static_cast<int>( after[i] ) );
        if ( delta != 0 )
        {
            ++differing;
            maxDelta = std::max( maxDelta, delta );
        }
    }
    EXPECT_EQ( differing, 0u ) << "shuffling the lumps moved " << differing << " bytes, by up to " << maxDelta;
}

TEST( CloudModellingVolume, TheBodyIsOneConnectedMass )
{
    // THE ACCEPTING CLAIM OF THE WHOLE PHASE, measured rather than asserted.
    //
    // The procedural producer cannot make this shape and the reason is structural: its coverage field is
    // an Alligator, `best - second`, which is exactly zero on the bisector between every pair of feature
    // points — so two neighbouring lobes are ALWAYS separated by a surface of zeroes and a threshold can
    // never join them. Three tasks measured that independently before this phase was approved.
    //
    // A smooth-min union has the opposite property by construction, and this is what that means in the
    // shipped volume: every voxel with any body in it belongs to ONE six-connected component.
    const auto& body = Body();

    const uint32_t width  = Assets::kCloudModellingVolumeWidth;
    const uint32_t height = Assets::kCloudModellingVolumeHeight;
    const uint32_t depth  = Assets::kCloudModellingVolumeDepth;

    std::vector<char> visited( static_cast<size_t>( width ) * height * depth, 0 );

    const auto solid = [&]( uint32_t x, uint32_t y, uint32_t z )
    { return body.Voxels[VoxelIndex( x, y, z )] > 0u; };

    const auto flat = [&]( uint32_t x, uint32_t y, uint32_t z )
    { return ( static_cast<size_t>( z ) * height + y ) * width + x; };

    size_t                               components = 0;
    size_t                               solidCount = 0;
    std::vector<std::array<uint32_t, 3>> stack;

    for ( uint32_t z = 0; z < depth; ++z )
    {
        for ( uint32_t y = 0; y < height; ++y )
        {
            for ( uint32_t x = 0; x < width; ++x )
            {
                if ( !solid( x, y, z ) || visited[flat( x, y, z )] )
                    continue;

                ++components;
                stack.push_back( { x, y, z } );
                visited[flat( x, y, z )] = 1;

                while ( !stack.empty() )
                {
                    const auto voxel = stack.back();
                    stack.pop_back();
                    ++solidCount;

                    const int offsets[6][3] = { { 1, 0, 0 },  { -1, 0, 0 }, { 0, 1, 0 },
                                                { 0, -1, 0 }, { 0, 0, 1 },  { 0, 0, -1 } };
                    for ( const auto& offset : offsets )
                    {
                        const long nx = static_cast<long>( voxel[0] ) + offset[0];
                        const long ny = static_cast<long>( voxel[1] ) + offset[1];
                        const long nz = static_cast<long>( voxel[2] ) + offset[2];

                        if ( nx < 0 || ny < 0 || nz < 0 || nx >= width || ny >= height || nz >= depth )
                            continue;

                        const uint32_t ux = static_cast<uint32_t>( nx );
                        const uint32_t uy = static_cast<uint32_t>( ny );
                        const uint32_t uz = static_cast<uint32_t>( nz );

                        if ( !solid( ux, uy, uz ) || visited[flat( ux, uy, uz )] )
                            continue;

                        visited[flat( ux, uy, uz )] = 1;
                        stack.push_back( { ux, uy, uz } );
                    }
                }
            }
        }
    }

    EXPECT_EQ( components, 1u ) << "the sculpted body has fallen into " << components
                                << " pieces; lumps that do not overlap are a string of beads, not a cumulus";

    // And it is a body rather than a speck: about a tenth of the box, which is what a cumulus of these
    // proportions occupies in a box drawn round it.
    const double occupancy =
         static_cast<double>( solidCount ) / static_cast<double>( static_cast<size_t>( width ) * height * depth );
    EXPECT_GT( occupancy, 0.04 );
    EXPECT_LT( occupancy, 0.30 );
}

TEST( CloudModellingVolume, TheBlendRadiusAddsMaterialInTheCreases )
{
    // WHAT THE CONNECTIVITY TEST ABOVE DOES *NOT* MEASURE, found by breaking it: dropping the blend
    // radius to a micrometre leaves the body in one piece, because these lumps OVERLAP and a union of
    // overlapping ellipsoids is connected at any sharpness. Connectivity is a property of the recipe;
    // smoothness is the property of the JOIN, and it needs its own assertion or the third of the three
    // reasons the exponential smooth-min was chosen is untested.
    //
    // The measurable consequence is that the join is a strict LOWER bound on the distance — `smin <= min`
    // — so a wider blend radius pushes the surface outward and fills the creases where two lumps meet.
    // More blend, more body, monotonically.
    Assets::CloudModellingVolumeRecipe sharp = Body().Recipe;
    sharp.BlendRadiusKm                      = 0.001f;

    const auto sharpBake = Assets::GenerateCloudModellingVolume( sharp );
    ASSERT_TRUE( sharpBake ) << sharpBake.GetError();

    const auto solidCount = []( const std::vector<unsigned char>& voxels )
    {
        size_t n = 0;
        for ( size_t i = 0; i < voxels.size(); i += 4 )
        {
            if ( voxels[i] > 0u )
                ++n;
        }
        return n;
    };

    const size_t smooth = solidCount( Body().Voxels );
    const size_t hard   = solidCount( sharpBake.GetValue() );

    EXPECT_GT( smooth, hard ) << "the 50 m blend radius added no material at all against a 1 m one, so the "
                                 "join is behaving like a plain min and the creases between the lumps are "
                                 "sharp";
}

TEST( CloudModellingVolume, TheEnvelopeIsConservative )
{
    // The cutout reads the envelope, so it MUST cover the body: a voxel with profile and no envelope is a
    // piece of cloud the procedural field is allowed to grow through, which is the exact defect the
    // cutout exists to prevent.
    const auto& body = Body();

    size_t violations = 0;
    for ( size_t i = 0; i < body.Voxels.size(); i += 4 )
    {
        if ( body.Voxels[i] > 0u && body.Voxels[i + 3] == 0u )
            ++violations;
    }

    EXPECT_EQ( violations, 0u );
}

TEST( CloudModellingVolume, ValidateRefusesABodyThatWouldTouchItsBox )
{
    Assets::CloudModellingVolumeRecipe recipe = Body().Recipe;

    // A BOX THE LUMPS THEMSELVES FIT IN, and nothing else. The recipe reaches 0.76 x 0.30 x 0.54 km from
    // its centre; the join inflates that by 0.104 km and the envelope dilates it by another 0.09, so a
    // half-size of 0.85 x 0.40 x 0.65 accepts the lumps and must still refuse the body.
    //
    // Sized this way rather than halved, because a halved box is refused even with the slack term
    // deleted — which is what breaking the slack term proved, and it means the halved version was testing
    // the wrong half of the arithmetic.
    recipe.SizeKm = glm::vec3( 1.70f, 0.80f, 1.30f );

    const auto valid = Assets::ValidateCloudModellingRecipe( recipe );
    EXPECT_FALSE( valid );
    EXPECT_NE( valid.GetError().find( "does not fit" ), std::string::npos ) << valid.GetError();
}

TEST( CloudModellingVolume, ThePathsTheAssetSystemAndTheFormatAgreeOn )
{
    // Two statements of one directory, so it is a test rather than a hope — the same relation
    // Desert/Tests/Engine/CloudType asserts for the type library.
    const std::string full = Common::Constants::Path::CLOUD_VOLUME_PATH.generic_string();
    const std::string tail = Assets::kCloudModellingAssetsRelativeDir;

    ASSERT_GE( full.size(), tail.size() );
    EXPECT_EQ( full.compare( full.size() - tail.size(), tail.size(), tail ), 0 ) << full << " vs " << tail;
}

// ---------------------------------------------------------------------------------------------------
// The volume and its reading
// ---------------------------------------------------------------------------------------------------

TEST( CloudAuthored, TheShaderAndTheEngineAgreeOnTheVolumeShape )
{
    EXPECT_EQ( static_cast<uint32_t>( CLOUD_MODELLING_VOLUME_WIDTH ), Assets::kCloudModellingVolumeWidth );
    EXPECT_EQ( static_cast<uint32_t>( CLOUD_MODELLING_VOLUME_HEIGHT ), Assets::kCloudModellingVolumeHeight );
    EXPECT_EQ( static_cast<uint32_t>( CLOUD_MODELLING_VOLUME_DEPTH ), Assets::kCloudModellingVolumeDepth );
    EXPECT_EQ( static_cast<uint32_t>( CLOUD_AUTHORED_SLOTS ), Graphic::kCloudAuthoredSlots );
}

TEST( CloudAuthored, VolumeAndItsReadingAgree )
{
    // THE RELATION THIS SUITE EXISTS FOR. The generator writes `((z * H + y) * W + x) * 4` and the shader
    // addresses `uvw * extent - 0.5`; both are individually reasonable and a disagreement between them is
    // a cloud whose lumps are in the wrong places, which looks like a sculpting mistake.
    //
    // Read at TEXEL CENTRES, where a trilinear filter returns exactly the texel it sits on, so the
    // comparison is exact rather than approximate — any error is an addressing error and not a filtering
    // one.
    const auto& body = Body();

    size_t compared = 0;
    for ( uint32_t z = 1; z < Assets::kCloudModellingVolumeDepth; z += 7 )
    {
        for ( uint32_t y = 1; y < Assets::kCloudModellingVolumeHeight; y += 5 )
        {
            for ( uint32_t x = 1; x < Assets::kCloudModellingVolumeWidth; x += 7 )
            {
                const glm::vec3 uvw( ( static_cast<float>( x ) + 0.5f ) /
                                          static_cast<float>( Assets::kCloudModellingVolumeWidth ),
                                     ( static_cast<float>( y ) + 0.5f ) /
                                          static_cast<float>( Assets::kCloudModellingVolumeHeight ),
                                     ( static_cast<float>( z ) + 0.5f ) /
                                          static_cast<float>( Assets::kCloudModellingVolumeDepth ) );

                const vec4   read  = CLOUD_SAMPLE_AUTHORED( CloudAuthoredClampUvw( uvw ) );
                const size_t index = VoxelIndex( x, y, z );

                ASSERT_NEAR( read.x, static_cast<float>( body.Voxels[index + 0] ) / 255.0f, 1e-5f )
                     << "profile at " << x << ", " << y << ", " << z;
                ASSERT_NEAR( read.y, static_cast<float>( body.Voxels[index + 1] ) / 255.0f, 1e-5f );
                ASSERT_NEAR( read.z, static_cast<float>( body.Voxels[index + 2] ) / 255.0f, 1e-5f );
                ASSERT_NEAR( read.w, static_cast<float>( body.Voxels[index + 3] ) / 255.0f, 1e-5f );
                ++compared;
            }
        }
    }

    EXPECT_GT( compared, 4000u );
}

TEST( CloudAuthored, TheGeneratorAndTheShaderAgreeAboutWhereNotOnlyAboutWhat )
{
    // THE HOLE THE TEST ABOVE HAD, found by breaking it: it compares the shader's addressing against the
    // test's own copy of the voxel layout, so TRANSPOSING THE GENERATOR'S WRITE ORDER left it green —
    // both sides permuted together and the comparison could not see it. A relation whose two sides are
    // the same statement is not a relation.
    //
    // The way to close it is through something neither side can permute: the SHAPE the recipe describes.
    // Every lump's centre is deep inside the body by construction, so reading the volume at each lump's
    // own local coordinate must find body there — and a transposed write order scatters those readings
    // across the volume, where the odds of landing inside a 10-per-cent-occupied body eight times running
    // are one in ten million.
    const Assets::CloudModellingVolumeRecipe& recipe = Body().Recipe;

    for ( size_t k = 0; k < recipe.Blobs.size(); ++k )
    {
        const glm::vec3 local = recipe.Blobs[k].CentreKm / recipe.SizeKm + glm::vec3( 0.5f );
        const vec4      voxel = CLOUD_SAMPLE_AUTHORED( CloudAuthoredClampUvw( local ) );

        EXPECT_GT( voxel.x, 0.0f ) << "lump " << k << " has no body at its own centre";
        EXPECT_GT( voxel.w, 0.0f ) << "lump " << k << " is not inside its own envelope";
    }

    // ... and the body is NOT symmetric about its middle in the vertical, which is what pins the axis
    // ORDER rather than merely the addressing within an axis: a cumulus has a flat base and a crown, so
    // the lower half of the box carries more of it than the upper half.
    size_t lower = 0;
    size_t upper = 0;
    for ( uint32_t z = 0; z < Assets::kCloudModellingVolumeDepth; ++z )
    {
        for ( uint32_t y = 0; y < Assets::kCloudModellingVolumeHeight; ++y )
        {
            for ( uint32_t x = 0; x < Assets::kCloudModellingVolumeWidth; ++x )
            {
                if ( Body().Voxels[VoxelIndex( x, y, z )] == 0u )
                    continue;
                ( y < Assets::kCloudModellingVolumeHeight / 2 ? lower : upper ) += 1;
            }
        }
    }

    EXPECT_GT( lower, upper * 3u / 2u )
         << "the body is nearly symmetric top to bottom, so this assertion cannot pin the axis order; "
            "lower "
         << lower << " upper " << upper;
}

TEST( CloudAuthored, TheBoundaryOfTheVolumeIsEmptyAndReadsAsItself )
{
    // THE ONE GUARANTEE THAT ACTUALLY CARRIES THE ADDRESSING, and it took breaking two other things to
    // find out that it is the one. `GenerateCloudModellingVolume` walks all six faces after the bake and
    // REFUSES a body that reaches any of them, and `ValidateCloudModellingRecipe` refuses the recipe
    // before it. So the outermost shell of every `.dcmv` is four zeroes.
    //
    // WHY THAT MATTERS MORE THAN IT LOOKS. Every sampler in this engine is REPEAT, and the seam pulls a
    // coordinate in by half a texel before it fetches. Both of those are second lines of defence: an
    // empty shell means a coordinate that lands on the boundary — by the clamp, by a wrap, or by a bounds
    // test that was skipped — reads zero and contributes nothing. Take the shell away and all three of
    // them become load-bearing at once.
    //
    // MEASURED, NOT ASSUMED: breaking the half-texel clamp to use the wrong axis extent leaves every test
    // in this suite green, and so does deleting the exact bounds test in the seam. Neither is redundant —
    // one bounds a cost and the other bounds a fetch — but neither is what keeps the picture right, and
    // saying that here is worth more than a test that pretends otherwise.
    const auto& body = Body();

    size_t nonEmpty = 0;
    for ( uint32_t z = 0; z < Assets::kCloudModellingVolumeDepth; ++z )
    {
        for ( uint32_t y = 0; y < Assets::kCloudModellingVolumeHeight; ++y )
        {
            for ( uint32_t x = 0; x < Assets::kCloudModellingVolumeWidth; ++x )
            {
                const bool onFace = ( x == 0 || x == Assets::kCloudModellingVolumeWidth - 1 || y == 0 ||
                                      y == Assets::kCloudModellingVolumeHeight - 1 || z == 0 ||
                                      z == Assets::kCloudModellingVolumeDepth - 1 );
                if ( !onFace )
                    continue;

                const size_t index = VoxelIndex( x, y, z );
                if ( body.Voxels[index + 0] != 0u || body.Voxels[index + 3] != 0u )
                    ++nonEmpty;
            }
        }
    }

    EXPECT_EQ( nonEmpty, 0u ) << "the body reaches its own box, so a coordinate on the boundary no longer "
                                 "reads as nothing and the REPEAT sampler becomes visible";

    // And the corner texels read back as themselves through the seam's own clamp, which is what says the
    // clamp does not move a legal coordinate off the texel it belongs to.
    for ( uint32_t z : { 0u, Assets::kCloudModellingVolumeDepth - 1u } )
    {
        for ( uint32_t y : { 0u, Assets::kCloudModellingVolumeHeight - 1u } )
        {
            for ( uint32_t x : { 0u, Assets::kCloudModellingVolumeWidth - 1u } )
            {
                const glm::vec3 uvw( ( static_cast<float>( x ) + 0.5f ) /
                                          static_cast<float>( Assets::kCloudModellingVolumeWidth ),
                                     ( static_cast<float>( y ) + 0.5f ) /
                                          static_cast<float>( Assets::kCloudModellingVolumeHeight ),
                                     ( static_cast<float>( z ) + 0.5f ) /
                                          static_cast<float>( Assets::kCloudModellingVolumeDepth ) );

                const vec4   read  = CLOUD_SAMPLE_AUTHORED( CloudAuthoredClampUvw( uvw ) );
                const size_t index = VoxelIndex( x, y, z );

                EXPECT_NEAR( read.x, static_cast<float>( body.Voxels[index + 0] ) / 255.0f, 1e-5f )
                     << "corner " << x << ", " << y << ", " << z;
                EXPECT_NEAR( read.w, static_cast<float>( body.Voxels[index + 3] ) / 255.0f, 1e-5f );
            }
        }
    }
}

TEST( CloudAuthored, TheInstanceMapsItsOwnCentreAndCorners )
{
    const Graphic::CloudAuthoredInstanceGpu gpu = MakeInstance( glm::vec3( 3.0f, 1.4f, -2.0f ) );

    ClearInstances();
    AddInstance( gpu );

    const CloudAuthoredInstance instance = CloudAuthoredInstanceAt( 0 );

    const vec3 centre = ( vec3( gpu.BoundsMin ) + vec3( gpu.BoundsMax ) ) * 0.5f;
    const vec3 middle = CloudAuthoredLocalUvw( instance, centre );

    EXPECT_NEAR( middle.x, 0.5f, 1e-4f );
    EXPECT_NEAR( middle.y, 0.5f, 1e-4f );
    EXPECT_NEAR( middle.z, 0.5f, 1e-4f );

    // An unrotated instance's bounds ARE its box, so its corners land exactly on 0 and 1 — which is the
    // statement that the packer's inverse and its bounds describe the same box.
    const vec3 low = CloudAuthoredLocalUvw( instance, vec3( gpu.BoundsMin ) );
    EXPECT_NEAR( low.x, 0.0f, 1e-4f );
    EXPECT_NEAR( low.y, 0.0f, 1e-4f );
    EXPECT_NEAR( low.z, 0.0f, 1e-4f );

    const vec3 high = CloudAuthoredLocalUvw( instance, vec3( gpu.BoundsMax ) );
    EXPECT_NEAR( high.x, 1.0f, 1e-4f );
    EXPECT_NEAR( high.y, 1.0f, 1e-4f );
    EXPECT_NEAR( high.z, 1.0f, 1e-4f );
}

TEST( CloudAuthored, BoundsContainTheRotatedBoxAndTheExactTestRejectsTheDifference )
{
    // A box rotated 45 degrees about Y has an axis-aligned hull whose horizontal extent is sqrt(2) times
    // its own. The cheap test lets those corners through and the exact one must not: every sampler in
    // this engine is REPEAT, so a coordinate outside [0, 1] would fetch the far side of the body.
    const float                             yaw = 0.7853981634f; // 45 degrees
    const Graphic::CloudAuthoredInstanceGpu gpu = MakeInstance( glm::vec3( 0.0f, 1.4f, 0.0f ), 1.0f, yaw );

    ClearInstances();
    AddInstance( gpu );
    const CloudAuthoredInstance instance = CloudAuthoredInstanceAt( 0 );

    const float halfX = 0.5f * ( gpu.BoundsMax.x - gpu.BoundsMin.x );
    EXPECT_NEAR( halfX, 0.5f * ( Body().Recipe.SizeKm.x + Body().Recipe.SizeKm.z ) * 0.70710678f, 1e-3f );

    // A corner of the axis-aligned hull: inside the cheap test, outside the body.
    const vec3 corner( gpu.BoundsMax.x - 1e-3f, 1.4f, gpu.BoundsMax.z - 1e-3f );
    EXPECT_TRUE( CloudAuthoredInBounds( instance, corner ) );
    EXPECT_FALSE( CloudAuthoredInVolume( CloudAuthoredLocalUvw( instance, corner ) ) );

    // ... and the centre is inside both.
    const vec3 centre( 0.0f, 1.4f, 0.0f );
    EXPECT_TRUE( CloudAuthoredInBounds( instance, centre ) );
    EXPECT_TRUE( CloudAuthoredInVolume( CloudAuthoredLocalUvw( instance, centre ) ) );

    // THE BOX MUST NEVER REJECT WHAT THE BODY WOULD ACCEPT, which is the one thing a conservative gate
    // owes and the one way it can be wrong. Swept over the instance's own box rather than spot-checked,
    // because the failure of a bound is at its corners.
    for ( int ix = 0; ix <= 8; ++ix )
    {
        for ( int iy = 0; iy <= 8; ++iy )
        {
            for ( int iz = 0; iz <= 8; ++iz )
            {
                const vec3 point( mix( gpu.BoundsMin.x, gpu.BoundsMax.x, static_cast<float>( ix ) / 8.0f ),
                                  mix( gpu.BoundsMin.y, gpu.BoundsMax.y, static_cast<float>( iy ) / 8.0f ),
                                  mix( gpu.BoundsMin.z, gpu.BoundsMax.z, static_cast<float>( iz ) / 8.0f ) );

                if ( CloudAuthoredInVolume( CloudAuthoredLocalUvw( instance, point ) ) )
                    ASSERT_TRUE( CloudAuthoredInBounds( instance, point ) )
                         << "the box rejected a point inside the body at " << ix << ", " << iy << ", " << iz;
            }
        }
    }

    // AND A POINT WELL OUTSIDE IS REJECTED BY THE CHEAP TEST, which is the whole of what it buys. Note
    // what this does NOT claim: removing the box entirely changes no answer, because the exact test that
    // follows rejects the same points. It is a COST gate, and its effect is in the slope in
    // Docs/Clouds/CALIBRATION.md section A0, not in a pixel.
    EXPECT_FALSE( CloudAuthoredInBounds( instance, vec3( gpu.BoundsMax.x + 1.0f, 1.4f, 0.0f ) ) );
    EXPECT_FALSE( CloudAuthoredInBounds( instance, vec3( 0.0f, gpu.BoundsMax.y + 1.0f, 0.0f ) ) );
    EXPECT_FALSE( CloudAuthoredInBounds( instance, vec3( 0.0f, 1.4f, gpu.BoundsMin.z - 1.0f ) ) );
}

TEST( CloudAuthored, APointInsideTheHullButOutsideTheRotatedBodyIsExactlyProcedural )
{
    // A corner of a rotated instance's axis-aligned hull is INSIDE the cheap box and OUTSIDE the body,
    // which is the one place in the sky where the second gate has anything to decide. The OUTCOME is what
    // is asserted: such a point contributes nothing.
    //
    // AND IT HOLDS FOR TWO INDEPENDENT REASONS, which was found by deleting the gate and watching this
    // stay green. The exact test rejects the point; and if it did not, the seam's half-texel clamp would
    // pull the coordinate onto the volume's boundary, which the bake guarantees is empty
    // (TheBoundaryOfTheVolumeIsEmptyAndReadsAsItself). The gate is therefore a COST bound — it saves a 3D
    // fetch for every point in the difference between a rotated box and its hull, which is up to 3.4x the
    // body's own volume — and not the thing that keeps the picture right. Both are worth having and only
    // one of them is worth calling a correctness test.
    CloudFieldParams params = DefaultParams();
    params.Coverage         = 0.85f;

    const float                             yaw = 0.7853981634f;
    const Graphic::CloudAuthoredInstanceGpu gpu = MakeInstance( glm::vec3( 0.0f, 1.4f, 0.0f ), 1.0f, yaw );

    const glm::vec3 corner( gpu.BoundsMax.x - 0.02f, 1.4f, gpu.BoundsMax.z - 0.02f );
    const float     fraction = corner.y / kLayerThicknessKm;

    ClearInstances();
    const CloudFieldSample bare = SampleCloudField( params, fraction, corner );

    ClearInstances();
    AddInstance( gpu );
    const CloudAuthoredInstance instance = CloudAuthoredInstanceAt( 0 );
    ASSERT_TRUE( CloudAuthoredInBounds( instance, corner ) ) << "the chosen corner is not inside the hull";
    ASSERT_FALSE( CloudAuthoredInVolume( CloudAuthoredLocalUvw( instance, corner ) ) )
         << "the chosen corner is inside the body, so it tests nothing";

    const CloudFieldSample with = SampleCloudField( params, fraction, corner );

    EXPECT_EQ( with.Profile, bare.Profile );
    EXPECT_EQ( with.DensityScale, bare.DensityScale );
}

TEST( CloudAuthored, ADegenerateTransformIsRefusedRatherThanInverted )
{
    Desert::ECS::HeroCloudData data;

    glm::mat4 flattened( 1.0f );
    flattened[1] = glm::vec4( 0.0f ); // scale 0 on Y

    const auto packed =
         Graphic::PackCloudAuthoredInstance( flattened, Body().Recipe.SizeKm, kLayerBottomKm, data );
    EXPECT_FALSE( packed.Valid );
}

// ---------------------------------------------------------------------------------------------------
// The seam
// ---------------------------------------------------------------------------------------------------

TEST( CloudAuthored, OutsideEveryInstanceTheAnswerIsBitForBitTheProceduralOne )
{
    // THE ZERO-COST CLAIM, in its testable half. A sky with a hero cloud somewhere else in it must render
    // exactly what it rendered without one — not nearly, exactly — because the union of a zero is the
    // identity and the cutout of a zero is the identity. Anything less would show as a whole sky moving
    // when one cloud was added to a corner of it.
    CloudFieldParams params = DefaultParams();

    ClearInstances();
    glm::vec3 point;
    ASSERT_TRUE( FindProceduralPoint( params, 0.45f, point ) );

    ClearInstances();
    const CloudFieldSample without = SampleCloudField( params, 0.45f, point );

    ClearInstances();
    AddInstance( MakeInstance( point + glm::vec3( 40.0f, 0.0f, 40.0f ) ) );
    const CloudFieldSample with = SampleCloudField( params, 0.45f, point );

    EXPECT_EQ( with.Profile, without.Profile );
    EXPECT_EQ( with.DetailType, without.DetailType );
    EXPECT_EQ( with.DensityScale, without.DensityScale );
    EXPECT_EQ( with.DetailFactor, without.DetailFactor );
    EXPECT_EQ( with.ExtinctionFactor, without.ExtinctionFactor );
}

TEST( CloudAuthored, TheUnionIsAMaxAndDoesNotDependOnTheOrDER )
{
    // Two bodies overlapping at one point, listed both ways round. `max` is commutative, and the winner's
    // material numbers travel with it — so the answer must be identical, field for field, whichever order
    // the scene happened to list its entities in.
    CloudFieldParams params = DefaultParams();

    const glm::vec3 centre( 0.0f, 0.5f, 0.0f );

    // The second is scaled down and given different material numbers, so "which one won" is visible in
    // the sample rather than having to be inferred.
    Graphic::CloudAuthoredInstanceGpu big   = MakeInstance( centre );
    Graphic::CloudAuthoredInstanceGpu small = MakeInstance( centre + glm::vec3( 0.35f, 0.0f, 0.0f ), 0.5f );
    small.Row0.w                            = 2.0f; // DetailFactor
    small.Row1.w                            = 3.0f; // DensityFactor
    small.Row2.w                            = 4.0f; // ExtinctionFactor

    size_t compared = 0;
    for ( int ix = -12; ix <= 12; ++ix )
    {
        for ( int iy = -6; iy <= 6; ++iy )
        {
            const glm::vec3 point( centre.x + static_cast<float>( ix ) * 0.06f,
                                   centre.y + static_cast<float>( iy ) * 0.05f, centre.z );
            const float     fraction = point.y / kLayerThicknessKm;

            ClearInstances();
            AddInstance( big );
            AddInstance( small );
            const CloudFieldSample forwards = SampleCloudField( params, fraction, point );

            ClearInstances();
            AddInstance( small );
            AddInstance( big );
            const CloudFieldSample backwards = SampleCloudField( params, fraction, point );

            ASSERT_EQ( forwards.Profile, backwards.Profile ) << "at " << ix << ", " << iy;
            ASSERT_EQ( forwards.DetailType, backwards.DetailType );
            ASSERT_EQ( forwards.DensityScale, backwards.DensityScale );
            ASSERT_EQ( forwards.DetailFactor, backwards.DetailFactor );
            ASSERT_EQ( forwards.ExtinctionFactor, backwards.ExtinctionFactor );

            // And it really is the MAX of the two, not one of them: each alone, then together.
            ClearInstances();
            AddInstance( big );
            const float onlyBig = SampleCloudField( params, fraction, point ).Profile;

            ClearInstances();
            AddInstance( small );
            const float onlySmall = SampleCloudField( params, fraction, point ).Profile;

            ASSERT_FLOAT_EQ( forwards.Profile, std::max( onlyBig, onlySmall ) );

            if ( forwards.Profile > 0.0f )
                ++compared;
        }
    }

    EXPECT_GT( compared, 20u ) << "the two bodies never overlapped the sampled points, so nothing was tested";
}

TEST( CloudAuthored, TheAuthoredWinnerTakesAllItsOwnMaterialNumbers )
{
    // A blend of two bodies' detail character is a third character that is neither, and the seam between
    // them is exactly where a floating average shows as a smear of the wrong kind of edge. So the winner
    // takes all four, and this is the assertion of it.
    CloudFieldParams params = DefaultParams();

    const glm::vec3 centre( 0.0f, 0.5f, 0.0f );

    Graphic::CloudAuthoredInstanceGpu solitary = MakeInstance( centre );
    solitary.Row0.w                            = 2.5f;
    solitary.Row1.w                            = 0.5f;
    solitary.Row2.w                            = 4.0f;

    ClearInstances();
    AddInstance( solitary );

    const CloudFieldSample sample = SampleCloudField( params, centre.y / kLayerThicknessKm, centre );
    ASSERT_GT( sample.Profile, 0.0f );

    EXPECT_FLOAT_EQ( sample.DetailFactor, 2.5f );
    EXPECT_FLOAT_EQ( sample.ExtinctionFactor, 4.0f );

    // The density is the LAYER's, the instance's factor and the volume's own per-voxel scale, in that
    // order — the same three-level composition the procedural producer performs.
    const CloudAuthoredInstance instance = CloudAuthoredInstanceAt( 0 );
    const vec4 voxel = CLOUD_SAMPLE_AUTHORED( CloudAuthoredClampUvw( CloudAuthoredLocalUvw( instance, centre ) ) );
    EXPECT_FLOAT_EQ( sample.DensityScale, params.DensityScale * 0.5f * voxel.z );
}

TEST( CloudAuthored, StrengthScalesTheProfile )
{
    CloudFieldParams params = DefaultParams();

    const glm::vec3 centre( 0.0f, 0.5f, 0.0f );
    const float     fraction = centre.y / kLayerThicknessKm;

    ClearInstances();
    AddInstance( MakeInstance( centre, 1.0f, 0.0f, 1.0f ) );
    const float full = SampleCloudField( params, fraction, centre ).Profile;

    ClearInstances();
    AddInstance( MakeInstance( centre, 1.0f, 0.0f, 0.4f ) );
    const float faded = SampleCloudField( params, fraction, centre ).Profile;

    ASSERT_GT( full, 0.0f );
    EXPECT_NEAR( faded, full * 0.4f, 1e-5f );

    // At zero the body is gone and the sky is the procedural one again — which is what makes Strength a
    // fade rather than a switch, and what a cutscene needs to bring a hero cloud in.
    ClearInstances();
    AddInstance( MakeInstance( centre, 1.0f, 0.0f, 0.0f, /*cutout=*/true ) );
    const CloudFieldSample gone = SampleCloudField( params, fraction, centre );

    ClearInstances();
    const CloudFieldSample bare = SampleCloudField( params, fraction, centre );

    EXPECT_EQ( gone.Profile, bare.Profile );
}

// ---------------------------------------------------------------------------------------------------
// The cutout
// ---------------------------------------------------------------------------------------------------

TEST( CloudAuthored, TheCutoutRemovesTheProceduralFieldInsideTheEnvelopeAndLeavesItOutside )
{
    // WITHOUT THIS, a procedural blob grows through a sculpted cloud and the composition turns to soup
    // (ANALYSIS_APPROACH.md section 4.3). The assertion has three parts and all three are needed: inside
    // the envelope the procedural field must be gone, the authored body must survive, and OUTSIDE the
    // instance nothing may move — a cutout that took the whole box would cut a rectangular hole in the
    // deck around the cloud.
    CloudFieldParams params = DefaultParams();
    params.Coverage         = 0.85f; // an overcast sky, so there is procedural field everywhere to remove

    constexpr float fraction = 0.45f;
    glm::vec3       centre;
    ASSERT_TRUE( FindProceduralPoint( params, fraction, centre ) );

    const Graphic::CloudAuthoredInstanceGpu open    = MakeInstance( centre, 1.0f, 0.0f, 1.0f, /*cutout=*/false );
    const Graphic::CloudAuthoredInstanceGpu cutting = MakeInstance( centre, 1.0f, 0.0f, 1.0f, /*cutout=*/true );

    glm::vec3 point;
    float     authored = 0.0f;
    ASSERT_TRUE( FindCutoutPoint( params, open, fraction, point, authored ) )
         << "no point where the procedural field wins inside the body's envelope, so this test would "
            "measure nothing";

    ClearInstances();
    const float bare = SampleCloudField( params, fraction, point ).Profile;

    // With the cutout OFF the union keeps the deeper of the two, and here that is the procedural field.
    ClearInstances();
    AddInstance( open );
    EXPECT_FLOAT_EQ( SampleCloudField( params, fraction, point ).Profile, bare );

    // With it ON the procedural field is gone and what remains is the authored body alone.
    ClearInstances();
    AddInstance( cutting );
    const CloudFieldSample cut = SampleCloudField( params, fraction, point );

    EXPECT_FLOAT_EQ( cut.Profile, authored );
    EXPECT_LT( cut.Profile, bare ) << "the cutout removed nothing";

    // ... and well outside the instance the procedural field is exactly what it was, bit for bit. This
    // is the half that fails if the cutout is applied over the box rather than over the body.
    const glm::vec3 elsewhere = centre + glm::vec3( 30.0f, 0.0f, 30.0f );

    ClearInstances();
    const CloudFieldSample bareElsewhere = SampleCloudField( params, fraction, elsewhere );

    ClearInstances();
    AddInstance( cutting );
    const CloudFieldSample cutElsewhere = SampleCloudField( params, fraction, elsewhere );

    EXPECT_EQ( cutElsewhere.Profile, bareElsewhere.Profile );
}

TEST( CloudAuthored, TheCutoutUnionsAcrossInstancesRatherThanFollowingTheWinner )
{
    // Two hero clouds standing in each other's boxes each suppress the procedural field over their own
    // body, and it has to be gone from BOTH: taking only the winner's cutout would let a blob grow
    // through the one that lost the profile by a hair.
    //
    // The arrangement is chosen so that the instance which CUTS is the one that LOSES: `deep` is the
    // same body scaled up, so at the test point it is further inside itself and wins the union, and it is
    // the one with the cutout switched OFF.
    CloudFieldParams params = DefaultParams();
    params.Coverage         = 0.85f;

    constexpr float fraction = 0.45f;
    glm::vec3       centre;
    ASSERT_TRUE( FindProceduralPoint( params, fraction, centre ) );

    const Graphic::CloudAuthoredInstanceGpu cutting = MakeInstance( centre, 1.0f, 0.0f, 1.0f, /*cutout=*/true );

    glm::vec3 point;
    float     authored = 0.0f;
    ASSERT_TRUE( FindCutoutPoint( params, cutting, fraction, point, authored ) );

    const Graphic::CloudAuthoredInstanceGpu deep = MakeInstance( centre, 1.35f, 0.0f, 1.0f, /*cutout=*/false );

    ClearInstances();
    const float bare = SampleCloudField( params, fraction, point ).Profile;

    ClearInstances();
    AddInstance( deep );
    const float onlyDeep = SampleCloudField( params, fraction, point ).Profile;
    ASSERT_GT( onlyDeep, authored ) << "the scaled body does not win the union here, so the test is not "
                                       "measuring what it says";
    ASSERT_FLOAT_EQ( onlyDeep, bare ) << "the procedural field was supposed to win against the pair alone";

    ClearInstances();
    AddInstance( deep );
    AddInstance( cutting );
    const float both = SampleCloudField( params, fraction, point ).Profile;

    EXPECT_LT( both, bare ) << "the cutout of the instance that LOST the union was ignored, so a "
                               "procedural blob is still growing through a hero cloud";

    // ... and the other order gives the same answer, because the cutout unions rather than sequences.
    ClearInstances();
    AddInstance( cutting );
    AddInstance( deep );
    EXPECT_FLOAT_EQ( SampleCloudField( params, fraction, point ).Profile, both );
}

// ---------------------------------------------------------------------------------------------------
// The relation the renderer warns about
// ---------------------------------------------------------------------------------------------------

TEST( CloudAuthored, FitsLayerIsTheRelationBetweenTheBodyAndTheShell )
{
    // The march only samples between the two shells, so a body outside them is not clipped by anything an
    // artist can see — it is simply never sampled, and the symptom is a cumulus with its crown sliced
    // flat by an altitude nobody set. Stated as a predicate so the renderer can name both numbers.
    const Graphic::CloudAuthoredInstanceGpu inside = MakeInstance( glm::vec3( 0.0f, 1.8f, 0.0f ) );
    EXPECT_TRUE( Graphic::CloudAuthoredInstanceFitsLayer( inside, kLayerThicknessKm ) );

    // Half a kilometre above the base with a body a kilometre tall: its bottom is below the shell.
    const Graphic::CloudAuthoredInstanceGpu low = MakeInstance( glm::vec3( 0.0f, 0.3f, 0.0f ) );
    EXPECT_FALSE( Graphic::CloudAuthoredInstanceFitsLayer( low, kLayerThicknessKm ) );

    const Graphic::CloudAuthoredInstanceGpu high = MakeInstance( glm::vec3( 0.0f, 3.4f, 0.0f ) );
    EXPECT_FALSE( Graphic::CloudAuthoredInstanceFitsLayer( high, kLayerThicknessKm ) );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
