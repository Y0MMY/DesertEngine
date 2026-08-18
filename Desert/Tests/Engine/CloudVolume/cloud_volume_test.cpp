// The hero-cloud data path — the `.dvol` format, the analytic baker and the atlas tile arithmetic —
// tested without a GPU.
//
// Every assertion below is a RELATION between two things that must agree, not a spot value. That is the
// project's own defect taxonomy talking: a noise tile 35 km wide beside a layer 3.5 km thick, a shadow
// map marching the antipodal side of a planet, a fade end past a view distance. Each side was
// individually correct and a unit test of either passed. So:
//
//   * the smooth union agrees with the primitives it unions (bounded above by their minimum, below by
//     the blend radius it was given);
//   * the profile agrees with the surface the primitive defines (1 at the centre, 0 outside, monotone
//     in between);
//   * the baked field agrees with the analytic field it was baked from (the zero set within a voxel);
//   * the decoded distance agrees with — and never exceeds — the true distance;
//   * a read `.dvol` agrees with the written one, byte for byte and field for field;
//   * the atlas mapping agrees with its own inverse, and a tap agrees to stay inside its tile.
//
// The header under test is the SAME header the CloudVolumeBaker tool compiles, so a passing test is a
// statement about the volumes that get shipped rather than about a paraphrase of the maths.

#include "CloudVolumeShaderReference.hpp"

#include <Engine/Graphic/Clouds/CloudVolumeAtlasLayout.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeBake.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeFormat.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeInstance.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using Desert::Graphic::CloudBakeDescription;
using Desert::Graphic::CloudBakeEvaluate;
using Desert::Graphic::CloudBakePrimitive;
using Desert::Graphic::CloudBakePrimitiveKind;
using Desert::Graphic::CloudBakeProfile;
using Desert::Graphic::CloudBakeSdCapsule;
using Desert::Graphic::CloudBakeSdEllipsoid;
using Desert::Graphic::CloudBakeSdPrimitive;
using Desert::Graphic::CloudBakeSettings;
using Desert::Graphic::CloudBakeShape;
using Desert::Graphic::CloudBakeVoxelCenter;
using Desert::Graphic::CloudVolume;
using Desert::Graphic::CloudVolumeAtlasBytes;
using Desert::Graphic::CloudVolumeAtlasDimensions;
using Desert::Graphic::CloudVolumeAtlasFootprint;
using Desert::Graphic::CloudVolumeAtlasLayout;
using Desert::Graphic::CloudVolumeAtlasLocal;
using Desert::Graphic::CloudVolumeAtlasTileCount;
using Desert::Graphic::CloudVolumeAtlasTileIndex;
using Desert::Graphic::CloudVolumeAtlasTileOrigin;
using Desert::Graphic::CloudVolumeAtlasTrilinearFootprint;
using Desert::Graphic::CloudVolumeAtlasUvw;
using Desert::Graphic::CloudVolumeAtlasWriteTile;
using Desert::Graphic::CloudVolumeChannelLayout;
using Desert::Graphic::CloudVolumeFadeWeight;
using Desert::Graphic::CloudVolumeFileBytes;
using Desert::Graphic::CloudVolumeHeader;
using Desert::Graphic::CloudVolumeInstance;
using Desert::Graphic::CloudVolumePayloadBytes;
using Desert::Graphic::CloudVolumeShellMaxProfile;
using Desert::Graphic::CloudVolumeUpAxisRotation;
using Desert::Graphic::CloudVolumeVoxelIndex;
using Desert::Graphic::DecodeSignedDistance;
using Desert::Graphic::EncodeSignedDistance;
using Desert::Graphic::kCloudVolumeChannels;
using Desert::Graphic::kCloudVolumeHeaderBytes;
using Desert::Graphic::kCloudVolumeMagic;
using Desert::Graphic::kCloudVolumeVersion;
using Desert::Graphic::kMaxCloudVolumeInstances;
using Desert::Graphic::MakeCloudVolumeInstance;
using Desert::Graphic::ReadCloudVolume;
using Desert::Graphic::ValidateCloudVolumeHeader;
using Desert::Graphic::WriteCloudVolume;

namespace
{
    CloudBakePrimitive Sphere( float x, float y, float z, float radius, float detailType = 0.5f,
                               float densityScale = 1.0f )
    {
        CloudBakePrimitive p;
        p.Kind         = CloudBakePrimitiveKind::Ellipsoid;
        p.Center       = { x, y, z };
        p.Radii        = { radius, radius, radius };
        p.DetailType   = detailType;
        p.DensityScale = densityScale;
        return p;
    }

    CloudBakePrimitive Capsule( float x, float y, float z, float halfZ, float radius )
    {
        CloudBakePrimitive p;
        p.Kind     = CloudBakePrimitiveKind::Capsule;
        p.Center   = { x, y, z };
        p.HalfAxis = { 0.0f, 0.0f, halfZ };
        p.Radius   = radius;
        return p;
    }

