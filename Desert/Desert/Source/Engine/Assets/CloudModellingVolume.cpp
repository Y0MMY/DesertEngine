#include "CloudModellingVolume.hpp"

#include <Engine/Assets/ContainerBytes.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

namespace Desert::Assets
{
    namespace
    {
        // The one pixel format a volume may be in. Written into the header and CHECKED on read, so a file
        // from a future build that stored half-floats is refused by name instead of being read as bytes.
        constexpr uint32_t kFormatRgba8 = 0u;

        // How many lumps one body may be made of. The bake is `voxels x blobs` ellipsoid evaluations —
        // 1 048 576 x 64 is 67 million, about a second optimised — so the ceiling is a bake time an artist
        // will wait through rather than an expressive limit. The shipped example uses eight.
        constexpr uint32_t kMaxBlobs = 64u;

        bool IsFinite( float value )
        {
            return std::isfinite( value );
        }

        bool IsFinite( const glm::vec3& value )
        {
            return IsFinite( value.x ) && IsFinite( value.y ) && IsFinite( value.z );
        }

        /**
         * The signed distance to an ellipsoid, in kilometres, negative inside.
         *
         * Inigo Quilez's bounded form. It is EXACT for a sphere — substitute r = R and it reduces to
         * `|p| - R` — and a tight underestimate otherwise, which is the safe direction here: an
         * underestimate makes the body slightly smaller than the bound Validate checked, never larger.
         *
         * The guard at the centre is not defensive padding: `k1` is zero exactly at p = 0 and the
         * expression is 0/0 there. The answer at the centre is the shortest semi-axis, which is what a
         * sphere's own formula gives and what the limit approaches along the short axis.
         */
        float EllipsoidDistanceKm( const glm::vec3& p, const glm::vec3& radii )
        {
            const glm::vec3 scaled  = p / radii;
            const glm::vec3 scaled2 = scaled / radii;

            const float k1 = glm::length( scaled2 );
            if ( k1 < 1e-9f )
                return -std::min( radii.x, std::min( radii.y, radii.z ) );

            const float k0 = glm::length( scaled );
            return k0 * ( k0 - 1.0f ) / k1;
        }

        unsigned char Quantize( float unit )
        {
            const float clamped = std::clamp( unit, 0.0f, 1.0f );
            return static_cast<unsigned char>( clamped * 255.0f + 0.5f );
        }

        /// The distance by which the exponential smooth minimum of @p count equal distances pushes the
        /// zero set outward. Exact rather than a safety factor: `-r*ln(N*exp(-d/r))` is `d - r*ln(N)`.
        float JoinInflationKm( float blendRadiusKm, size_t count )
        {
            if ( count <= 1u )
                return 0.0f;
            return blendRadiusKm * std::log( static_cast<float>( count ) );
        }
    } // namespace

    const char* CloudModellingChannelName( CloudModellingChannel channel )
    {
        switch ( channel )
        {
            case CloudModellingChannel::DimensionalProfile:
                return "Dimensional Profile (depth inside the body)";
            case CloudModellingChannel::DetailType:
                return "Detail Type (0 wispy, 1 billowy)";
            case CloudModellingChannel::DensityScale:
                return "Density Scale (per-voxel multiplier)";
            case CloudModellingChannel::CutoutEnvelope:
                return "Cutout Envelope (the body, dilated)";
        }
        return "unknown";
    }

