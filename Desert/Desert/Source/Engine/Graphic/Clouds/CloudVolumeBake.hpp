#pragma once

#include <Engine/Graphic/Clouds/CloudVolumeFormat.hpp>

#include <Common/Core/ResultStr.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// The hero-cloud baker: analytic signed-distance primitives in, a `.dvol` out.
//
// WHY PRIMITIVES AND NOT A SIMULATION (VOXEL_CLOUD_PATH.md §5(b), ranked first). The reference deck
// proves on p. 81 that a baked volume does not have to look like a cloud: the same data rendered
// without the runtime noise is "smooth, blobby melted plastic", and with it a fully detailed cumulus.
// ALL the cloud character comes from the erosion the engine already ships. What the baked volume owes
// the renderer is a silhouette and a smooth interior gradient — and a smooth union of ellipsoids and
// capsules supplies both exactly, with no distance transform, no simulation and no external tool.
//
// THE PROFILE *IS* THE DISTANCE FIELD. The Dimensional Profile the deck describes (pp. 84/98 — 1 in
// the core, 0 at and outside the surface) is `saturate(-d / falloff)` over the smooth-min field. That
// is the entire conversion, and it is why this path needs neither a Euclidean distance transform nor
// a separate "make it look inflated" step.
//
// EVERY TYPE BELOW IS A PLAIN AGGREGATE of arithmetic types, `std::array` and `std::vector`. That is
// deliberate: it is exactly what `rfl::json` reads and writes, so the authored shape description IS
// this struct — there is no second, parallel "file version" of it to drift. The header itself pulls in
// no reflection library, so a test or a tool can compile the maths without one.
//
// UNITS: world units, i.e. CENTIMETRES, everywhere, matching `.dvol` and the rest of the engine.

namespace Desert::Graphic
{
    // ---- Primitives -------------------------------------------------------------------------------

    // `int32_t` rather than the `uint8_t` the ECS components use: reflect-cpp reads and writes this enum
    // by NAME, and its enumerator scan compares the underlying type's limits against a signed range
    // bound, so an unsigned underlying type does not compile. The description file is the only thing
    // that stores this value, so nothing else pays for the four bytes.
    enum class CloudBakePrimitiveKind : int32_t
    {
        // Three semi-axes. The workhorse: a puff, a squashed anvil head, a stretched lobe.
        Ellipsoid = 0,
        // A tube of constant radius around a segment. What makes a leaning tower a tower rather than a
        // stack of separate balls — one capsule carries the whole trunk.
        Capsule = 1,
    };

    // A lobe of a cloud, in the volume's LOCAL space: the origin is the centre of the baked box and the
    // axes are the box's own, so a description is independent of where the entity ends up.
    //
    // `Kind` selects which of the geometry fields are read. The unread ones are ignored rather than
    // forbidden, because an author flipping a lobe from Ellipsoid to Capsule and back should not lose
    // the numbers they typed.
    struct CloudBakePrimitive
    {
        CloudBakePrimitiveKind Kind = CloudBakePrimitiveKind::Ellipsoid;

        std::array<float, 3> Center{ 0.0f, 0.0f, 0.0f };

        // Ellipsoid: the three semi-axes. Unused by Capsule.
        std::array<float, 3> Radii{ 1.0f, 1.0f, 1.0f };

        // Capsule: half the core segment — the tube runs from Center-HalfAxis to Center+HalfAxis.
        // Unused by Ellipsoid. A zero HalfAxis degenerates a capsule to a sphere of `Radius`, which is
        // a legal shape rather than an error.
        std::array<float, 3> HalfAxis{ 0.0f, 0.0f, 0.0f };

        // Capsule: the tube radius. Unused by Ellipsoid.
        float Radius = 1.0f;

        // This lobe's contribution to the G channel — 0 = wispy, 1 = billowy (deck p. 108). Authored per
        // lobe, not derived from height, because the deck's own rule for it is physical rather than
        // geometric (p. 92: evaporating regions get wisps, condensing regions get billows) and only the
        // person building the shape knows which lobe is which.
        float DetailType = 0.5f;