    // A tiny but complete bake: a single sphere inside a box with room for the guard shell. Small enough
    // that a test can walk every one of its voxels.
    CloudBakeSettings SmallSettings()
    {
        CloudBakeSettings settings;
        settings.Width               = 32;
        settings.Height              = 32;
        settings.Depth               = 16;
        settings.Extent              = { 3200.0f, 3200.0f, 1600.0f }; // 100 world units per voxel
        settings.SignedDistanceRange = 1600.0f;
        return settings;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// The primitives themselves. The distance field is what everything else is derived from, so it is
// checked first and against the geometry rather than against remembered numbers.
// ---------------------------------------------------------------------------------------------------

TEST( CloudVolumePrimitives, ASphereIsTheExactDistanceToItsSurface )
{
    const glm::vec3 radii( 100.0f );

    // Exact everywhere for a sphere: |p| - r.
    for ( float t = 0.0f; t <= 300.0f; t += 7.0f )
    {
        const glm::vec3 p = glm::normalize( glm::vec3( 1.0f, 2.0f, -3.0f ) ) * t;
        EXPECT_NEAR( CloudBakeSdEllipsoid( p, radii ), t - 100.0f, 1e-3f ) << "at |p| = " << t;
    }
}

TEST( CloudVolumePrimitives, AnEllipsoidsZeroSetIsExactlyItsSurface )
{
    const glm::vec3 radii( 300.0f, 120.0f, 60.0f );

    // Every point with |p/r| == 1 is on the surface, in every direction — that is the one property of
    // the approximation that is exact, and it is the property the baked zero set inherits.
    const glm::vec3 directions[] = { glm::vec3( 1, 0, 0 ), glm::vec3( 0, 1, 0 ),  glm::vec3( 0, 0, 1 ),
                                     glm::vec3( 1, 1, 1 ), glm::vec3( -2, 1, 3 ), glm::vec3( 0.3f, -1, 0.7f ) };
    for ( const glm::vec3& d : directions )
    {
        const glm::vec3 unit = glm::normalize( d );
        // Scale so that |p/r| == 1.
        const float k = 1.0f / glm::length( glm::vec3( unit.x / radii.x, unit.y / radii.y, unit.z / radii.z ) );
        EXPECT_NEAR( CloudBakeSdEllipsoid( unit * k, radii ), 0.0f, 1e-2f )
             << "direction (" << d.x << "," << d.y << "," << d.z << ")";
    }
}

TEST( CloudVolumePrimitives, TheCentreOfAnEllipsoidIsTheDistanceToItsNearestSurface )
{
    // The approximation is 0/0 at the origin and its limit is direction-dependent, so the header pins
    // the answer to the true one: the shortest way out.
    EXPECT_FLOAT_EQ( CloudBakeSdEllipsoid( glm::vec3( 0.0f ), glm::vec3( 300.0f, 120.0f, 60.0f ) ), -60.0f );
}

TEST( CloudVolumePrimitives, ACapsuleIsATubeAroundItsSegmentAndDegeneratesToASphere )
{
    const glm::vec3 halfAxis( 0.0f, 0.0f, 100.0f );

    // Radially, anywhere along the segment, the distance is the radial distance minus the radius.
    EXPECT_NEAR( CloudBakeSdCapsule( glm::vec3( 30.0f, 0.0f, 0.0f ), halfAxis, 20.0f ), 10.0f, 1e-3f );
    EXPECT_NEAR( CloudBakeSdCapsule( glm::vec3( 30.0f, 0.0f, 50.0f ), halfAxis, 20.0f ), 10.0f, 1e-3f );

    // Past the cap, it is the distance to the segment END minus the radius — a hemisphere, not a plane.
    EXPECT_NEAR( CloudBakeSdCapsule( glm::vec3( 0.0f, 0.0f, 150.0f ), halfAxis, 20.0f ), 30.0f, 1e-3f );

    // A zero-length capsule is a legal shape: a sphere.
    EXPECT_NEAR( CloudBakeSdCapsule( glm::vec3( 50.0f, 0.0f, 0.0f ), glm::vec3( 0.0f ), 20.0f ), 30.0f, 1e-3f );
}

// ---------------------------------------------------------------------------------------------------
// The smooth union. Both bounds are asserted because they fail in opposite directions: an upper bound
// alone passes for a union that swallowed a primitive whole, a lower bound alone passes for one that
// barely blended at all.
// ---------------------------------------------------------------------------------------------------

TEST( CloudVolumeSmoothUnion, IsNeverGreaterThanEitherPrimitiveAndNeverBelowTheirMinimumByMoreThanTheBlend )
{
    CloudBakeShape shape;
    shape.Primitives  = { Sphere( -60.0f, 0.0f, 0.0f, 100.0f ), Sphere( 60.0f, 0.0f, 0.0f, 80.0f ) };
    shape.BlendRadius = 25.0f;

    for ( float x = -400.0f; x <= 400.0f; x += 3.0f )
    {
        for ( float z = -200.0f; z <= 200.0f; z += 7.0f )
        {
            const glm::vec3 p( x, 0.0f, z );

            const float a    = CloudBakeSdPrimitive( shape.Primitives[0], p );
            const float b    = CloudBakeSdPrimitive( shape.Primitives[1], p );
            const float smin = CloudBakeEvaluate( shape, p ).SignedDistance;

            EXPECT_LE( smin, a + 1e-3f ) << "at (" << x << ", " << z << ")";
            EXPECT_LE( smin, b + 1e-3f ) << "at (" << x << ", " << z << ")";
            EXPECT_GE( smin, std::fmin( a, b ) - shape.BlendRadius - 1e-3f ) << "at (" << x << ", " << z << ")";
        }
    }
}

TEST( CloudVolumeSmoothUnion, DoesNotDependOnTheOrderThePrimitivesWereAuthoredIn )
{
    // The reason the exponential form was chosen over the pairwise polynomial one. An author reordering
    // a list in the editor must not move a single voxel.
    CloudBakeShape forward;
    forward.Primitives  = { Sphere( -60.0f, 0.0f, 0.0f, 100.0f, 0.1f, 0.4f ),
                            Sphere( 60.0f, 0.0f, 0.0f, 80.0f, 0.9f, 1.0f ),
                            Sphere( 0.0f, 0.0f, 90.0f, 50.0f, 0.5f, 0.7f ) };
    forward.BlendRadius = 30.0f;

    CloudBakeShape reversed = forward;
    std::reverse( reversed.Primitives.begin(), reversed.Primitives.end() );

    for ( float x = -300.0f; x <= 300.0f; x += 11.0f )
    {
        const glm::vec3 p( x, 13.0f, 21.0f );
        const auto      f = CloudBakeEvaluate( forward, p );
        const auto      r = CloudBakeEvaluate( reversed, p );

        EXPECT_NEAR( f.SignedDistance, r.SignedDistance, 1e-3f ) << "at x = " << x;
        EXPECT_NEAR( f.DetailType, r.DetailType, 1e-5f ) << "at x = " << x;
        EXPECT_NEAR( f.DensityScale, r.DensityScale, 1e-5f ) << "at x = " << x;
    }
}

TEST( CloudVolumeSmoothUnion, AZeroBlendRadiusIsTheHardUnionOfTheNearestPrimitive )
{
    CloudBakeShape shape;
    shape.Primitives  = { Sphere( -60.0f, 0.0f, 0.0f, 100.0f, 0.2f, 0.4f ),
                          Sphere( 60.0f, 0.0f, 0.0f, 80.0f, 0.8f, 1.0f ) };
    shape.BlendRadius = 0.0f;

    for ( float x = -300.0f; x <= 300.0f; x += 5.0f )
    {
        const glm::vec3 p( x, 0.0f, 0.0f );
        const float     a = CloudBakeSdPrimitive( shape.Primitives[0], p );
        const float     b = CloudBakeSdPrimitive( shape.Primitives[1], p );

        const auto field = CloudBakeEvaluate( shape, p );
        EXPECT_FLOAT_EQ( field.SignedDistance, std::fmin( a, b ) ) << "at x = " << x;
        // The other two channels come from the SAME primitive that answered for the distance — the
        // failure this rules out is a silhouette from one lobe wearing another lobe's detail type.
        EXPECT_FLOAT_EQ( field.DetailType, a <= b ? 0.2f : 0.8f ) << "at x = " << x;
    }
}

TEST( CloudVolumeSmoothUnion, TheBlendedChannelsStayInsideTheRangeThePrimitivesAuthored )
{
    // A weighted mean cannot leave the interval its inputs span. If it ever did, a Detail Type above 1
    // would drive the erosion's lerp past the billow end and a Density Scale above 1 would brighten a
    // cloud nobody authored to be brighter.
    CloudBakeShape shape;
    shape.Primitives  = { Sphere( -60.0f, 0.0f, 0.0f, 100.0f, 0.15f, 0.4f ),
                          Sphere( 60.0f, 0.0f, 0.0f, 80.0f, 0.85f, 1.0f ),
                          Capsule( 0.0f, 0.0f, 0.0f, 120.0f, 40.0f ) };
    shape.BlendRadius = 30.0f;

    for ( float x = -400.0f; x <= 400.0f; x += 9.0f )
    {
        for ( float z = -300.0f; z <= 300.0f; z += 9.0f )
        {
            const auto field = CloudBakeEvaluate( shape, glm::vec3( x, 0.0f, z ) );
            EXPECT_GE( field.DetailType, 0.15f - 1e-5f );
            EXPECT_LE( field.DetailType, 0.85f + 1e-5f );
            EXPECT_GE( field.DensityScale, 0.4f - 1e-5f );
            EXPECT_LE( field.DensityScale, 1.0f + 1e-5f );
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// The Dimensional Profile. Deck pp. 84/98: 1 in the core, 0 at and outside the surface, and a gradient
// in every direction between the two — the shape of the field six separate formulas in the lighting and
// the erosion assume.
// ---------------------------------------------------------------------------------------------------

TEST( CloudVolumeProfile, IsOneAtAPrimitivesCentreAndZeroOutsideItsSurface )
{
    CloudBakeShape shape;
    shape.Primitives     = { Sphere( 0.0f, 0.0f, 0.0f, 100.0f ) };
    shape.BlendRadius    = 0.0f;
    shape.ProfileFalloff = 40.0f; // below the radius, so the core is reached

    const auto profileAt = [&shape]( const glm::vec3& p )
    { return CloudBakeProfile( CloudBakeEvaluate( shape, p ).SignedDistance, shape.ProfileFalloff ); };

    EXPECT_FLOAT_EQ( profileAt( glm::vec3( 0.0f ) ), 1.0f );

    // Exactly at the surface, and anywhere beyond it.
    EXPECT_FLOAT_EQ( profileAt( glm::vec3( 100.0f, 0.0f, 0.0f ) ), 0.0f );
    EXPECT_FLOAT_EQ( profileAt( glm::vec3( 0.0f, 140.0f, 0.0f ) ), 0.0f );
    EXPECT_FLOAT_EQ( profileAt( glm::vec3( 0.0f, 0.0f, -1000.0f ) ), 0.0f );
}

TEST( CloudVolumeProfile, RisesMonotonicallyAlongARayFromOutsideToTheCore )
{
    CloudBakeShape shape;
    shape.Primitives     = { Sphere( 0.0f, 0.0f, 0.0f, 100.0f ) };
    shape.BlendRadius    = 0.0f;
    shape.ProfileFalloff = 40.0f;

    const glm::vec3 direction = glm::normalize( glm::vec3( 1.0f, 2.0f, -0.5f ) );

    float previous = -1.0f;
    for ( float t = 250.0f; t >= 0.0f; t -= 1.0f )
    {
        const float profile =
             CloudBakeProfile( CloudBakeEvaluate( shape, direction * t ).SignedDistance, shape.ProfileFalloff );
        EXPECT_GE( profile, previous - 1e-5f ) << "the profile fell while moving toward the core, at t = " << t;
        previous = profile;
    }
    EXPECT_FLOAT_EQ( previous, 1.0f );
}

TEST( CloudVolumeProfile, AFalloffLargerThanThePrimitiveNeverReachesAFullCore )
{
    // The invariant the bake descriptions are authored against, asserted rather than left as advice: a
    // small lobe under a big falloff is genuinely not deep, and it must read that way.
    CloudBakeShape shape;
    shape.Primitives     = { Sphere( 0.0f, 0.0f, 0.0f, 30.0f ) };
    shape.BlendRadius    = 0.0f;
    shape.ProfileFalloff = 120.0f;

    const float centre =
         CloudBakeProfile( CloudBakeEvaluate( shape, glm::vec3( 0.0f ) ).SignedDistance, shape.ProfileFalloff );
    EXPECT_GT( centre, 0.0f );
    EXPECT_LT( centre, 1.0f );
    EXPECT_NEAR( centre, 30.0f / 120.0f, 1e-5f );
}

// ---------------------------------------------------------------------------------------------------
// The signed-distance channel. The reference names the failure on each side (p. 159: "Too low = extra
// steps. Too High = rendering artifacts"), so the encoding's one hard promise is that it never
// over-states a distance.
// ---------------------------------------------------------------------------------------------------

TEST( CloudVolumeDistanceEncoding, NeverDecodesToAMagnitudeLargerThanTheOneItWasGiven )
{
    constexpr float range = 25600.0f;

    for ( float d = -2.0f * range; d <= 2.0f * range; d += 37.0f )
    {
        const float decoded = DecodeSignedDistance( EncodeSignedDistance( d, range ), range );

        EXPECT_LE( std::fabs( decoded ), std::fabs( d ) + 1e-3f )
             << "encoding " << d << " produced the longer distance " << decoded;

        // And it never crosses zero: an outside point must not decode as inside, or the march would
        // shade empty air.
        if ( d > 0.0f )
            EXPECT_GE( decoded, 0.0f ) << "at d = " << d;
        if ( d < 0.0f )
            EXPECT_LE( decoded, 0.0f ) << "at d = " << d;
    }
}

TEST( CloudVolumeDistanceEncoding, IsExactlyZeroAtZeroAndSaturatesAtTheRange )
{
    constexpr float range = 1000.0f;

    EXPECT_EQ( EncodeSignedDistance( 0.0f, range ), 128 );
    EXPECT_FLOAT_EQ( DecodeSignedDistance( 128, range ), 0.0f );

    EXPECT_FLOAT_EQ( DecodeSignedDistance( EncodeSignedDistance( 10.0f * range, range ), range ), range );
    EXPECT_FLOAT_EQ( DecodeSignedDistance( EncodeSignedDistance( -10.0f * range, range ), range ), -range );
}

TEST( CloudVolumeDistanceEncoding, StaysWithinOneQuantisationStepOfTheTruth )
{
    // Under-estimating is safe but it is not free — it costs march steps. The error has to be bounded,
    // and the bound is the step the encoding advertises.
    constexpr float range = 25600.0f;
    constexpr float step  = range / 127.0f;

    for ( float d = -range; d <= range; d += 13.0f )
    {
        const float decoded = DecodeSignedDistance( EncodeSignedDistance( d, range ), range );
        EXPECT_LE( std::fabs( decoded - d ), step + 1e-3f ) << "at d = " << d;
    }
}

TEST( CloudVolumeDistanceEncoding, ADecoderNeverHandsBackADistanceOutsideTheRangeEvenForAByteItWouldNeverWrite )
{
    // Byte 0 is never produced by the encoder. A corrupted or hand-made file can still contain it, and
    // a distance longer than the range would become a march step past the cloud it was meant to find.
    constexpr float range = 1000.0f;
    for ( int byte = 0; byte <= 255; ++byte )
    {
        const float decoded = DecodeSignedDistance( static_cast<uint8_t>( byte ), range );
        EXPECT_GE( decoded, -range );
        EXPECT_LE( decoded, range );
    }
}

// ---------------------------------------------------------------------------------------------------
// The `.dvol` file.
// ---------------------------------------------------------------------------------------------------

TEST( CloudVolumeFormat, TheHeaderIsTheFortyBytesTheFormatPromises )
{
    // The static_asserts in the header already make a layout change a build error; this is the
    // documentation half — the numbers a reader of the format specification can check against.
    EXPECT_EQ( sizeof( CloudVolumeHeader ), 40u );
    EXPECT_EQ( kCloudVolumeHeaderBytes, 40u );

    CloudVolumeHeader header;
    EXPECT_EQ( header.Magic, kCloudVolumeMagic );
    EXPECT_EQ( header.Version, kCloudVolumeVersion );
    EXPECT_EQ( header.ChannelLayout, static_cast<uint32_t>( CloudVolumeChannelLayout::ProfileTypeScaleDistance ) );

    header.Width  = 128;
    header.Height = 128;
    header.Depth  = 64;
    EXPECT_EQ( CloudVolumePayloadBytes( header ), 128ull * 128ull * 64ull * 4ull );
    EXPECT_EQ( CloudVolumeFileBytes( header ), CloudVolumePayloadBytes( header ) + 40ull );
}

TEST( CloudVolumeFormat, AWriteReadRoundTripReproducesEveryHeaderFieldAndEveryVoxel )
{
    CloudBakeShape shape;
    shape.Primitives     = { Sphere( 100.0f, -50.0f, 0.0f, 600.0f, 0.2f, 0.6f ),
                             Capsule( -200.0f, 0.0f, 0.0f, 300.0f, 300.0f ) };
    shape.BlendRadius    = 80.0f;
    shape.ProfileFalloff = 200.0f;

    const auto baked = Desert::Graphic::BakeCloudVolume( shape, SmallSettings() );
    ASSERT_TRUE( baked.IsSuccess() ) << baked.GetError();

    const auto bytes = WriteCloudVolume( baked.GetValue() );
    ASSERT_TRUE( bytes.IsSuccess() ) << bytes.GetError();
    EXPECT_EQ( bytes.GetValue().size(), CloudVolumeFileBytes( baked.GetValue().Header ) );

    const auto read = ReadCloudVolume( bytes.GetValue().data(), bytes.GetValue().size() );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();

    const CloudVolumeHeader& a = baked.GetValue().Header;
    const CloudVolumeHeader& b = read.GetValue().Header;
    EXPECT_EQ( a.Magic, b.Magic );
    EXPECT_EQ( a.Version, b.Version );
    EXPECT_EQ( a.Width, b.Width );
    EXPECT_EQ( a.Height, b.Height );
    EXPECT_EQ( a.Depth, b.Depth );
    EXPECT_EQ( a.ChannelLayout, b.ChannelLayout );
    EXPECT_FLOAT_EQ( a.ExtentX, b.ExtentX );
    EXPECT_FLOAT_EQ( a.ExtentY, b.ExtentY );
    EXPECT_FLOAT_EQ( a.ExtentZ, b.ExtentZ );
    EXPECT_FLOAT_EQ( a.SignedDistanceRange, b.SignedDistanceRange );

    ASSERT_EQ( baked.GetValue().Voxels.size(), read.GetValue().Voxels.size() );
    EXPECT_EQ( baked.GetValue().Voxels, read.GetValue().Voxels );
}

TEST( CloudVolumeFormat, EveryWayAFileCanBeWrongIsRejectedWithItsOwnReason )
{
    // Data errors, so each one is a ResultStr carrying the numbers — never an exception, and never a
    // quiet default that turns a bad file into an empty sky nobody can explain.
    CloudVolumeHeader header;
    header.Width               = 4;
    header.Height              = 4;
    header.Depth               = 4;
    header.ExtentX             = 100.0f;
    header.ExtentY             = 100.0f;
    header.ExtentZ             = 100.0f;
    header.SignedDistanceRange = 50.0f;
    EXPECT_TRUE( ValidateCloudVolumeHeader( header ).IsSuccess() );

    const auto rejects = []( CloudVolumeHeader broken )
    {
        const auto result = ValidateCloudVolumeHeader( broken );
        EXPECT_FALSE( result.IsSuccess() );
        EXPECT_FALSE( result.GetError().empty() );
    };

    {
        CloudVolumeHeader h = header;
        h.Magic             = 0x12345678u;
        rejects( h );
    }
    {
        CloudVolumeHeader h = header;
        h.Version           = kCloudVolumeVersion + 1;
        rejects( h );
    }
    {
        CloudVolumeHeader h = header;
        h.ChannelLayout     = 99u;
        rejects( h );
    }
    {
        CloudVolumeHeader h = header;
        h.Depth             = 0;
        rejects( h );
    }
    {
        CloudVolumeHeader h = header;
        h.ExtentY           = 0.0f;
        rejects( h );
    }
    {
        CloudVolumeHeader h   = header;
        h.SignedDistanceRange = -1.0f;
        rejects( h );
    }

    // Truncation and a null buffer are file-level, not header-level.
    std::vector<unsigned char> tooShort( 8, 0 );
    EXPECT_FALSE( ReadCloudVolume( tooShort.data(), tooShort.size() ).IsSuccess() );
    EXPECT_FALSE( ReadCloudVolume( nullptr, 1024 ).IsSuccess() );

    // A header that describes more voxels than the file holds.
    CloudVolume volume;
    volume.Header = header;
    volume.Voxels.assign( static_cast<size_t>( CloudVolumePayloadBytes( header ) ), 0 );
    const auto bytes = WriteCloudVolume( volume );
    ASSERT_TRUE( bytes.IsSuccess() );
    std::vector<unsigned char> truncated = bytes.GetValue();
    truncated.pop_back();
    EXPECT_FALSE( ReadCloudVolume( truncated.data(), truncated.size() ).IsSuccess() );
}

// ---------------------------------------------------------------------------------------------------
// The bake itself: does the grid agree with the analytic field it came from?
// ---------------------------------------------------------------------------------------------------

TEST( CloudVolumeBake, TheBakedZeroSetMatchesTheAnalyticSurfaceToWithinAVoxel )
{
    // Radius 500 in a box whose SHORT axis is 1600 units half-extent 800: the shape has to clear the
    // guard shell on every axis, and the thin one is the one that catches you.
    CloudBakeShape shape;
    shape.Primitives     = { Sphere( 0.0f, 0.0f, 0.0f, 500.0f ) };
    shape.BlendRadius    = 0.0f;
    shape.ProfileFalloff = 300.0f;

    const CloudBakeSettings settings = SmallSettings();
    const auto              baked    = Desert::Graphic::BakeCloudVolume( shape, settings );
    ASSERT_TRUE( baked.IsSuccess() ) << baked.GetError();

    const CloudVolume& volume = baked.GetValue();
    const float        voxel  = settings.Extent[0] / static_cast<float>( settings.Width ); // 100 units

    for ( uint32_t z = 0; z < settings.Depth; ++z )
    {
        for ( uint32_t y = 0; y < settings.Height; ++y )
        {
            for ( uint32_t x = 0; x < settings.Width; ++x )
            {
                const glm::vec3 p        = CloudBakeVoxelCenter( settings, x, y, z );
                const float     analytic = CloudBakeEvaluate( shape, p ).SignedDistance;
                const size_t    at       = CloudVolumeVoxelIndex( volume.Header, x, y, z );
                const float     decoded =
                     DecodeSignedDistance( volume.Voxels[at + 3], volume.Header.SignedDistanceRange );
                const uint8_t profile = volume.Voxels[at + 0];

                // A voxel comfortably OUTSIDE the surface must read as empty, and one comfortably inside
                // must not. Voxels within a voxel's width of the surface are where the two definitions
                // are allowed to disagree, and are the only ones exempted.
                if ( analytic > voxel )
                {
                    EXPECT_EQ( profile, 0 ) << "an outside voxel carries density at (" << x << "," << y << "," << z
                                            << "), analytic distance " << analytic;
                    EXPECT_GE( decoded, 0.0f ) << "an outside voxel decodes as inside";
                }
                else if ( analytic < -voxel )
                {
                    EXPECT_GT( profile, 0 ) << "an inside voxel is empty at (" << x << "," << y << "," << z
                                            << "), analytic distance " << analytic;
                    EXPECT_LE( decoded, 0.0f ) << "an inside voxel decodes as outside";
                }
            }
        }
    }
}

TEST( CloudVolumeBake, TheProfileChannelIsTheQuantisedAnalyticProfile )
{
    CloudBakeShape shape;
    shape.Primitives     = { Sphere( 200.0f, -100.0f, 0.0f, 700.0f, 0.3f, 0.8f ),
                             Sphere( -400.0f, 0.0f, 200.0f, 400.0f, 0.9f, 0.5f ) };
    shape.BlendRadius    = 90.0f;
    shape.ProfileFalloff = 250.0f;

    const CloudBakeSettings settings = SmallSettings();
    const auto              baked    = Desert::Graphic::BakeCloudVolume( shape, settings );
    ASSERT_TRUE( baked.IsSuccess() ) << baked.GetError();

    const CloudVolume& volume = baked.GetValue();

    for ( uint32_t z = 1; z < settings.Depth; z += 3 )
    {
        for ( uint32_t y = 1; y < settings.Height; y += 5 )
        {
            for ( uint32_t x = 1; x < settings.Width; x += 5 )
            {
                const glm::vec3 p     = CloudBakeVoxelCenter( settings, x, y, z );
                const auto      field = CloudBakeEvaluate( shape, p );
                const size_t    at    = CloudVolumeVoxelIndex( volume.Header, x, y, z );

                const float expectedProfile = CloudBakeProfile( field.SignedDistance, shape.ProfileFalloff );
                EXPECT_NEAR( volume.Voxels[at + 0] / 255.0f, expectedProfile, 1.0f / 255.0f );
                EXPECT_NEAR( volume.Voxels[at + 1] / 255.0f, field.DetailType, 1.0f / 255.0f );
                EXPECT_NEAR( volume.Voxels[at + 2] / 255.0f, field.DensityScale, 1.0f / 255.0f );
            }
        }
    }
}

TEST( CloudVolumeBake, RefusesAShapeThatReachesTheBoundaryOfItsBox )
{
    // The guard band is CHECKED, not imposed. Forcing the shell to zero would hide the overflow behind a
    // one-voxel cliff — a cloud with a razor-straight face, and no message saying why.
    CloudBakeShape shape;
    shape.Primitives     = { Sphere( 0.0f, 0.0f, 0.0f, 5000.0f ) }; // far larger than the box
    shape.BlendRadius    = 0.0f;
    shape.ProfileFalloff = 200.0f;

    const auto baked = Desert::Graphic::BakeCloudVolume( shape, SmallSettings() );
    EXPECT_FALSE( baked.IsSuccess() );
    EXPECT_NE( baked.GetError().find( "boundary" ), std::string::npos ) << baked.GetError();
}

TEST( CloudVolumeBake, LeavesTheGuardShellEmptyOnEveryVolumeItAccepts )
{
    CloudBakeShape shape;
    shape.Primitives     = { Sphere( 0.0f, 0.0f, 0.0f, 400.0f ), Capsule( 300.0f, 0.0f, 0.0f, 200.0f, 250.0f ) };
    shape.BlendRadius    = 60.0f;
    shape.ProfileFalloff = 250.0f;

    const auto baked = Desert::Graphic::BakeCloudVolume( shape, SmallSettings() );
    ASSERT_TRUE( baked.IsSuccess() ) << baked.GetError();
    EXPECT_EQ( CloudVolumeShellMaxProfile( baked.GetValue() ), 0 );
}

TEST( CloudVolumeBake, RejectsTheSettingsThatCannotProduceAVolume )
{
    CloudBakeShape shape;
    shape.Primitives     = { Sphere( 0.0f, 0.0f, 0.0f, 100.0f ) };
    shape.ProfileFalloff = 50.0f;

    EXPECT_FALSE( Desert::Graphic::BakeCloudVolume( CloudBakeShape{}, SmallSettings() ).IsSuccess() );

    {
        CloudBakeSettings s = SmallSettings();
        s.Depth             = 2;
        EXPECT_FALSE( Desert::Graphic::BakeCloudVolume( shape, s ).IsSuccess() );
    }
    {
        CloudBakeSettings s = SmallSettings();
        s.Extent[1]         = 0.0f;
        EXPECT_FALSE( Desert::Graphic::BakeCloudVolume( shape, s ).IsSuccess() );
    }
    {
        CloudBakeSettings s   = SmallSettings();
        s.SignedDistanceRange = 0.0f;
        EXPECT_FALSE( Desert::Graphic::BakeCloudVolume( shape, s ).IsSuccess() );
    }
    {
        CloudBakeShape bad = shape;
        bad.ProfileFalloff = 0.0f;
        EXPECT_FALSE( Desert::Graphic::BakeCloudVolume( bad, SmallSettings() ).IsSuccess() );
    }
    {
        CloudBakeShape bad = shape;
        bad.BlendRadius    = -1.0f;
        EXPECT_FALSE( Desert::Graphic::BakeCloudVolume( bad, SmallSettings() ).IsSuccess() );
    }
}

TEST( CloudVolumeBake, VoxelCentresAreHalfAVoxelInFromTheGridCornersAndSpanTheWholeExtent )
{
    // Half a voxel out of step with the sampler is a mistake that shifts the whole cloud and looks like
    // nothing at all until two of them disagree.
    const CloudBakeSettings settings = SmallSettings();
    const float             voxel    = settings.Extent[0] / static_cast<float>( settings.Width );

    const glm::vec3 first = CloudBakeVoxelCenter( settings, 0, 0, 0 );
    EXPECT_NEAR( first.x, -settings.Extent[0] * 0.5f + voxel * 0.5f, 1e-3f );

    const glm::vec3 last =
         CloudBakeVoxelCenter( settings, settings.Width - 1, settings.Height - 1, settings.Depth - 1 );
    EXPECT_NEAR( last.x, settings.Extent[0] * 0.5f - voxel * 0.5f, 1e-3f );

    // And the centre voxel pair straddles the origin.
    const glm::vec3 mid =
         CloudBakeVoxelCenter( settings, settings.Width / 2, settings.Height / 2, settings.Depth / 2 );
    EXPECT_NEAR( mid.x, voxel * 0.5f, 1e-3f );
}

// ---------------------------------------------------------------------------------------------------
// The atlas. Every 3D sampler this engine creates is LINEAR/REPEAT and asserted to be, so the clamp
// that keeps a hero cloud inside its own tile is OUR arithmetic — which is exactly why it is tested
// rather than trusted.
// ---------------------------------------------------------------------------------------------------

TEST( CloudVolumeAtlas, TheShippedGeometryIsEightTilesOfThirtyTwoMebibytes )
{
    const CloudVolumeAtlasLayout layout;

    EXPECT_EQ( CloudVolumeAtlasTileCount( layout ), 8u );
    EXPECT_EQ( CloudVolumeAtlasDimensions( layout ), glm::uvec3( 512u, 256u, 64u ) );
    EXPECT_EQ( CloudVolumeAtlasBytes( layout ), 32ull * 1024ull * 1024ull );

    // And a tile is exactly the bake default, or a baked volume could not be placed at all.
    const CloudBakeSettings defaults;
    EXPECT_EQ( layout.TileWidth, defaults.Width );
    EXPECT_EQ( layout.TileHeight, defaults.Height );
    EXPECT_EQ( layout.TileDepth, defaults.Depth );
}

TEST( CloudVolumeAtlas, TileOriginAndTileIndexAreExactInverses )
{
    const CloudVolumeAtlasLayout layouts[] = {
         CloudVolumeAtlasLayout{},
         CloudVolumeAtlasLayout{ 8, 4, 2, 3, 5, 2 }, // deliberately asymmetric and prime-ish
         CloudVolumeAtlasLayout{ 16, 16, 16, 1, 1, 1 },
    };

    for ( const CloudVolumeAtlasLayout& layout : layouts )
    {
        for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
        {
            const glm::uvec3 origin = CloudVolumeAtlasTileOrigin( layout, tile );
            EXPECT_EQ( CloudVolumeAtlasTileIndex( layout, origin ), tile );

            // And the tile fits inside the atlas it claims to be part of.
            const glm::uvec3 dims = CloudVolumeAtlasDimensions( layout );
            EXPECT_LE( origin.x + layout.TileWidth, dims.x );
            EXPECT_LE( origin.y + layout.TileHeight, dims.y );
            EXPECT_LE( origin.z + layout.TileDepth, dims.z );
        }
    }
}

TEST( CloudVolumeAtlas, NoTwoTilesOverlap )
{
    const CloudVolumeAtlasLayout layout;
    const glm::uvec3             dims = CloudVolumeAtlasDimensions( layout );

    std::vector<uint32_t> owner( static_cast<size_t>( dims.x ) * dims.y * dims.z, 0xFFFFFFFFu );

    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        const glm::uvec3 origin = CloudVolumeAtlasTileOrigin( layout, tile );
        for ( uint32_t z = 0; z < layout.TileDepth; ++z )
            for ( uint32_t y = 0; y < layout.TileHeight; ++y )
                for ( uint32_t x = 0; x < layout.TileWidth; ++x )
                {
                    const size_t at =
                         ( static_cast<size_t>( origin.z + z ) * dims.y + ( origin.y + y ) ) * dims.x +
                         ( origin.x + x );
                    ASSERT_EQ( owner[at], 0xFFFFFFFFu )
                         << "tiles " << owner[at] << " and " << tile << " share a voxel";
                    owner[at] = tile;
                }
    }
}

TEST( CloudVolumeAtlas, TheUvwMappingIsTheExactInverseOfItselfInsideTheClampBand )
{
    const CloudVolumeAtlasLayout layout;

    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        // Inside [0.5/dim, 1 - 0.5/dim] on every axis the clamp does not move the point, so the round
        // trip must be exact. Outside it the forward map is many-to-one on purpose.
        for ( float t = 0.02f; t <= 0.98f; t += 0.04f )
        {
            const glm::vec3 local( t, 1.0f - t, 0.5f + 0.4f * std::sin( t * 6.0f ) );
            const glm::vec3 back =
                 CloudVolumeAtlasLocal( layout, tile, CloudVolumeAtlasUvw( layout, tile, local ) );

            EXPECT_NEAR( back.x, local.x, 1e-4f ) << "tile " << tile << " at t = " << t;
            EXPECT_NEAR( back.y, local.y, 1e-4f ) << "tile " << tile << " at t = " << t;
            EXPECT_NEAR( back.z, local.z, 1e-4f ) << "tile " << tile << " at t = " << t;
        }
    }
}

TEST( CloudVolumeAtlas, ATrilinearTapCanNeverReachANeighbouringTile )
{
    // THE property this arithmetic exists for. `local` is swept far outside [0,1] because that is where
    // a march actually spends most of its samples — outside the instance box — and the REPEAT sampler
    // would happily wrap such a coordinate into another hero cloud.
    const CloudVolumeAtlasLayout layout;

    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        const glm::uvec3 origin = CloudVolumeAtlasTileOrigin( layout, tile );

        for ( float t = -3.0f; t <= 4.0f; t += 0.013f )
        {
            const glm::vec3 local( t, -t, 1.0f - t );
            const auto      footprint =
                 CloudVolumeAtlasTrilinearFootprint( layout, CloudVolumeAtlasUvw( layout, tile, local ) );

            EXPECT_GE( footprint.Min.x, static_cast<int32_t>( origin.x ) ) << "tile " << tile << " t " << t;
            EXPECT_GE( footprint.Min.y, static_cast<int32_t>( origin.y ) ) << "tile " << tile << " t " << t;
            EXPECT_GE( footprint.Min.z, static_cast<int32_t>( origin.z ) ) << "tile " << tile << " t " << t;

            EXPECT_LE( footprint.Max.x, static_cast<int32_t>( origin.x + layout.TileWidth ) - 1 )
                 << "tile " << tile << " t " << t;
            EXPECT_LE( footprint.Max.y, static_cast<int32_t>( origin.y + layout.TileHeight ) - 1 )
                 << "tile " << tile << " t " << t;
            EXPECT_LE( footprint.Max.z, static_cast<int32_t>( origin.z + layout.TileDepth ) - 1 )
                 << "tile " << tile << " t " << t;
        }
    }
}

TEST( CloudVolumeAtlas, WritingATilePlacesEveryVoxelWhereTheMappingSaysItIs )
{
    // A small layout so the test can compare every voxel of every tile rather than a sample of them.
    const CloudVolumeAtlasLayout layout{ 4, 4, 2, 2, 2, 1 };

    CloudBakeSettings settings;
    settings.Width               = layout.TileWidth;
    settings.Height              = layout.TileHeight;
    settings.Depth               = layout.TileDepth;
    settings.Extent              = { 400.0f, 400.0f, 200.0f };
    settings.SignedDistanceRange = 200.0f;

    // Distinct content per tile, so a misplaced copy is visible rather than accidentally equal.
    std::vector<CloudVolume> volumes;
    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        CloudVolume volume;
        volume.Header.Width               = settings.Width;
        volume.Header.Height              = settings.Height;
        volume.Header.Depth               = settings.Depth;
        volume.Header.ExtentX             = settings.Extent[0];
        volume.Header.ExtentY             = settings.Extent[1];
        volume.Header.ExtentZ             = settings.Extent[2];
        volume.Header.SignedDistanceRange = settings.SignedDistanceRange;
        volume.Voxels.resize( static_cast<size_t>( CloudVolumePayloadBytes( volume.Header ) ) );

        for ( uint32_t z = 0; z < settings.Depth; ++z )
            for ( uint32_t y = 0; y < settings.Height; ++y )
                for ( uint32_t x = 0; x < settings.Width; ++x )
                {
                    const size_t at       = CloudVolumeVoxelIndex( volume.Header, x, y, z );
                    volume.Voxels[at + 0] = static_cast<unsigned char>( tile * 16u + 1u );
                    volume.Voxels[at + 1] = static_cast<unsigned char>( x );
                    volume.Voxels[at + 2] = static_cast<unsigned char>( y );
                    volume.Voxels[at + 3] = static_cast<unsigned char>( z );
                }
        volumes.push_back( std::move( volume ) );
    }

    std::vector<unsigned char> atlas( static_cast<size_t>( CloudVolumeAtlasBytes( layout ) ), 0 );
    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        const auto written = CloudVolumeAtlasWriteTile( layout, tile, volumes[tile], atlas );
        ASSERT_TRUE( written.IsSuccess() ) << written.GetError();
    }