    Common::BoolResultStr ValidateCloudModellingRecipe( const CloudModellingVolumeRecipe& recipe )
    {
        if ( !IsFinite( recipe.SizeKm ) || recipe.SizeKm.x <= 0.0f || recipe.SizeKm.y <= 0.0f ||
             recipe.SizeKm.z <= 0.0f )
            return Common::MakeFormattedError<bool>( "Size must be positive on every axis, got {} x {} x {} km",
                                                     recipe.SizeKm.x, recipe.SizeKm.y, recipe.SizeKm.z );

        if ( !IsFinite( recipe.BlendRadiusKm ) || recipe.BlendRadiusKm <= 0.0f )
            return Common::MakeFormattedError<bool>(
                 "Blend Radius must be strictly positive — it is the reciprocal of the join's sharpness and "
                 "zero is a division — got {} km",
                 recipe.BlendRadiusKm );

        if ( !IsFinite( recipe.ProfileDepthKm ) || recipe.ProfileDepthKm <= 0.0f )
            return Common::MakeFormattedError<bool>( "Profile Depth must be strictly positive, got {} km",
                                                     recipe.ProfileDepthKm );

        if ( !IsFinite( recipe.EnvelopeMarginKm ) || recipe.EnvelopeMarginKm <= 0.0f )
            return Common::MakeFormattedError<bool>( "Envelope Margin must be strictly positive, got {} km",
                                                     recipe.EnvelopeMarginKm );

        if ( recipe.Blobs.empty() )
            return Common::MakeError<bool>( "a modelling volume with no lumps in it is an empty box, not a "
                                            "cloud; add at least one" );

        if ( recipe.Blobs.size() > kMaxBlobs )
            return Common::MakeFormattedError<bool>( "{} lumps is past the ceiling of {}", recipe.Blobs.size(),
                                                     kMaxBlobs );

        for ( size_t i = 0; i < recipe.Blobs.size(); ++i )
        {
            const CloudModellingBlob& blob = recipe.Blobs[i];

            if ( !IsFinite( blob.CentreKm ) )
                return Common::MakeFormattedError<bool>( "lump {} has a centre that is not a finite number", i );

            if ( !IsFinite( blob.RadiiKm ) || blob.RadiiKm.x <= 0.0f || blob.RadiiKm.y <= 0.0f ||
                 blob.RadiiKm.z <= 0.0f )
                return Common::MakeFormattedError<bool>(
                     "lump {} has radii {} x {} x {} km; every semi-axis must be strictly positive, because a "
                     "zero axis divides by zero in the distance function",
                     i, blob.RadiiKm.x, blob.RadiiKm.y, blob.RadiiKm.z );

            if ( !IsFinite( blob.DetailType ) || blob.DetailType < 0.0f || blob.DetailType > 1.0f )
                return Common::MakeFormattedError<bool>( "lump {} has Detail Type {}, outside [0, 1]", i,
                                                         blob.DetailType );

            if ( !IsFinite( blob.DensityScale ) || blob.DensityScale < 0.0f || blob.DensityScale > 1.0f )
                return Common::MakeFormattedError<bool>( "lump {} has Density Scale {}, outside [0, 1]", i,
                                                         blob.DensityScale );
        }

        // THE BODY MUST NOT REACH ITS OWN BOX, and the slack is the join's inflation plus the envelope's
        // dilation. Both are real distances the bake adds to the lumps' own extents, so checking the
        // lumps alone would pass a recipe whose cloud is cut flat by the volume's face — and, every
        // sampler in this engine being REPEAT, whose top would then blend with its own bottom.
        const float slackKm =
             JoinInflationKm( recipe.BlendRadiusKm, recipe.Blobs.size() ) + recipe.EnvelopeMarginKm;

        glm::vec3 reach{ 0.0f };
        for ( const CloudModellingBlob& blob : recipe.Blobs )
        {
            reach.x = std::max( reach.x, std::abs( blob.CentreKm.x ) + blob.RadiiKm.x );
            reach.y = std::max( reach.y, std::abs( blob.CentreKm.y ) + blob.RadiiKm.y );
            reach.z = std::max( reach.z, std::abs( blob.CentreKm.z ) + blob.RadiiKm.z );
        }

        const glm::vec3 needed = reach + glm::vec3( slackKm );
        const glm::vec3 half   = recipe.SizeKm * 0.5f;

        if ( needed.x > half.x || needed.y > half.y || needed.z > half.z )
            return Common::MakeFormattedError<bool>(
                 "the body reaches {:.3f} x {:.3f} x {:.3f} km from the centre once the join's inflation "
                 "({:.3f} km over {} lumps) and the envelope's margin ({:.3f} km) are added, which does not fit "
                 "inside the half-size {:.3f} x {:.3f} x {:.3f} km. Either grow Size or move the lumps in.",
                 needed.x, needed.y, needed.z, JoinInflationKm( recipe.BlendRadiusKm, recipe.Blobs.size() ),
                 recipe.Blobs.size(), recipe.EnvelopeMarginKm, half.x, half.y, half.z );

        return Common::MakeSuccess( true );
    }

