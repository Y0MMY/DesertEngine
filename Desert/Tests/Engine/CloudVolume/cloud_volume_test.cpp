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

#include <Engine/Graphic/Clouds/CloudVolumeAtlasLayout.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeBake.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeFormat.hpp>

#include <gtest/gtest.h>

#include <cmath>
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
using Desert::Graphic::CloudVolumeAtlasLayout;
using Desert::Graphic::CloudVolumeAtlasLocal;
using Desert::Graphic::CloudVolumeAtlasTileCount;
using Desert::Graphic::CloudVolumeAtlasTileIndex;
using Desert::Graphic::CloudVolumeAtlasTileOrigin;
using Desert::Graphic::CloudVolumeAtlasTrilinearFootprint;
using Desert::Graphic::CloudVolumeAtlasUvw;
using Desert::Graphic::CloudVolumeAtlasWriteTile;
using Desert::Graphic::CloudVolumeChannelLayout;
using Desert::Graphic::CloudVolumeFileBytes;
using Desert::Graphic::CloudVolumeHeader;
using Desert::Graphic::CloudVolumePayloadBytes;
using Desert::Graphic::CloudVolumeShellMaxProfile;
using Desert::Graphic::CloudVolumeVoxelIndex;
using Desert::Graphic::DecodeSignedDistance;
using Desert::Graphic::EncodeSignedDistance;
using Desert::Graphic::kCloudVolumeChannels;
using Desert::Graphic::kCloudVolumeHeaderBytes;
using Desert::Graphic::kCloudVolumeMagic;
using Desert::Graphic::kCloudVolumeVersion;
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

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