    const glm::uvec3 dims = CloudVolumeAtlasDimensions( layout );
    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        const glm::uvec3 origin = CloudVolumeAtlasTileOrigin( layout, tile );
        for ( uint32_t z = 0; z < layout.TileDepth; ++z )
            for ( uint32_t y = 0; y < layout.TileHeight; ++y )
                for ( uint32_t x = 0; x < layout.TileWidth; ++x )
                {
                    const size_t at =
                         ( ( static_cast<size_t>( origin.z + z ) * dims.y + ( origin.y + y ) ) * dims.x +
                           ( origin.x + x ) ) *
                         kCloudVolumeChannels;

                    EXPECT_EQ( atlas[at + 0], static_cast<unsigned char>( tile * 16u + 1u ) );
                    EXPECT_EQ( atlas[at + 1], static_cast<unsigned char>( x ) );
                    EXPECT_EQ( atlas[at + 2], static_cast<unsigned char>( y ) );
                    EXPECT_EQ( atlas[at + 3], static_cast<unsigned char>( z ) );
                }
    }
}

TEST( CloudVolumeAtlas, RefusesAVolumeThatIsNotTheTileSize )
{
    // The one mismatch that stops a hero cloud rendering, and the message has to say so — the atlas
    // geometry is fixed (teamlead Q2) and the volume is what has to change.
    const CloudVolumeAtlasLayout layout;

    CloudBakeSettings settings = SmallSettings(); // 32x32x16, not the 128x128x64 a tile is
    CloudBakeShape    shape;
    shape.Primitives     = { Sphere( 0.0f, 0.0f, 0.0f, 500.0f ) };
    shape.ProfileFalloff = 200.0f;

    const auto baked = Desert::Graphic::BakeCloudVolume( shape, settings );
    ASSERT_TRUE( baked.IsSuccess() ) << baked.GetError();

    std::vector<unsigned char> atlas( static_cast<size_t>( CloudVolumeAtlasBytes( layout ) ), 0 );
    const auto                 written = CloudVolumeAtlasWriteTile( layout, 0, baked.GetValue(), atlas );
    EXPECT_FALSE( written.IsSuccess() );
    EXPECT_NE( written.GetError().find( "128x128x64" ), std::string::npos ) << written.GetError();

    // And a tile index past the end, and an atlas buffer of the wrong size.
    std::vector<unsigned char> wrongSize( 16, 0 );
    EXPECT_FALSE( CloudVolumeAtlasWriteTile( layout, 99, baked.GetValue(), atlas ).IsSuccess() );
    EXPECT_FALSE( CloudVolumeAtlasWriteTile( layout, 0, baked.GetValue(), wrongSize ).IsSuccess() );
}