    Common::ResultStr<std::vector<unsigned char>>
    GenerateCloudModellingVolume( const CloudModellingVolumeRecipe& recipe )
    {
        if ( auto valid = ValidateCloudModellingRecipe( recipe ); !valid )
            return Common::MakeFormattedError<std::vector<unsigned char>>( "recipe is not usable: {}",
                                                                           valid.GetError() );

        // THE LUMPS ARE SORTED INTO A CANONICAL ORDER BEFORE ANYTHING IS SUMMED, and this is a MEASURED
        // correction rather than tidiness.
        //
        // The exponential smooth minimum is commutative and associative in REAL arithmetic, which is the
        // first of the three properties it was chosen for (PLAN_AUTHORED_CLOUDS.md section 3) — but
        // floating-point addition is neither, so `sum += exp(...)` over a shuffled list produces a
        // slightly different total. Measured on the shipped recipe: shuffling the eight lumps moved
        // 6 bytes of 4 194 304, each by one 255th. Tiny, and still the wrong shape of answer — a bake is
        // a build artefact, and a build artefact whose bytes depend on the order its inputs happened to be
        // listed in cannot be compared, cached by hash, or asserted equal.
        //
        // Sorting once, here, makes the summation order a function of the lumps themselves and not of the
        // sequence they arrived in, so the property the plan claims is exactly true rather than nearly
        // true. It costs one sort of at most 64 elements against 8.4 million exponentials.
        //
        // The key is lexicographic on every authored number, so two lumps that differ in any way have a
        // defined order and two that are identical are interchangeable by construction.
        std::vector<CloudModellingBlob> blobs = recipe.Blobs;
        std::sort( blobs.begin(), blobs.end(),
                   []( const CloudModellingBlob& a, const CloudModellingBlob& b )
                   {
                       const auto key = []( const CloudModellingBlob& blob )
                       {
                           return std::tie( blob.CentreKm.x, blob.CentreKm.y, blob.CentreKm.z, blob.RadiiKm.x,
                                            blob.RadiiKm.y, blob.RadiiKm.z, blob.DetailType, blob.DensityScale );
                       };
                       return key( a ) < key( b );
                   } );

        const uint32_t width  = kCloudModellingVolumeWidth;
        const uint32_t height = kCloudModellingVolumeHeight;
        const uint32_t depth  = kCloudModellingVolumeDepth;

        const glm::vec3 half = recipe.SizeKm * 0.5f;
        const glm::vec3 voxelSizeKm =
             recipe.SizeKm /
             glm::vec3( static_cast<float>( width ), static_cast<float>( height ), static_cast<float>( depth ) );
        const float  invBlend    = 1.0f / recipe.BlendRadiusKm;
        const float  invProfile  = 1.0f / recipe.ProfileDepthKm;
        const float  invEnvelope = 1.0f / recipe.EnvelopeMarginKm;
        const size_t blobCount   = blobs.size();

        std::vector<unsigned char> voxels( static_cast<size_t>( kCloudModellingVoxelBytes ), 0u );
        std::vector<float>         weights( blobCount, 0.0f );

        for ( uint32_t z = 0; z < depth; ++z )
        {
            const float pz =
                 ( ( static_cast<float>( z ) + 0.5f ) / static_cast<float>( depth ) ) * recipe.SizeKm.z - half.z;

            for ( uint32_t y = 0; y < height; ++y )
            {
                const float py =
                     ( ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( height ) ) * recipe.SizeKm.y -
                     half.y;

                for ( uint32_t x = 0; x < width; ++x )
                {
                    const float px =
                         ( ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( width ) ) * recipe.SizeKm.x -
                         half.x;

                    const glm::vec3 point{ px, py, pz };

                    // THE SHIFT IS WHAT KEEPS THIS FINITE. `exp(-d/r)` overflows a float once d/r passes
                    // about 88, which at the shipped 50 m blend radius is 4.4 km — nearer than the corner
                    // of a box a mile across. Subtracting the smallest distance first is algebraically the
                    // identity and moves the largest exponent to exactly 1.
                    float nearest = EllipsoidDistanceKm( point - blobs[0].CentreKm, blobs[0].RadiiKm );
                    for ( size_t k = 1; k < blobCount; ++k )
                        nearest = std::min( nearest,
                                            EllipsoidDistanceKm( point - blobs[k].CentreKm, blobs[k].RadiiKm ) );

                    float sum = 0.0f;
                    for ( size_t k = 0; k < blobCount; ++k )
                    {
                        const float distance = EllipsoidDistanceKm( point - blobs[k].CentreKm, blobs[k].RadiiKm );
                        const float weight   = std::exp( -( distance - nearest ) * invBlend );
                        weights[k]           = weight;
                        sum += weight;
                    }

                    // The exponential smooth minimum, shifted back. Commutative and associative in the
                    // distances, so the order of the lumps cannot reach the answer.
                    const float joined = nearest - std::log( sum ) * recipe.BlendRadiusKm;

                    const size_t index =
                         ( ( static_cast<size_t>( z ) * height + y ) * width + x ) * kCloudModellingBytesPerVoxel;

                    // Everything outside the body is four zeroes — including the envelope, past its own
                    // margin — so the early-out in the march is a single comparison and the empty parts of
                    // a hero cloud's box cost exactly what empty sky costs.
                    if ( joined >= recipe.EnvelopeMarginKm )
                        continue;

                    // THE TWO MATERIAL CHANNELS ARE THE JOIN'S OWN WEIGHTS, not a second construction.
                    // `weights` is already the softmax of the negated distances, so dividing by its sum
                    // gives a partition of unity over the lumps: a point deep inside one lump takes that
                    // lump's numbers, and a point in the crease between two takes their blend, with the
                    // blend's width being the same blend radius that fused the shapes. That is the second
                    // of the three properties the exponential smooth-min was chosen for.
                    const float invSum       = 1.0f / sum;
                    float       detailType   = 0.0f;
                    float       densityScale = 0.0f;
                    for ( size_t k = 0; k < blobCount; ++k )
                    {
                        const float share = weights[k] * invSum;
                        detailType += share * blobs[k].DetailType;
                        densityScale += share * blobs[k].DensityScale;
                    }

                    // 0 at the surface and 1 at ProfileDepth inside, which is Guerrilla's Dimensional
                    // Profile (deck p.85) obtained analytically instead of by a distance transform.
                    const float profile = std::clamp( -joined * invProfile, 0.0f, 1.0f );

                    // 1 throughout the body and falling to 0 at the margin outside it. Above zero
                    // wherever the profile is, and past it, which is what makes it conservative.
                    const float envelope =
                         std::clamp( ( recipe.EnvelopeMarginKm - joined ) * invEnvelope, 0.0f, 1.0f );

                    voxels[index + 0] = Quantize( profile );
                    voxels[index + 1] = Quantize( detailType );
                    voxels[index + 2] = Quantize( densityScale );
                    voxels[index + 3] = Quantize( envelope );
                }
            }
        }

        // THE BOUND WAS ARITHMETIC; THIS IS THE MEASUREMENT. Validate's slack is a conservative estimate
        // of the join's inflation, and a conservative estimate is exactly the kind of thing that is right
        // until somebody changes the distance function. Walking the six faces costs 0.6 % of the bake and
        // turns "the body should fit" into "the body does fit".
        const auto faceIsEmpty = [&]( uint32_t x, uint32_t y, uint32_t z )
        {
            const size_t index =
                 ( ( static_cast<size_t>( z ) * height + y ) * width + x ) * kCloudModellingBytesPerVoxel;
            return voxels[index + 0] == 0u && voxels[index + 3] == 0u;
        };

        for ( uint32_t z = 0; z < depth; ++z )
        {
            for ( uint32_t y = 0; y < height; ++y )
            {
                for ( uint32_t x = 0; x < width; ++x )
                {
                    const bool onFace =
                         ( x == 0 || x == width - 1 || y == 0 || y == height - 1 || z == 0 || z == depth - 1 );
                    if ( !onFace || faceIsEmpty( x, y, z ) )
                        continue;

                    return Common::MakeFormattedError<std::vector<unsigned char>>(
                         "the baked body touches the volume's boundary at voxel ({}, {}, {}) — the cloud would "
                         "be cut flat there, and a REPEAT sampler would blend that face with the opposite one. "
                         "Grow Size or move the lumps in; the voxel is {:.1f} x {:.1f} x {:.1f} m",
                         x, y, z, voxelSizeKm.x * 1000.0f, voxelSizeKm.y * 1000.0f, voxelSizeKm.z * 1000.0f );
                }
            }
        }

        return Common::MakeSuccess( std::move( voxels ) );
    }