        // This lobe's contribution to the B channel — a linear density multiplier (deck p. 118).
        float DensityScale = 1.0f;
    };

    // A whole cloud: the lobes plus the two scalars that turn their union into a profile.
    struct CloudBakeShape
    {
        std::vector<CloudBakePrimitive> Primitives;

        // The smooth-minimum softening `k`, in world units. Zero is a hard union with visible creases
        // where lobes meet; a few metres is what makes two ellipsoids read as one cloud.
        float BlendRadius = 0.0f;

        // The depth, in world units, over which the profile ramps from 0 at the surface to 1. This is
        // the "soft grey falloff" of the deck's p. 84 cross-section. Larger = a softer, more diffuse
        // cloud; it must stay below the smallest lobe's radius or that lobe never reaches a full core.
        float ProfileFalloff = 1.0f;
    };

    // ---- The distance field -----------------------------------------------------------------------

    inline glm::vec3 CloudBakeToVec3( const std::array<float, 3>& v )
    {
        return glm::vec3( v[0], v[1], v[2] );
    }

    // Inigo Quilez's ellipsoid bound. The exact distance to an ellipsoid has no closed form, so this is
    // the standard approximation — and the property that matters here is exact: its ZERO SET is exactly
    // the ellipsoid surface, because `k0 == 1` means `|p/r| == 1`. It is also the exact distance for a
    // sphere and along each principal axis; elsewhere it under-estimates the magnitude, which is the
    // safe direction for a field that will be used as a step lower bound.
    //
    // At the centre both terms vanish and the ratio is 0/0. The limit is direction-dependent (it is
    // -r.x along x and -r.y along y), so there is no continuous answer; this returns the true distance
    // from the centre to the surface, `-min(r)`. Deep interior saturates to a full profile either way.
    inline float CloudBakeSdEllipsoid( const glm::vec3& p, const glm::vec3& radii )
    {
        const float smallest = glm::min( radii.x, glm::min( radii.y, radii.z ) );

        const glm::vec3 overR  = glm::vec3( p.x / radii.x, p.y / radii.y, p.z / radii.z );
        const glm::vec3 overR2 = glm::vec3( overR.x / radii.x, overR.y / radii.y, overR.z / radii.z );

        const float k0 = glm::length( overR );
        const float k1 = glm::length( overR2 );

        if ( !( k1 > 0.0f ) )
            return -smallest;

        return k0 * ( k0 - 1.0f ) / k1;
    }

    // Exact distance to a capsule: the distance to the core segment, minus the tube radius.
    inline float CloudBakeSdCapsule( const glm::vec3& p, const glm::vec3& halfAxis, float radius )
    {
        const glm::vec3 ba = halfAxis * 2.0f; // the full segment, from the -halfAxis end
        const glm::vec3 pa = p + halfAxis;    // p relative to that end
        const float     bb = glm::dot( ba, ba );

        if ( !( bb > 0.0f ) )
            return glm::length( p ) - radius; // a degenerate capsule is a sphere

        const float h = glm::clamp( glm::dot( pa, ba ) / bb, 0.0f, 1.0f );
        return glm::length( pa - ba * h ) - radius;
    }

    inline float CloudBakeSdPrimitive( const CloudBakePrimitive& primitive, const glm::vec3& p )
    {
        const glm::vec3 local = p - CloudBakeToVec3( primitive.Center );

        if ( primitive.Kind == CloudBakePrimitiveKind::Capsule )
            return CloudBakeSdCapsule( local, CloudBakeToVec3( primitive.HalfAxis ), primitive.Radius );

        return CloudBakeSdEllipsoid( local, CloudBakeToVec3( primitive.Radii ) );
    }