// ---------------------------------------------------------------------------------------------------
// The default settings and the atlas have to agree about more than the tile size, and the description
// aggregate has to survive being default-constructed — it is what a `.cloudshape.json` is read into.
// ---------------------------------------------------------------------------------------------------

TEST( CloudVolumeBakeDefaults, TheDefaultBakeIsEightMetreVoxelsOnEveryAxis )
{
    // Deck parity (p. 82) and the design doc's hero-cumulus row: 1024 x 1024 x 512 m over 128x128x64.
    const CloudBakeSettings settings;

    EXPECT_FLOAT_EQ( settings.Extent[0] / static_cast<float>( settings.Width ), 800.0f );
    EXPECT_FLOAT_EQ( settings.Extent[1] / static_cast<float>( settings.Height ), 800.0f );
    EXPECT_FLOAT_EQ( settings.Extent[2] / static_cast<float>( settings.Depth ), 800.0f );

    // And the distance quantisation is finer than a voxel, so the encoding is not what limits accuracy.
    EXPECT_LT( settings.SignedDistanceRange / 127.0f, 800.0f );

    const CloudBakeDescription description;
    EXPECT_TRUE( description.Shape.Primitives.empty() );
    EXPECT_FLOAT_EQ( description.Settings.Extent[0], settings.Extent[0] );
}