    std::vector<unsigned char> EncodeCloudModellingVolume( const CloudModellingVolumeData& data )
    {
        std::vector<unsigned char> blobBytes;
        blobBytes.reserve( data.Recipe.Blobs.size() * kCloudModellingBlobBytes );
        for ( const CloudModellingBlob& blob : data.Recipe.Blobs )
        {
            WriteF32( blobBytes, blob.CentreKm.x );
            WriteF32( blobBytes, blob.CentreKm.y );
            WriteF32( blobBytes, blob.CentreKm.z );
            WriteF32( blobBytes, blob.RadiiKm.x );
            WriteF32( blobBytes, blob.RadiiKm.y );
            WriteF32( blobBytes, blob.RadiiKm.z );
            WriteF32( blobBytes, blob.DetailType );
            WriteF32( blobBytes, blob.DensityScale );
        }

        std::vector<unsigned char> out;
        out.reserve( kCloudModellingHeaderSize + blobBytes.size() + data.Voxels.size() );

        out.insert( out.end(), kCloudModellingMagic, kCloudModellingMagic + sizeof( kCloudModellingMagic ) );
        WriteU32( out, kCloudModellingContainerVersion );
        WriteU32( out, data.GeneratorVersion );
        WriteU32( out, kCloudModellingVolumeWidth );
        WriteU32( out, kCloudModellingVolumeHeight );
        WriteU32( out, kCloudModellingVolumeDepth );
        WriteU32( out, kFormatRgba8 );

        // The channel meanings are STORED, not implied. A reader that finds an order it does not know can
        // say so; a reader that assumed the order would render the density scale as a silhouette and look
        // merely wrong.
        WriteU32( out, static_cast<uint32_t>( CloudModellingChannel::DimensionalProfile ) );
        WriteU32( out, static_cast<uint32_t>( CloudModellingChannel::DetailType ) );
        WriteU32( out, static_cast<uint32_t>( CloudModellingChannel::DensityScale ) );
        WriteU32( out, static_cast<uint32_t>( CloudModellingChannel::CutoutEnvelope ) );

        WriteF32( out, data.Recipe.SizeKm.x );
        WriteF32( out, data.Recipe.SizeKm.y );
        WriteF32( out, data.Recipe.SizeKm.z );
        WriteF32( out, data.Recipe.BlendRadiusKm );
        WriteF32( out, data.Recipe.ProfileDepthKm );
        WriteF32( out, data.Recipe.EnvelopeMarginKm );
        WriteU32( out, static_cast<uint32_t>( data.Recipe.Blobs.size() ) );

        WriteU64( out, static_cast<uint64_t>( data.Voxels.size() ) );
        WriteU32( out, Crc32( data.Voxels.data(), data.Voxels.size() ) );
        WriteU32( out, Crc32( blobBytes.data(), blobBytes.size() ) );

        out.insert( out.end(), blobBytes.begin(), blobBytes.end() );
        out.insert( out.end(), data.Voxels.begin(), data.Voxels.end() );
        return out;
    }