    // ---- The smooth union -------------------------------------------------------------------------
    //
    // The EXPONENTIAL smooth minimum, `-k * log( sum exp(-d_i / k) )`, chosen over Quilez's more common
    // quadratic polynomial form for two reasons that both matter to a BAKER rather than to a shader:
    //
    //   1. It is exactly commutative and associative over any number of primitives, so the order the
    //      lobes appear in an authored file cannot change a single voxel. The polynomial form is
    //      pairwise and its result depends on the fold order — a property nobody would ever guess from
    //      the editor UI, and a bug report nobody would ever reproduce.
    //   2. Its gradient weights are the softmax of the distances, which is exactly the blend the OTHER
    //      two channels need. Detail Type and Density Scale are authored per lobe and must cross over
    //      wherever the shapes cross over; taking those weights from the same function that made the
    //      silhouette means the three channels can never disagree about where one lobe ends.
    //
    // BOUNDS, which the tests assert. Writing `m = min(d_i)`:
    //
    //      smin <= m                       every term of the sum is positive, so the log is >= 0
    //      smin >= m - k * log(n)          every term is at most 1, so the sum is at most n
    //
    // For the two-primitive case that is `m - 0.693 k`, comfortably inside the blend radius. Note the
    // consequence for n > 2: the union UNDER-estimates distance by up to `k log n` even far from any
    // seam. That is the safe direction for a conservative step bound (§2.3: under-estimating costs
    // steps, over-estimating leaks through surfaces), and it is why the value is not corrected.
    //
    // The sum is evaluated relative to the minimum so every exponent is <= 0. Nothing can overflow, and
    // a term that underflows to zero was a lobe too far away to contribute anyway.

    struct CloudBakeField
    {
        float SignedDistance = 0.0f;
        float DetailType     = 0.0f;
        float DensityScale   = 0.0f;
    };

    inline CloudBakeField CloudBakeEvaluate( const CloudBakeShape& shape, const glm::vec3& p )
    {
        CloudBakeField field;
        if ( shape.Primitives.empty() )
            return field;

        size_t nearest          = 0;
        float  smallestDistance = CloudBakeSdPrimitive( shape.Primitives[0], p );
        for ( size_t i = 1; i < shape.Primitives.size(); ++i )
        {
            const float d = CloudBakeSdPrimitive( shape.Primitives[i], p );
            if ( d < smallestDistance )
            {
                smallestDistance = d;
                nearest          = i;
            }
        }

        // A zero blend radius is a hard union: the nearest lobe answers alone, for all three channels.
        if ( !( shape.BlendRadius > 0.0f ) )
        {
            field.SignedDistance = smallestDistance;
            field.DetailType     = shape.Primitives[nearest].DetailType;
            field.DensityScale   = shape.Primitives[nearest].DensityScale;
            return field;
        }

        const float k         = shape.BlendRadius;
        float       weightSum = 0.0f;
        float       typeSum   = 0.0f;
        float       scaleSum  = 0.0f;
        for ( const auto& primitive : shape.Primitives )
        {
            const float d = CloudBakeSdPrimitive( primitive, p );
            const float w = std::exp( -( d - smallestDistance ) / k );
            weightSum += w;
            typeSum += w * primitive.DetailType;
            scaleSum += w * primitive.DensityScale;
        }

        field.SignedDistance = smallestDistance - k * std::log( weightSum );
        field.DetailType     = typeSum / weightSum;
        field.DensityScale   = scaleSum / weightSum;
        return field;
    }

    // The deck's Dimensional Profile, and the whole of the conversion (VOXEL_CLOUD_PATH.md §5(b)):
    // 1 at and below `-falloff`, 0 at and outside the surface, linear between.
    inline float CloudBakeProfile( float signedDistance, float falloff )
    {
        if ( !( falloff > 0.0f ) )
            return signedDistance < 0.0f ? 1.0f : 0.0f;

        return glm::clamp( -signedDistance / falloff, 0.0f, 1.0f );
    }

    // ---- Baking -----------------------------------------------------------------------------------