// ---------------------------------------------------------------------------------------------------
// THE SHADER'S OWN COPY OF THE TILE ARITHMETIC.
//
// Common/CloudVolumeAtlas.glslh is compiled AS C++ here (see CloudVolumeShaderReference.hpp) and checked
// against CloudVolumeAtlasLayout.hpp. Two independent implementations of one mapping is precisely the
// shape of every expensive defect this project has had: each side individually correct, a unit test of
// either passing, and the sky wrong. So the assertion is the AGREEMENT, over a sample dense enough to
// include every place the two clamps bite.
// ---------------------------------------------------------------------------------------------------

namespace
{
    namespace ShaderRef = Desert::Tests::CloudVolumeShaderRef;

    glm::vec3 ShaderUvw( const CloudVolumeAtlasLayout& layout, uint32_t tileIndex, const glm::vec3& local )
    {
        const glm::vec3 dims( static_cast<float>( layout.TileWidth ), static_cast<float>( layout.TileHeight ),
                              static_cast<float>( layout.TileDepth ) );
        const glm::vec3 counts( static_cast<float>( layout.TilesX ), static_cast<float>( layout.TilesY ),
                                static_cast<float>( layout.TilesZ ) );
        return ShaderRef::CloudVolumeAtlasUvwOf( dims, counts, static_cast<float>( tileIndex ), local );
    }
} // namespace