    Common::ResultStr<CloudModellingVolumeData>
    DecodeCloudModellingVolume( const std::vector<unsigned char>& bytes )
    {
        if ( bytes.size() < kCloudModellingHeaderSize )
            return Common::MakeFormattedError<CloudModellingVolumeData>(
                 "file is {} bytes, shorter than the {}-byte header", bytes.size(), kCloudModellingHeaderSize );

        const unsigned char* at = bytes.data();

        if ( std::memcmp( at, kCloudModellingMagic, sizeof( kCloudModellingMagic ) ) != 0 )
            return Common::MakeFormattedError<CloudModellingVolumeData>(
                 "not a cloud modelling volume: magic is '{:02X}{:02X}{:02X}{:02X}', expected 'DCMV'", at[0],
                 at[1], at[2], at[3] );

        const uint32_t containerVersion = ReadU32( at + 4 );
        if ( containerVersion != kCloudModellingContainerVersion )
            return Common::MakeFormattedError<CloudModellingVolumeData>(
                 "container version {} is not the {} this build reads", containerVersion,
                 kCloudModellingContainerVersion );

        CloudModellingVolumeData data;
        data.GeneratorVersion = ReadU32( at + 8 );

        const uint32_t width  = ReadU32( at + 12 );
        const uint32_t height = ReadU32( at + 16 );
        const uint32_t depth  = ReadU32( at + 20 );
        const uint32_t format = ReadU32( at + 24 );

        if ( width != kCloudModellingVolumeWidth || height != kCloudModellingVolumeHeight ||
             depth != kCloudModellingVolumeDepth )
            return Common::MakeFormattedError<CloudModellingVolumeData>(
                 "volume is {}x{}x{} voxels; this build reads {}x{}x{}, which the format fixes so the shader "
                 "can clamp a fetch by half a texel at compile time",
                 width, height, depth, kCloudModellingVolumeWidth, kCloudModellingVolumeHeight,
                 kCloudModellingVolumeDepth );

        if ( format != kFormatRgba8 )
            return Common::MakeFormattedError<CloudModellingVolumeData>(
                 "pixel format {} is not RGBA8 ({}), which is the only format a volume may be in", format,
                 kFormatRgba8 );

        for ( uint32_t channel = 0; channel < 4u; ++channel )
        {
            const uint32_t stored = ReadU32( at + 28 + channel * 4u );
            if ( stored != channel )
                return Common::MakeFormattedError<CloudModellingVolumeData>(
                     "channel {} declares meaning {}, but this build reads volumes whose channels are "
                     "Dimensional Profile / Detail Type / Density Scale / Cutout Envelope in that order",
                     channel, stored );
        }

        data.Recipe.SizeKm           = glm::vec3( ReadF32( at + 44 ), ReadF32( at + 48 ), ReadF32( at + 52 ) );
        data.Recipe.BlendRadiusKm    = ReadF32( at + 56 );
        data.Recipe.ProfileDepthKm   = ReadF32( at + 60 );
        data.Recipe.EnvelopeMarginKm = ReadF32( at + 64 );

        const uint32_t blobCount   = ReadU32( at + 68 );
        const uint64_t voxelBytes  = ReadU64( at + 72 );
        const uint32_t storedVoxel = ReadU32( at + 80 );
        const uint32_t storedBlob  = ReadU32( at + 84 );

        if ( blobCount == 0u || blobCount > kMaxBlobs )
            return Common::MakeFormattedError<CloudModellingVolumeData>( "header declares {} lumps, outside 1..{}",
                                                                         blobCount, kMaxBlobs );

        // The extents and the payload length are two statements of one fact, and the whole class of
        // defects this programme keeps meeting is two statements of one fact that disagree. Checked here,
        // once, rather than trusted into an out-of-bounds upload later.
        if ( voxelBytes != kCloudModellingVoxelBytes )
            return Common::MakeFormattedError<CloudModellingVolumeData>(
                 "header says {} payload bytes but a {}x{}x{} RGBA8 volume is {} bytes", voxelBytes,
                 kCloudModellingVolumeWidth, kCloudModellingVolumeHeight, kCloudModellingVolumeDepth,
                 kCloudModellingVoxelBytes );

        const uint64_t expected = static_cast<uint64_t>( kCloudModellingHeaderSize ) +
                                  blobCount * kCloudModellingBlobBytes + voxelBytes;

        if ( bytes.size() != expected )
            return Common::MakeFormattedError<CloudModellingVolumeData>(
                 "file is {} bytes; a header, {} lumps and the payload are {}", bytes.size(), blobCount,
                 expected );

        const unsigned char* blobAt = at + kCloudModellingHeaderSize;

        if ( Crc32( blobAt, blobCount * kCloudModellingBlobBytes ) != storedBlob )
            return Common::MakeFormattedError<CloudModellingVolumeData>(
                 "the recipe's checksum does not match the {:08X} in the header; the file is corrupt",
                 storedBlob );

        data.Recipe.Blobs.resize( blobCount );
        for ( uint32_t i = 0; i < blobCount; ++i )
        {
            const unsigned char* b = blobAt + static_cast<size_t>( i ) * kCloudModellingBlobBytes;

            data.Recipe.Blobs[i].CentreKm   = glm::vec3( ReadF32( b + 0 ), ReadF32( b + 4 ), ReadF32( b + 8 ) );
            data.Recipe.Blobs[i].RadiiKm    = glm::vec3( ReadF32( b + 12 ), ReadF32( b + 16 ), ReadF32( b + 20 ) );
            data.Recipe.Blobs[i].DetailType = ReadF32( b + 24 );
            data.Recipe.Blobs[i].DensityScale = ReadF32( b + 28 );
        }

        const unsigned char* voxelAt = blobAt + static_cast<size_t>( blobCount ) * kCloudModellingBlobBytes;
        data.Voxels.assign( voxelAt, voxelAt + voxelBytes );

        const uint32_t actualVoxel = Crc32( data.Voxels.data(), data.Voxels.size() );
        if ( actualVoxel != storedVoxel )
            return Common::MakeFormattedError<CloudModellingVolumeData>(
                 "payload checksum {:08X} does not match the {:08X} in the header; the file is corrupt",
                 actualVoxel, storedVoxel );

        // Validated LAST, so a corrupt file is reported as corrupt rather than as an illegal recipe read
        // out of its wreckage.
        if ( auto valid = ValidateCloudModellingRecipe( data.Recipe ); !valid )
            return Common::MakeFormattedError<CloudModellingVolumeData>( "header recipe is not usable: {}",
                                                                         valid.GetError() );

        return Common::MakeSuccess( std::move( data ) );
    }