    struct CloudBakeSettings
    {
        // 128x128x64 is the teamlead's Q2 answer: the atlas is eight tiles of exactly this size, so a
        // volume baked at anything else cannot be placed without resampling.
        uint32_t Width  = 128;
        uint32_t Height = 128;
        uint32_t Depth  = 64;

        // The world box the volume covers, as a FULL extent in world units (cm). The default is
        // 1024 m x 1024 m x 512 m, i.e. 8 m per voxel on every axis — the reference deck's own voxel
        // size (p. 82) and the design doc's hero-cumulus row (§4.3).
        std::array<float, 3> Extent{ 102400.0f, 102400.0f, 51200.0f };

        // The +/- range the `.a` channel encodes, in world units (cm). 256 m by default: far enough to
        // measure most of the way across an empty tile so the distance is worth a long step, and still
        // only 2.0 m per quantisation level — a quarter of a voxel, so the encoding is not what limits
        // the field's accuracy.
        float SignedDistanceRange = 25600.0f;
    };

    // The outermost voxel shell of every `.dvol` must be empty. Two things depend on it:
    //
    //   * the ATLAS. Eight tiles share one image, and although the sampler arithmetic clamps a tap into
    //     its own tile (see CloudVolumeAtlasLayout.hpp), an empty shell means the clamp degrades into a
    //     fade rather than into a smeared edge texel.
    //   * the CLOUD. A shape that reaches the boundary of its box gets sliced flat there, and a cloud
    //     with a razor-straight vertical face is the single most obvious way for this to look wrong.
    //
    // So it is CHECKED, not imposed. Forcing the shell to zero would hide an overflowing shape behind a
    // one-voxel cliff; reporting it tells the author to shrink the lobes or grow the extent.
    inline uint8_t CloudVolumeShellMaxProfile( const CloudVolume& volume )
    {
        uint8_t worst = 0;
        for ( uint32_t z = 0; z < volume.Header.Depth; ++z )
        {
            const bool zEdge = ( z == 0 || z + 1 == volume.Header.Depth );
            for ( uint32_t y = 0; y < volume.Header.Height; ++y )
            {
                const bool yEdge = ( y == 0 || y + 1 == volume.Header.Height );
                for ( uint32_t x = 0; x < volume.Header.Width; ++x )
                {
                    if ( !zEdge && !yEdge && x != 0 && x + 1 != volume.Header.Width )
                    {
                        // Skip straight across the interior of this row instead of testing every voxel.
                        x = volume.Header.Width - 2;
                        continue;
                    }

                    const uint8_t profile = volume.Voxels[CloudVolumeVoxelIndex( volume.Header, x, y, z )];
                    if ( profile > worst )
                        worst = profile;
                }
            }
        }
        return worst;
    }

    // The centre of voxel (x,y,z) in the volume's local space, in world units. The half-voxel offset is
    // not cosmetic: a sampler reads texel centres, so baking at grid corners would put the whole field
    // half a voxel out of step with every fetch that ever reads it.
    inline glm::vec3 CloudBakeVoxelCenter( const CloudBakeSettings& settings, uint32_t x, uint32_t y, uint32_t z )
    {
        const glm::vec3 extent = CloudBakeToVec3( settings.Extent );
        const glm::vec3 uvw =
             glm::vec3( ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( settings.Width ),
                        ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( settings.Height ),
                        ( static_cast<float>( z ) + 0.5f ) / static_cast<float>( settings.Depth ) );
        return ( uvw - glm::vec3( 0.5f ) ) * extent;
    }