TEST( CloudVolumeShaderMirror, TheShippedGeometryConstantsAreTheOnesTheLayoutDefaultsTo )
{
    const CloudVolumeAtlasLayout layout;

    EXPECT_FLOAT_EQ( ShaderRef::CLOUD_VOLUME_TILE_DIMS.x, static_cast<float>( layout.TileWidth ) );
    EXPECT_FLOAT_EQ( ShaderRef::CLOUD_VOLUME_TILE_DIMS.y, static_cast<float>( layout.TileHeight ) );
    EXPECT_FLOAT_EQ( ShaderRef::CLOUD_VOLUME_TILE_DIMS.z, static_cast<float>( layout.TileDepth ) );

    EXPECT_FLOAT_EQ( ShaderRef::CLOUD_VOLUME_TILE_COUNT.x, static_cast<float>( layout.TilesX ) );
    EXPECT_FLOAT_EQ( ShaderRef::CLOUD_VOLUME_TILE_COUNT.y, static_cast<float>( layout.TilesY ) );
    EXPECT_FLOAT_EQ( ShaderRef::CLOUD_VOLUME_TILE_COUNT.z, static_cast<float>( layout.TilesZ ) );
}

TEST( CloudVolumeShaderMirror, TheShaderPlacesEveryTileWhereTheCppSaysItIs )
{
    const CloudVolumeAtlasLayout layout;

    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        const glm::uvec3 expected = CloudVolumeAtlasTileOrigin( layout, tile );
        const glm::vec3  actual   = ShaderRef::CloudVolumeAtlasTileOriginOf(
             glm::vec3( static_cast<float>( layout.TileWidth ), static_cast<float>( layout.TileHeight ),
                           static_cast<float>( layout.TileDepth ) ),
             glm::vec3( static_cast<float>( layout.TilesX ), static_cast<float>( layout.TilesY ),
                           static_cast<float>( layout.TilesZ ) ),
             static_cast<float>( tile ) );

        EXPECT_FLOAT_EQ( actual.x, static_cast<float>( expected.x ) ) << "tile " << tile;
        EXPECT_FLOAT_EQ( actual.y, static_cast<float>( expected.y ) ) << "tile " << tile;
        EXPECT_FLOAT_EQ( actual.z, static_cast<float>( expected.z ) ) << "tile " << tile;
    }
}

TEST( CloudVolumeShaderMirror, TheShaderUvwAgreesWithTheCppOneEverywhereIncludingWellOutsideTheBox )
{
    const CloudVolumeAtlasLayout layout;

    // Deliberately reaches far outside [0,1] on every axis: a ray steps outside an instance's box on
    // most of its samples, and the two clamps are what the whole mapping rests on. If the shader had
    // "simplified away" the second clamp the disagreement would show first at the 0 and 1 rows.
    const float samples[] = { -12.5f, -1.0f, -0.001f,     0.0f,      1e-5f, 0.00390625f, 0.25f,
                              0.5f,   0.75f, 0.99609375f, 0.999999f, 1.0f,  1.001f,      9.0f };

    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        for ( const float x : samples )
        {
            for ( const float y : samples )
            {
                for ( const float z : samples )
                {
                    const glm::vec3 local( x, y, z );
                    const glm::vec3 expected = CloudVolumeAtlasUvw( layout, tile, local );
                    const glm::vec3 actual   = ShaderUvw( layout, tile, local );

                    ASSERT_FLOAT_EQ( actual.x, expected.x )
                         << "tile " << tile << " local " << x << "," << y << "," << z;
                    ASSERT_FLOAT_EQ( actual.y, expected.y );
                    ASSERT_FLOAT_EQ( actual.z, expected.z );
                }
            }
        }
    }
}

TEST( CloudVolumeShaderMirror, TheShaderUvwAgreesOnAsymmetricGeometriesToo )
{
    // The constants above are the shipped ones; these are not. Driving the layout through the arguments
    // is what stops the agreement being an accident of 128/128/64 and 4/2/1 — a tile count of one on an
    // axis, an odd tile size, and a depth of tiles all exercise the divide/mod chain differently.
    const CloudVolumeAtlasLayout layouts[] = {
         { .TileWidth = 3, .TileHeight = 5, .TileDepth = 7, .TilesX = 1, .TilesY = 1, .TilesZ = 1 },
         { .TileWidth = 16, .TileHeight = 8, .TileDepth = 4, .TilesX = 2, .TilesY = 3, .TilesZ = 2 },
         { .TileWidth = 64, .TileHeight = 64, .TileDepth = 64, .TilesX = 1, .TilesY = 1, .TilesZ = 8 },
    };

    for ( const auto& layout : layouts )
    {
        for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
        {
            for ( float t = -0.5f; t <= 1.5f; t += 0.0625f )
            {
                const glm::vec3 local( t, 1.0f - t, 0.5f * t );
                const glm::vec3 expected = CloudVolumeAtlasUvw( layout, tile, local );
                const glm::vec3 actual   = ShaderUvw( layout, tile, local );

                ASSERT_FLOAT_EQ( actual.x, expected.x );
                ASSERT_FLOAT_EQ( actual.y, expected.y );
                ASSERT_FLOAT_EQ( actual.z, expected.z );
            }
        }
    }
}

TEST( CloudVolumeShaderMirror, TheBoxTestIsExactlyTheUnitCube )
{
    EXPECT_TRUE( ShaderRef::CloudVolumeInsideBox( glm::vec3( 0.0f, 0.0f, 0.0f ) ) );
    EXPECT_TRUE( ShaderRef::CloudVolumeInsideBox( glm::vec3( 1.0f, 1.0f, 1.0f ) ) );
    EXPECT_TRUE( ShaderRef::CloudVolumeInsideBox( glm::vec3( 0.5f, 0.25f, 0.75f ) ) );

    // One axis outside is outside, on either side, on each axis.
    for ( int axis = 0; axis < 3; ++axis )
    {
        glm::vec3 below( 0.5f );
        glm::vec3 above( 0.5f );
        below[axis] = -1e-4f;
        above[axis] = 1.0f + 1e-4f;

        EXPECT_FALSE( ShaderRef::CloudVolumeInsideBox( below ) ) << "axis " << axis;
        EXPECT_FALSE( ShaderRef::CloudVolumeInsideBox( above ) ) << "axis " << axis;
    }
}