    const CloudModellingVolumeRecipe& CloudModellingDefaultRecipe()
    {
        // A CUMULUS, SCULPTED AS ONE CONNECTED BODY. Every lump below overlaps the one it grew from by
        // more than the blend radius, so the join fuses them into a single surface rather than a row of
        // beads — which is the whole point of the phase, because the procedural producer's Alligator lobes
        // cannot merge by construction.
        //
        // The proportions are a cumulus mediocris rising into a congestus: a flattened base pad 1.2 km
        // across, two shoulders, a body, a tower and a crown, plus one wispy tail shorn off downwind. The
        // box is 2 x 1 x 2 km at 128 x 64 x 128 voxels, so a voxel is 15.6 m on the horizontal axes and
        // 15.6 m on the vertical one — the arithmetic of PLAN_AUTHORED_CLOUDS.md §2, and the 8 m of the
        // deck is not the target: 8 m of DATA is 0.5 m of VISIBLE detail there because the up-rez carries
        // it, and the up-rez is Common/CloudField.glslh's erosion here.
        static const CloudModellingVolumeRecipe recipe = []
        {
            CloudModellingVolumeRecipe r;
            r.SizeKm           = glm::vec3( 2.0f, 1.0f, 2.0f );
            r.BlendRadiusKm    = 0.05f;
            r.ProfileDepthKm   = 0.18f;
            r.EnvelopeMarginKm = 0.09f;
            r.Blobs            = {
                 // base pad — flat and wide, the underside a cumulus has because it sits on the
                 // condensation level
                 CloudModellingBlob{ glm::vec3( 0.00f, -0.17f, 0.00f ), glm::vec3( 0.60f, 0.11f, 0.54f ), 0.90f,
                                     0.85f },
                 // west lobe
                 CloudModellingBlob{ glm::vec3( -0.30f, -0.06f, 0.08f ), glm::vec3( 0.36f, 0.17f, 0.32f ), 0.95f,
                                     1.00f },
                 // east lobe
                 CloudModellingBlob{ glm::vec3( 0.32f, -0.08f, -0.12f ), glm::vec3( 0.34f, 0.16f, 0.30f ), 0.95f,
                                     1.00f },
                 // the body the tower rises out of
                 CloudModellingBlob{ glm::vec3( 0.02f, 0.02f, -0.02f ), glm::vec3( 0.40f, 0.20f, 0.36f ), 1.00f,
                                     1.00f },
                 // tower
                 CloudModellingBlob{ glm::vec3( -0.06f, 0.11f, 0.04f ), glm::vec3( 0.25f, 0.17f, 0.23f ), 1.00f,
                                     0.95f },
                 // crown
                 CloudModellingBlob{ glm::vec3( -0.03f, 0.20f, 0.02f ), glm::vec3( 0.15f, 0.085f, 0.14f ), 1.00f,
                                     0.80f },
                 // shoulder puff
                 CloudModellingBlob{ glm::vec3( 0.30f, 0.08f, 0.22f ), glm::vec3( 0.19f, 0.12f, 0.17f ), 0.85f,
                                     0.75f },
                 // the wispy tail: the one lump whose Detail Type is near zero, so the join hands the
                 // up-rez a WISPY erosion there and a billowy one everywhere else — two channels of the
                 // volume written by the same weights that fused the shapes
                 CloudModellingBlob{ glm::vec3( -0.52f, -0.02f, -0.30f ), glm::vec3( 0.24f, 0.09f, 0.19f ), 0.30f,
                                     0.50f },
            };
            return r;
        }();

        return recipe;
    }
} // namespace Desert::Assets