    inline Common::ResultStr<CloudVolume> BakeCloudVolume( const CloudBakeShape&    shape,
                                                           const CloudBakeSettings& settings )
    {
        if ( shape.Primitives.empty() )
            return Common::MakeError<CloudVolume>( "Cannot bake a cloud volume from an empty primitive list" );

        // Three voxels per axis is the smallest grid that has an empty shell AND an interior to put a
        // cloud in; below that the guard-band guarantee has nothing to guard.
        if ( settings.Width < 3 || settings.Height < 3 || settings.Depth < 3 )
            return Common::MakeFormattedError<CloudVolume>(
                 "Cloud volume dimensions {}x{}x{} are below the 3x3x3 minimum a guard band needs", settings.Width,
                 settings.Height, settings.Depth );

        if ( !( shape.ProfileFalloff > 0.0f ) )
            return Common::MakeFormattedError<CloudVolume>(
                 "Cloud volume profile falloff {} world units is not strictly positive", shape.ProfileFalloff );

        if ( shape.BlendRadius < 0.0f )
            return Common::MakeFormattedError<CloudVolume>( "Cloud volume blend radius {} is negative",
                                                            shape.BlendRadius );

        CloudVolume volume;
        volume.Header.Width               = settings.Width;
        volume.Header.Height              = settings.Height;
        volume.Header.Depth               = settings.Depth;
        volume.Header.ExtentX             = settings.Extent[0];
        volume.Header.ExtentY             = settings.Extent[1];
        volume.Header.ExtentZ             = settings.Extent[2];
        volume.Header.SignedDistanceRange = settings.SignedDistanceRange;

        const auto valid = ValidateCloudVolumeHeader( volume.Header );
        if ( !valid.IsSuccess() )
            return Common::MakeError<CloudVolume>( valid.GetError() );

        volume.Voxels.resize( static_cast<size_t>( CloudVolumePayloadBytes( volume.Header ) ) );

        for ( uint32_t z = 0; z < settings.Depth; ++z )
        {
            for ( uint32_t y = 0; y < settings.Height; ++y )
            {
                for ( uint32_t x = 0; x < settings.Width; ++x )
                {
                    const CloudBakeField field =
                         CloudBakeEvaluate( shape, CloudBakeVoxelCenter( settings, x, y, z ) );
                    const float profile = CloudBakeProfile( field.SignedDistance, shape.ProfileFalloff );

                    const size_t at = CloudVolumeVoxelIndex( volume.Header, x, y, z );

                    // The three [0,1] channels quantise by rounding to nearest: they are read as values,
                    // not as bounds, so the smallest error is the right error. Only the distance channel
                    // trades accuracy for the conservative guarantee (see EncodeSignedDistance).
                    const auto unorm8 = []( float v )
                    { return static_cast<unsigned char>( glm::clamp( v, 0.0f, 1.0f ) * 255.0f + 0.5f ); };

                    volume.Voxels[at + 0] = unorm8( profile );
                    volume.Voxels[at + 1] = unorm8( field.DetailType );
                    volume.Voxels[at + 2] = unorm8( field.DensityScale );
                    volume.Voxels[at + 3] =
                         EncodeSignedDistance( field.SignedDistance, settings.SignedDistanceRange );
                }
            }
        }

        const uint8_t shell = CloudVolumeShellMaxProfile( volume );
        if ( shell > 0 )
            return Common::MakeFormattedError<CloudVolume>(
                 "The shape reaches the boundary of its {}x{}x{} box (the outermost voxel shell peaks at a "
                 "profile of {}/255, and must be 0). Shrink the primitives or grow the extent past its "
                 "current {}x{}x{} world units.",
                 settings.Width, settings.Height, settings.Depth, static_cast<uint32_t>( shell ),
                 settings.Extent[0], settings.Extent[1], settings.Extent[2] );

        return Common::MakeSuccess( std::move( volume ) );
    }

    // ---- The authored file ------------------------------------------------------------------------

    // What a `.cloudshape.json` holds, and the ONLY thing the baker tool reads. Shapes are CONTENT: the
    // cumulus, the congestus and the anvil live in data files, and adding a fourth is authoring, not a
    // rebuild.
    struct CloudBakeDescription
    {
        // Free text, carried for the tool's log line so a bake names itself. Nothing reads it as an
        // identifier — the `.dvol`'s identity is its path.
        std::string       Name;
        CloudBakeSettings Settings;
        CloudBakeShape    Shape;
    };
} // namespace Desert::Graphic
