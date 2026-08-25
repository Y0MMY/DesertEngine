#include "CloudLayout.hpp"

#include <Engine/Assets/ContainerBytes.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Desert::Assets
{
    namespace
    {
        constexpr uint32_t kPatternBytesPerTexel = 4u;

        /// What the mask's neutral byte is. 128 rather than 0 because the stored table is UNSIGNED and an
        /// artist floods a painting with mid-grey, not with black — see the note on CloudLayoutData::Mask.
        constexpr float kMaskNeutral = 128.0f;

        /// Which way the two table-presence bits sit in the header's flag word. Written out rather than
        /// implied so that a reader from another build cannot disagree about which bit is which.
        constexpr uint32_t kFlagHasPattern = 1u << 0;
        constexpr uint32_t kFlagHasMask    = 1u << 1;

        /// A texel index that wraps for negative and oversized coordinates alike.
        ///
        /// `%` ALONE IS NOT ENOUGH and that is the whole reason this is a function: C's remainder keeps the
        /// sign of its left operand, so `-1 % 512` is `-1` and indexing with it walks off the front of the
        /// vector. The painting tiles the world, so a negative world coordinate is not an error — it is
        /// ordinary, and it is what every camera west or south of the origin produces.
        uint32_t WrapTexel( int32_t index, uint32_t resolution )
        {
            const int32_t side = static_cast<int32_t>( resolution );
            int32_t       m    = index % side;
            if ( m < 0 )
                m += side;
            return static_cast<uint32_t>( m );
        }

        /// Bilinear over a wrapping table, given a fetch for one texel. One implementation, two tables:
        /// a pattern channel and the mask filter identically, and writing the filter twice is how the two
        /// come to disagree about half a texel.
        template <typename FetchFn>
        float BilinearWrapped( const glm::vec2& uv, uint32_t resolution, FetchFn fetch )
        {
            // HALF A TEXEL, and it is not decoration. `uv * resolution` puts u = 0 on the CENTRE of texel 0
            // only if the half-texel is subtracted first; without it the painting is displaced by half a
            // texel against the world, which at one repeat over 48 km is 47 metres of drift between what
            // the artist painted and where the cloud lands.
            const float x = uv.x * static_cast<float>( resolution ) - 0.5f;
            const float y = uv.y * static_cast<float>( resolution ) - 0.5f;

            const float x0f = std::floor( x );
            const float y0f = std::floor( y );

            const float fx = x - x0f;
            const float fy = y - y0f;

            const uint32_t x0 = WrapTexel( static_cast<int32_t>( x0f ), resolution );
            const uint32_t y0 = WrapTexel( static_cast<int32_t>( y0f ), resolution );
            const uint32_t x1 = WrapTexel( static_cast<int32_t>( x0f ) + 1, resolution );
            const uint32_t y1 = WrapTexel( static_cast<int32_t>( y0f ) + 1, resolution );

            const float v00 = fetch( x0, y0 );
            const float v10 = fetch( x1, y0 );
            const float v01 = fetch( x0, y1 );
            const float v11 = fetch( x1, y1 );

            const float top    = v00 + ( v10 - v00 ) * fx;
            const float bottom = v01 + ( v11 - v01 ) * fx;
            return top + ( bottom - top ) * fy;
        }
    } // namespace

    bool CloudLayoutPlacementEqual( const CloudLayoutPlacement& a, const CloudLayoutPlacement& b )
    {
        // WRITTEN OUT FIELD BY FIELD rather than defaulted, and it is the same safe direction
        // CloudProceduralParamsEqual takes: a defaulted comparison silently starts comparing whatever
        // somebody adds, which sounds right until the added field is one the bake does not read — and then
        // every frame re-bakes. Comparing named fields means a new one has to be considered.
        return a.RepeatsPerRegion == b.RepeatsPerRegion && a.QuarterTurns == b.QuarterTurns &&
               a.OffsetKm == b.OffsetKm && a.PatternStrength == b.PatternStrength &&
               a.MaskStrength == b.MaskStrength;
    }

    Common::BoolResultStr ValidateCloudLayoutPlacement( const CloudLayoutPlacement& placement )
    {
        // THE FLOOR IS ONE AND THE CEILING IS THE LATTICE. Zero repeats is a division by zero dressed as a
        // setting; above sixteen the painting's period at the shipped 48 km region is finer than the 3 km
        // placement cell, so a texel can no longer decide a cell and the painting reads as noise on the
        // coverage rather than as a shape.
        if ( placement.RepeatsPerRegion < 1u || placement.RepeatsPerRegion > 16u )
            return Common::MakeFormattedError<bool>( "Layout Repeats must lie in [1, 16], got {}",
                                                     placement.RepeatsPerRegion );

        if ( placement.QuarterTurns > 3u )
            return Common::MakeFormattedError<bool>(
                 "Layout Rotation is a count of QUARTER TURNS and must lie in [0, 3], got {}; a free angle "
                 "would break the modelling volume's periodicity, which is what the far field is",
                 placement.QuarterTurns );

        if ( !std::isfinite( placement.OffsetKm.x ) || !std::isfinite( placement.OffsetKm.y ) )
            return Common::MakeFormattedError<bool>( "Layout Offset must be finite, got ({}, {})",
                                                     placement.OffsetKm.x, placement.OffsetKm.y );

        if ( !std::isfinite( placement.PatternStrength ) || placement.PatternStrength < 0.0f ||
             placement.PatternStrength > 1.0f )
            return Common::MakeFormattedError<bool>( "Layout Pattern Strength must lie in [0, 1], got {}",
                                                     placement.PatternStrength );

        if ( !std::isfinite( placement.MaskStrength ) || placement.MaskStrength < 0.0f ||
             placement.MaskStrength > 1.0f )
            return Common::MakeFormattedError<bool>( "Layout Mask Strength must lie in [0, 1], got {}",
                                                     placement.MaskStrength );

        return Common::MakeSuccess( true );
    }

    Common::BoolResultStr ValidateCloudLayoutData( const CloudLayoutData& data )
    {
        if ( data.Resolution < kCloudLayoutMinResolution || data.Resolution > kCloudLayoutMaxResolution )
            return Common::MakeFormattedError<bool>( "layout resolution must lie in [{}, {}], got {}",
                                                     kCloudLayoutMinResolution, kCloudLayoutMaxResolution,
                                                     data.Resolution );

        // A LAYOUT THAT CARRIES NEITHER TABLE IS REFUSED rather than tolerated. It would occupy the slot,
        // pass every other check and change nothing at all — which is a dead setting reached from the far
        // side, and it is indistinguishable from the slot being empty except that the artist believes it is
        // not.
        if ( !data.HasPattern() && !data.HasMask() )
            return Common::MakeFormattedError<bool>(
                 "layout carries neither a pattern nor a mask, so binding it could not change one cell" );

        const uint64_t texels = static_cast<uint64_t>( data.Resolution ) * data.Resolution;

        if ( data.HasPattern() && data.Pattern.size() != texels * kPatternBytesPerTexel )
            return Common::MakeFormattedError<bool>( "pattern is {} bytes, expected {} for {}x{} RGBA8",
                                                     data.Pattern.size(), texels * kPatternBytesPerTexel,
                                                     data.Resolution, data.Resolution );

        if ( data.HasMask() && data.Mask.size() != texels )
            return Common::MakeFormattedError<bool>( "mask is {} bytes, expected {} for {}x{} R8",
                                                     data.Mask.size(), texels, data.Resolution, data.Resolution );

        for ( uint32_t slot = 0; slot < kCloudLayoutChannels; ++slot )
        {
            const float mean = data.PatternMean[slot];
            if ( !std::isfinite( mean ) || mean < 0.0f || mean > 1.0f )
                return Common::MakeFormattedError<bool>(
                     "pattern mean for slot {} is {}, which is not a fraction; the zero-mean application "
                     "that keeps the Coverage slider honest cannot be performed with it",
                     slot, mean );
        }

        return Common::MakeSuccess( true );
    }

    glm::vec2 CloudLayoutUv( const CloudLayoutPlacement& placement, float regionSizeKm, const glm::vec2& worldKm )
    {
        const float region  = std::max( regionSizeKm, 1e-3f );
        const float repeats = static_cast<float>( std::max( placement.RepeatsPerRegion, 1u ) );

        const glm::vec2 shifted = worldKm - placement.OffsetKm;

        // THE RESTRICTION IS ON THE ANGLE, NOT ON THE ARITHMETIC, and a sabotage is what settled which.
        //
        // The first version of this comment claimed that writing the quarter turn as a `cos`/`sin` matrix
        // would break the periodicity by a texel at large world coordinates. It does not: replacing the
        // swap below with a float `cos`/`sin` at 90 degrees was measured at 3.8e-6 of a period against the
        // exact form's 1.9e-6, on probes out to 12 345 km, and
        // CloudPlacementSpectrum.ThePaintingRepeatsExactlyWithTheRegionAtEveryRotationAndOffset stayed
        // GREEN. The claim was wrong and is corrected rather than deleted, because the decision it was
        // offered in support of is the right one for a different reason.
        //
        // WHAT ACTUALLY BREAKS IS ANY OTHER ANGLE. A square lattice maps onto itself under a quarter turn
        // and under nothing else, so the periodicity the far field depends on is a property of the SET of
        // permitted rotations. The same sabotage at 45 degrees moves the departure to 0.414 of a period —
        // five orders of magnitude — and the test goes red at once. That is why `QuarterTurns` is a count
        // and not an angle.
        //
        // The exact swap is kept anyway: it is cheaper than two transcendentals and it is exact at every
        // magnitude, so it removes a residue that is small rather than one that was large.
        glm::vec2 turned = shifted;
        switch ( placement.QuarterTurns & 3u )
        {
            case 1:
                turned = glm::vec2( -shifted.y, shifted.x );
                break;
            case 2:
                turned = glm::vec2( -shifted.x, -shifted.y );
                break;
            case 3:
                turned = glm::vec2( shifted.y, -shifted.x );
                break;
            default:
                break;
        }

        return turned * ( repeats / region );
    }

    float SampleCloudLayoutPattern( const CloudLayoutData& data, uint32_t slot, const glm::vec2& uv )
    {
        if ( !data.HasPattern() || slot >= kCloudLayoutChannels || data.Resolution == 0u )
            return 0.0f;

        const uint32_t             resolution = data.Resolution;
        const unsigned char* const pixels     = data.Pattern.data();

        return BilinearWrapped( uv, resolution,
                                [pixels, resolution, slot]( uint32_t x, uint32_t y )
                                {
                                    const size_t index =
                                         ( static_cast<size_t>( y ) * resolution + x ) * kPatternBytesPerTexel +
                                         slot;
                                    return static_cast<float>( pixels[index] ) * ( 1.0f / 255.0f );
                                } );
    }

    float SampleCloudLayoutMask( const CloudLayoutData& data, const glm::vec2& uv )
    {
        if ( !data.HasMask() || data.Resolution == 0u )
            return 0.0f;

        const uint32_t             resolution = data.Resolution;
        const unsigned char* const pixels     = data.Mask.data();

        const float raw = BilinearWrapped( uv, resolution,
                                           [pixels, resolution]( uint32_t x, uint32_t y )
                                           {
                                               return static_cast<float>(
                                                    pixels[static_cast<size_t>( y ) * resolution + x] );
                                           } );

        // THE TWO SIDES OF NEUTRAL ARE NOT THE SAME WIDTH — 128 has 128 values below it and 127 above — so
        // the two halves are normalised separately. Dividing by one number instead would make a fully white
        // mask reach 0.992 while a fully black one reached -1.000, and "paint it white" would then be
        // measurably weaker than "paint it black" for no reason an artist could ever see.
        const float centred = raw - kMaskNeutral;
        return centred >= 0.0f ? centred / ( 255.0f - kMaskNeutral ) : centred / kMaskNeutral;
    }

    Common::ResultStr<std::vector<unsigned char>> EncodeCloudLayout( const CloudLayoutData& data )
    {
        CloudLayoutData written = data;

        // THE MEANS ARE RECOMPUTED HERE AND NOWHERE ELSE. This is the one point where they can be made to
        // agree with the pixels they describe; a mean that arrived from a caller could belong to a
        // different painting, and the symptom would be a sky whose cover drifts from its slider with
        // nothing in the file to say why.
        for ( uint32_t slot = 0; slot < kCloudLayoutChannels; ++slot )
            written.PatternMean[slot] = 0.0f;

        if ( written.HasPattern() )
        {
            const uint64_t texels = static_cast<uint64_t>( written.Resolution ) * written.Resolution;
            if ( texels != 0u && written.Pattern.size() == texels * kPatternBytesPerTexel )
            {
                // Summed in double. At 1024 squared a float accumulator has already lost the low bits of
                // the sum by the time it reaches a million terms, and the error lands directly in the
                // number that keeps the Coverage mapping honest.
                double sums[kCloudLayoutChannels] = { 0.0, 0.0, 0.0, 0.0 };
                for ( uint64_t t = 0; t < texels; ++t )
                    for ( uint32_t slot = 0; slot < kCloudLayoutChannels; ++slot )
                        sums[slot] += static_cast<double>(
                             written.Pattern[static_cast<size_t>( t ) * kPatternBytesPerTexel + slot] );

                for ( uint32_t slot = 0; slot < kCloudLayoutChannels; ++slot )
                    written.PatternMean[slot] =
                         static_cast<float>( sums[slot] / ( static_cast<double>( texels ) * 255.0 ) );
            }
        }

        if ( auto valid = ValidateCloudLayoutData( written ); !valid )
            return Common::MakeFormattedError<std::vector<unsigned char>>( "cannot encode layout: {}",
                                                                           valid.GetError() );

        std::vector<unsigned char> payload;
        payload.reserve( written.Pattern.size() + written.Mask.size() );
        payload.insert( payload.end(), written.Pattern.begin(), written.Pattern.end() );
        payload.insert( payload.end(), written.Mask.begin(), written.Mask.end() );

        uint32_t flags = 0u;
        if ( written.HasPattern() )
            flags |= kFlagHasPattern;
        if ( written.HasMask() )
            flags |= kFlagHasMask;

        std::vector<unsigned char> out;
        out.reserve( kCloudLayoutHeaderSize + payload.size() );

        out.insert( out.end(), kCloudLayoutMagic, kCloudLayoutMagic + sizeof( kCloudLayoutMagic ) );
        WriteU32( out, kCloudLayoutContainerVersion );
        WriteU32( out, written.Resolution );
        WriteU32( out, flags );

        for ( uint32_t slot = 0; slot < kCloudLayoutChannels; ++slot )
            WriteF32( out, written.PatternMean[slot] );

        WriteU64( out, static_cast<uint64_t>( payload.size() ) );
        WriteU32( out, Crc32( payload.data(), payload.size() ) );

        // Four bytes of nothing so the header is a round 48 and the payload starts on an eight-byte
        // boundary. Named rather than left implicit, because a reader that computed the offset from the
        // fields above would drift the moment one of them moved.
        WriteU32( out, 0u );

        out.insert( out.end(), payload.begin(), payload.end() );
        return Common::MakeSuccess( std::move( out ) );
    }

    Common::ResultStr<CloudLayoutData> DecodeCloudLayout( const std::vector<unsigned char>& bytes )
    {
        if ( bytes.size() < kCloudLayoutHeaderSize )
            return Common::MakeFormattedError<CloudLayoutData>(
                 "file is {} bytes, shorter than the {}-byte header", bytes.size(), kCloudLayoutHeaderSize );

        if ( std::memcmp( bytes.data(), kCloudLayoutMagic, sizeof( kCloudLayoutMagic ) ) != 0 )
            return Common::MakeFormattedError<CloudLayoutData>(
                 "not a cloud layout: the first four bytes are {:02x} {:02x} {:02x} {:02x}, expected 'DCLY'",
                 bytes[0], bytes[1], bytes[2], bytes[3] );

        const unsigned char* at = bytes.data() + sizeof( kCloudLayoutMagic );

        const uint32_t version = ReadU32( at );
        at += 4;
        if ( version != kCloudLayoutContainerVersion )
            return Common::MakeFormattedError<CloudLayoutData>(
                 "container version {} is not the {} this build reads", version, kCloudLayoutContainerVersion );

        CloudLayoutData data;
        data.Resolution = ReadU32( at );
        at += 4;

        const uint32_t flags = ReadU32( at );
        at += 4;

        for ( uint32_t slot = 0; slot < kCloudLayoutChannels; ++slot )
        {
            data.PatternMean[slot] = ReadF32( at );
            at += 4;
        }

        const uint64_t payloadBytes = ReadU64( at );
        at += 8;

        const uint32_t storedCrc = ReadU32( at );
        at += 4;

        if ( data.Resolution < kCloudLayoutMinResolution || data.Resolution > kCloudLayoutMaxResolution )
            return Common::MakeFormattedError<CloudLayoutData>(
                 "layout resolution {} lies outside [{}, {}]", data.Resolution, kCloudLayoutMinResolution,
                 kCloudLayoutMaxResolution );

        const uint64_t texels = static_cast<uint64_t>( data.Resolution ) * data.Resolution;

        const bool hasPattern = ( flags & kFlagHasPattern ) != 0u;
        const bool hasMask    = ( flags & kFlagHasMask ) != 0u;

        const uint64_t expected =
             ( hasPattern ? texels * kPatternBytesPerTexel : 0u ) + ( hasMask ? texels : 0u );

        if ( payloadBytes != expected )
            return Common::MakeFormattedError<CloudLayoutData>(
                 "header says {} payload bytes but {}x{} with pattern={} mask={} needs {}", payloadBytes,
                 data.Resolution, data.Resolution, hasPattern, hasMask, expected );

        if ( bytes.size() != kCloudLayoutHeaderSize + payloadBytes )
            return Common::MakeFormattedError<CloudLayoutData>( "file is {} bytes, expected {} for its header",
                                                                bytes.size(),
                                                                kCloudLayoutHeaderSize + payloadBytes );

        const unsigned char* payload = bytes.data() + kCloudLayoutHeaderSize;

        const uint32_t actualCrc = Crc32( payload, static_cast<size_t>( payloadBytes ) );
        if ( actualCrc != storedCrc )
            return Common::MakeFormattedError<CloudLayoutData>(
                 "payload checksum is {:08x} but the header says {:08x}; the file is corrupt, and a corrupt "
                 "layout renders as a sky with the wrong clouds in it rather than as an error",
                 actualCrc, storedCrc );

        if ( hasPattern )
        {
            const size_t count = static_cast<size_t>( texels * kPatternBytesPerTexel );
            data.Pattern.assign( payload, payload + count );
            payload += count;
        }

        if ( hasMask )
            data.Mask.assign( payload, payload + static_cast<size_t>( texels ) );

        data.ContentHash = storedCrc;

        if ( auto valid = ValidateCloudLayoutData( data ); !valid )
            return Common::MakeFormattedError<CloudLayoutData>( "layout decoded but is unusable: {}",
                                                                valid.GetError() );

        return Common::MakeSuccess( std::move( data ) );
    }

    Common::ResultStr<CloudLayoutData> MakeCloudLayoutFromImage( const std::vector<unsigned char>& pixels,
                                                                 uint32_t width, uint32_t height,
                                                                 const uint32_t channelForSlot[kCloudLayoutChannels],
                                                                 bool takeMask )
    {
        if ( width != height )
            return Common::MakeFormattedError<CloudLayoutData>(
                 "layout source is {}x{}; it must be square, because the painting tiles the world on a "
                 "square period and resampling it would be an opinion about the artist's image",
                 width, height );

        if ( width < kCloudLayoutMinResolution || width > kCloudLayoutMaxResolution )
            return Common::MakeFormattedError<CloudLayoutData>( "layout source is {}x{}; the side must lie in "
                                                                "[{}, {}]",
                                                                width, height, kCloudLayoutMinResolution,
                                                                kCloudLayoutMaxResolution );

        const uint64_t texels = static_cast<uint64_t>( width ) * height;
        if ( pixels.size() != texels * kPatternBytesPerTexel )
            return Common::MakeFormattedError<CloudLayoutData>( "layout source is {} bytes, expected {} for "
                                                                "{}x{} RGBA8",
                                                                pixels.size(), texels * kPatternBytesPerTexel,
                                                                width, height );

        for ( uint32_t slot = 0; slot < kCloudLayoutChannels; ++slot )
            if ( channelForSlot[slot] >= kPatternBytesPerTexel )
                return Common::MakeFormattedError<CloudLayoutData>(
                     "slot {} is mapped to source channel {}, and an RGBA image has four", slot,
                     channelForSlot[slot] );

        CloudLayoutData data;
        data.Resolution = width;

        data.Pattern.resize( static_cast<size_t>( texels ) * kPatternBytesPerTexel );
        for ( uint64_t t = 0; t < texels; ++t )
            for ( uint32_t slot = 0; slot < kCloudLayoutChannels; ++slot )
                data.Pattern[static_cast<size_t>( t ) * kPatternBytesPerTexel + slot] =
                     pixels[static_cast<size_t>( t ) * kPatternBytesPerTexel + channelForSlot[slot]];

        if ( takeMask )
        {
            data.Mask.resize( static_cast<size_t>( texels ) );
            for ( uint64_t t = 0; t < texels; ++t )
                data.Mask[static_cast<size_t>( t )] =
                     pixels[static_cast<size_t>( t ) * kPatternBytesPerTexel + 3u];
        }

        // Round-tripped rather than returned raw, so that the means and the content hash a caller receives
        // are the ones the FILE will carry. Two paths to a CloudLayoutData — one through the encoder and
        // one around it — is how the mean in memory comes to differ from the mean on disk.
        auto encoded = EncodeCloudLayout( data );
        if ( !encoded )
            return Common::MakeFormattedError<CloudLayoutData>( "layout built from the image is unusable: {}",
                                                                encoded.GetError() );

        return DecodeCloudLayout( encoded.GetValue() );
    }
} // namespace Desert::Assets