// ---------------------------------------------------------------------------------------------------
// THE INSTANCE TRANSFORM — and specifically the thing phase 1a's handover called the single most likely
// thing to get silently wrong: the volume's LOCAL Z IS UP while the engine is Y-up.
//
// A wrong axis here does not crash, does not warn, and does not even look empty: it renders a hero cloud
// lying on its side, which reads as "the bake is odd" rather than as "the transform is wrong". So it is
// asserted as a RELATION between world up and the volume's own third axis, in several independent ways.
// ---------------------------------------------------------------------------------------------------

namespace
{
    CloudVolumeHeader TestVolumeHeader( float ex = 102400.0f, float ey = 102400.0f, float ez = 51200.0f )
    {
        CloudVolumeHeader header;
        header.Width               = 128;
        header.Height              = 128;
        header.Depth               = 64;
        header.ExtentX             = ex;
        header.ExtentY             = ey;
        header.ExtentZ             = ez;
        header.SignedDistanceRange = 25600.0f;
        return header;
    }

    glm::vec3 ToLocal( const CloudVolumeInstance& instance, const glm::vec3& world )
    {
        return glm::vec3( instance.WorldToLocal * glm::vec4( world, 1.0f ) );
    }
} // namespace

TEST( CloudVolumeInstanceTransform, WorldUpBecomesLocalPlusZ )
{
    // THE assertion of this file. Half the baked box's Z extent above the centre must land exactly at the
    // top of the local box, and half below it at the bottom — which is only true if world +Y maps to
    // local +Z, and is false for every other axis assignment including the ones that "look" fine.
    const CloudVolumeHeader header = TestVolumeHeader();
    const glm::vec3         centre( 1000.0f, 250000.0f, -4000.0f );

    glm::mat4 world( 1.0f );
    world[3] = glm::vec4( centre, 1.0f );

    const CloudVolumeInstance instance = MakeCloudVolumeInstance( world, header, 0, 1.0f, 0.0f );

    const glm::vec3 atCentre = ToLocal( instance, centre );
    EXPECT_NEAR( atCentre.x, 0.5f, 1e-5f );
    EXPECT_NEAR( atCentre.y, 0.5f, 1e-5f );
    EXPECT_NEAR( atCentre.z, 0.5f, 1e-5f );

    const glm::vec3 atTop = ToLocal( instance, centre + glm::vec3( 0.0f, 0.5f * header.ExtentZ, 0.0f ) );
    EXPECT_NEAR( atTop.x, 0.5f, 1e-5f );
    EXPECT_NEAR( atTop.y, 0.5f, 1e-5f );
    EXPECT_NEAR( atTop.z, 1.0f, 1e-5f );

    const glm::vec3 atBottom = ToLocal( instance, centre - glm::vec3( 0.0f, 0.5f * header.ExtentZ, 0.0f ) );
    EXPECT_NEAR( atBottom.z, 0.0f, 1e-5f );

    // ...and the local height is MONOTONE in world altitude, which is the property that makes
    // fields.Local.z usable as "where in the cloud am I vertically" the way the procedural path uses its
    // per-cell height. A spot value could pass with an inverted axis; this cannot.
    float previous = -1.0f;
    for ( float up = -0.5f * header.ExtentZ; up <= 0.5f * header.ExtentZ; up += 0.05f * header.ExtentZ )
    {
        const float z = ToLocal( instance, centre + glm::vec3( 0.0f, up, 0.0f ) ).z;
        EXPECT_GT( z, previous );
        previous = z;
    }
}

TEST( CloudVolumeInstanceTransform, TheTurnIsAProperRotationAndNotAMirror )
{
    // A mirror would map the axes just as convincingly and hand every authored silhouette back
    // left-right reversed — invisible on a blob, obvious on a sculpted cloud, and impossible to spot in
    // a diff. Determinant +1 is the whole statement.
    const glm::mat4 rotation = CloudVolumeUpAxisRotation();

    EXPECT_NEAR( glm::determinant( glm::mat3( rotation ) ), 1.0f, 1e-6f );

    const glm::vec3 up = glm::vec3( rotation * glm::vec4( 0.0f, 1.0f, 0.0f, 0.0f ) );
    EXPECT_NEAR( up.x, 0.0f, 1e-6f );
    EXPECT_NEAR( up.y, 0.0f, 1e-6f );
    EXPECT_NEAR( up.z, 1.0f, 1e-6f );

    // Orthonormal: it must not scale, or the extent divide that follows would be wrong by that factor.
    const glm::mat3 linear = glm::mat3( rotation );
    EXPECT_NEAR( glm::length( linear[0] ), 1.0f, 1e-6f );
    EXPECT_NEAR( glm::length( linear[1] ), 1.0f, 1e-6f );
    EXPECT_NEAR( glm::length( linear[2] ), 1.0f, 1e-6f );
}

TEST( CloudVolumeInstanceTransform, TheHeaderExtentIsWhatSizesTheBoxAndTheTransformScaleMultipliesIt )
{
    // The component deliberately has no extent field (teamlead Q2): the `.dvol` says how big it was baked
    // and the entity's scale says how big it is here. So a scale of 2 must put the box corner twice as
    // far out, and an anisotropic header must be respected axis by axis.
    const CloudVolumeHeader header = TestVolumeHeader( 40000.0f, 80000.0f, 20000.0f );

    glm::mat4 world( 1.0f );
    world[0][0] = 2.0f;
    world[1][1] = 2.0f;
    world[2][2] = 2.0f;

    const CloudVolumeInstance instance = MakeCloudVolumeInstance( world, header, 0, 1.0f, 0.0f );

    // Half the SCALED extent on each world axis reaches the box face. World X is local X, world Y is
    // local Z, world Z is local -Y — so the extent that bounds a world axis is the one on the local axis
    // it maps to.
    EXPECT_NEAR( ToLocal( instance, glm::vec3( header.ExtentX, 0.0f, 0.0f ) ).x, 1.0f, 1e-5f );
    EXPECT_NEAR( ToLocal( instance, glm::vec3( 0.0f, header.ExtentZ, 0.0f ) ).z, 1.0f, 1e-5f );
    EXPECT_NEAR( ToLocal( instance, glm::vec3( 0.0f, 0.0f, -header.ExtentY ) ).y, 1.0f, 1e-5f );

    // And the box test agrees with the extent: just inside is inside, just outside is outside.
    EXPECT_TRUE(
         ShaderRef::CloudVolumeInsideBox( ToLocal( instance, glm::vec3( 0.0f, 0.99f * header.ExtentZ, 0.0f ) ) ) );
    EXPECT_FALSE(
         ShaderRef::CloudVolumeInsideBox( ToLocal( instance, glm::vec3( 0.0f, 1.01f * header.ExtentZ, 0.0f ) ) ) );
}

TEST( CloudVolumeInstanceTransform, ARotatedInstanceTurnsTheVolumeWithIt )
{
    // A quarter turn about world Y must move the volume's own X axis onto world -Z (or +Z, depending on
    // the sense) — the point being that the local coordinate FOLLOWS the entity rather than staying
    // world-axis-aligned, which is what makes "turn the cloud so its flank faces the sun" work.
    const CloudVolumeHeader header = TestVolumeHeader();

    const glm::mat4 world = glm::rotate( glm::mat4( 1.0f ), glm::radians( 90.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) );

    const CloudVolumeInstance instance = MakeCloudVolumeInstance( world, header, 0, 1.0f, 0.0f );

    // A point half an extent along world -Z sits on the volume's own +X face after the turn.
    const glm::vec3 probe = ToLocal( instance, glm::vec3( 0.0f, 0.0f, -0.5f * header.ExtentX ) );
    EXPECT_NEAR( probe.x, 1.0f, 1e-5f );
    EXPECT_NEAR( probe.y, 0.5f, 1e-5f );
    EXPECT_NEAR( probe.z, 0.5f, 1e-5f );

    // Up is still up, whatever the yaw: the turn about Y cannot touch the volume's vertical axis.
    EXPECT_NEAR( ToLocal( instance, glm::vec3( 0.0f, 0.5f * header.ExtentZ, 0.0f ) ).z, 1.0f, 1e-5f );
}

TEST( CloudVolumeInstanceTransform, TheParamsCarryTheTileAndTheTwoPerInstanceDials )
{
    const CloudVolumeInstance instance =
         MakeCloudVolumeInstance( glm::mat4( 1.0f ), TestVolumeHeader(), 6, 1.75f, -0.4f );

    EXPECT_FLOAT_EQ( instance.Params.x, 6.0f );
    EXPECT_FLOAT_EQ( instance.Params.y, 1.75f );
    EXPECT_FLOAT_EQ( instance.Params.z, -0.4f );

    // Every tile index the atlas can hand out survives the float round trip exactly — the record is read
    // as vec4s, so the index travels as a float and an inexact one would sample a neighbouring tile.
    const CloudVolumeAtlasLayout layout;
    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        const CloudVolumeInstance record =
             MakeCloudVolumeInstance( glm::mat4( 1.0f ), TestVolumeHeader(), tile, 1.75f, -0.4f );
        EXPECT_EQ( static_cast<uint32_t>( record.Params.x ), tile );
    }

    // A negative Density Scale would invert the channel it multiplies; it is repaired at the boundary
    // rather than left for the shader's clamp to hide.
    EXPECT_FLOAT_EQ( MakeCloudVolumeInstance( glm::mat4( 1.0f ), TestVolumeHeader(), 0, -2.0f, 0.0f ).Params.y,
                     0.0f );
}

TEST( CloudVolumeInstanceTransform, TheGpuRecordIsTheEightyBytesTheShaderIndexesWith )
{
    // The GLSL half is `struct CloudVolumeInstanceData` in Common/CloudDensityVoxel.glslh. std430 gives
    // an array of these an 80-byte stride; a C++ record of any other size would make every element after
    // the first straddle it, which reads as hero clouds sampling each other's transforms.
    EXPECT_EQ( sizeof( CloudVolumeInstance ), 80u );
    EXPECT_EQ( offsetof( CloudVolumeInstance, WorldToLocal ), 0u );
    EXPECT_EQ( offsetof( CloudVolumeInstance, Params ), 64u );

    // The instance budget IS the atlas tile count — eight distinct shapes resident at once. Two numbers
    // that must agree, so the agreement is asserted rather than left in two headers.
    EXPECT_EQ( kMaxCloudVolumeInstances, CloudVolumeAtlasTileCount( CloudVolumeAtlasLayout{} ) );
}

TEST( CloudVolumeInstanceTransform, EveryTileOfTheShippedAtlasIsReachableThroughTheWholeChain )
{
    // End to end: place the same volume in eight different tiles, sample the box centre through the
    // transform and then through the shader's UVW, and require each answer to land inside its OWN tile.
    // This is the property the whole hero-cloud path exists to keep — one cloud never reading another —
    // and it is the only test here that exercises the transform and the atlas mapping together.
    const CloudVolumeAtlasLayout layout;
    const CloudVolumeHeader      header = TestVolumeHeader();
    const glm::uvec3             atlas  = CloudVolumeAtlasDimensions( layout );

    for ( uint32_t tile = 0; tile < CloudVolumeAtlasTileCount( layout ); ++tile )
    {
        const CloudVolumeInstance instance =
             MakeCloudVolumeInstance( glm::mat4( 1.0f ), header, tile, 1.0f, 0.0f );

        const glm::uvec3 origin = CloudVolumeAtlasTileOrigin( layout, tile );

        for ( float x = -0.6f; x <= 0.6f; x += 0.1f )
        {
            for ( float y = -0.6f; y <= 0.6f; y += 0.1f )
            {
                const glm::vec3 world( x * header.ExtentX, y * header.ExtentZ, 0.0f );
                const glm::vec3 local = ToLocal( instance, world );
                const glm::vec3 uvw   = ShaderUvw( layout, tile, local );

                const CloudVolumeAtlasFootprint footprint = CloudVolumeAtlasTrilinearFootprint( layout, uvw );

                EXPECT_GE( footprint.Min.x, static_cast<int32_t>( origin.x ) );
                EXPECT_GE( footprint.Min.y, static_cast<int32_t>( origin.y ) );
                EXPECT_GE( footprint.Min.z, static_cast<int32_t>( origin.z ) );
                EXPECT_LT( footprint.Max.x, static_cast<int32_t>( origin.x + layout.TileWidth ) );
                EXPECT_LT( footprint.Max.y, static_cast<int32_t>( origin.y + layout.TileHeight ) );
                EXPECT_LT( footprint.Max.z, static_cast<int32_t>( origin.z + layout.TileDepth ) );

                EXPECT_LT( footprint.Max.x, static_cast<int32_t>( atlas.x ) );
                EXPECT_LT( footprint.Max.y, static_cast<int32_t>( atlas.y ) );
                EXPECT_LT( footprint.Max.z, static_cast<int32_t>( atlas.z ) );
            }
        }
    }
}

// ---- The distance fade (phase 2) ------------------------------------------------------------------
//
// The hand-back to the procedural deck. Phase 1b measured the limit it exists for: at distance a hero
// reads SMOOTHER than the deck around it, because a hero is one analytic body the erosion nibbles while
// a deck cloud is many weather cells intrinsically shredded. The fix is not a finer or coarser volume —
// it is to stop drawing the hero and let the deck, which is already behind it under the union, carry
// the frame.

TEST( CloudVolumeFade, FullInsideTheStartGoneAtTheEndAndMonotoneBetween )
{
    constexpr float kStart = 2000000.0f; // 20 km
    constexpr float kEnd   = 3000000.0f; // 30 km

    // Exactly 1 and exactly 0, not "about": a near hero must be bit for bit the cloud it was before this
    // parameter existed, and a faded one must contribute NOTHING rather than a residue that keeps its
    // instance record alive and its box test on every sample.
    EXPECT_FLOAT_EQ( CloudVolumeFadeWeight( 0.0f, kStart, kEnd ), 1.0f );
    EXPECT_FLOAT_EQ( CloudVolumeFadeWeight( kStart, kStart, kEnd ), 1.0f );
    EXPECT_FLOAT_EQ( CloudVolumeFadeWeight( kEnd, kStart, kEnd ), 0.0f );
    EXPECT_FLOAT_EQ( CloudVolumeFadeWeight( kEnd * 10.0f, kStart, kEnd ), 0.0f );
    EXPECT_FLOAT_EQ( CloudVolumeFadeWeight( 0.5f * ( kStart + kEnd ), kStart, kEnd ), 0.5f );

    float previous = 1.0f;
    for ( int i = 0; i <= 200; ++i )
    {
        const float distance = kEnd * 1.5f * static_cast<float>( i ) / 200.0f;
        const float weight   = CloudVolumeFadeWeight( distance, kStart, kEnd );

        EXPECT_GE( weight, 0.0f ) << "distance " << distance;
        EXPECT_LE( weight, 1.0f ) << "distance " << distance;
        // Monotone, so a camera flying toward a hero cloud can only ever gain it. A ramp that turned
        // round would draw a ring of denser cloud at whatever distance it turned.
        EXPECT_LE( weight, previous + 1e-6f ) << "distance " << distance;
        previous = weight;
    }
}

TEST( CloudVolumeFade, AnInvertedOrDegenerateRangeIsAHardCutAndNeverAnInvertedRamp )
{
    // start == end is a legitimate authored "pop off exactly here".
    EXPECT_FLOAT_EQ( CloudVolumeFadeWeight( 999999.0f, 1000000.0f, 1000000.0f ), 1.0f );
    EXPECT_FLOAT_EQ( CloudVolumeFadeWeight( 1000000.0f, 1000000.0f, 1000000.0f ), 0.0f );

    // start > end must NOT invert the ramp. Unguarded, `1 - (d - start)/(end - start)` with a negative
    // denominator makes a hero VISIBLE only beyond `start` and invisible near the camera — the exact
    // opposite of what the two fields say, from a pair an artist can produce with one drag.
    for ( float d = 0.0f; d <= 4000000.0f; d += 100000.0f )
    {
        const float weight = CloudVolumeFadeWeight( d, 3000000.0f, 2000000.0f );
        EXPECT_FLOAT_EQ( weight, d >= 2000000.0f ? 0.0f : 1.0f ) << "distance " << d;
    }
}

TEST( CloudVolumeFade, TheWeightReachesTheShaderThroughTheDensityScaleAndNowhereElse )
{
    // The fade is folded into the instance record's density multiplier by the renderer, so the shader
    // learns nothing about it and Common/CloudDensityVoxel.glslh needed no change at all. Pinned here
    // because the alternative — a distance term in the fine tier — is the two-tier disagreement the
    // horizon fringe was made of (the seam's cheap tier takes no distance argument).
    const CloudVolumeHeader header = TestVolumeHeader();

    const CloudVolumeInstance full = MakeCloudVolumeInstance( glm::mat4( 1.0f ), header, 0, 1.6f * 1.0f, 0.25f );
    const CloudVolumeInstance half = MakeCloudVolumeInstance( glm::mat4( 1.0f ), header, 0, 1.6f * 0.5f, 0.25f );

    EXPECT_FLOAT_EQ( full.Params.y, 1.6f );
    EXPECT_FLOAT_EQ( half.Params.y, 0.8f );

    // Everything else about the record is untouched by the fade: same tile, same detail bias, same
    // transform. A fade that moved the cloud would be a very strange bug to chase.
    EXPECT_FLOAT_EQ( full.Params.x, half.Params.x );
    EXPECT_FLOAT_EQ( full.Params.z, half.Params.z );
    EXPECT_EQ( full.WorldToLocal, half.WorldToLocal );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
