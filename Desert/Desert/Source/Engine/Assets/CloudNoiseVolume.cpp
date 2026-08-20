#include "CloudNoiseVolume.hpp"

#include <Engine/Assets/ContainerBytes.hpp>

#include <cstring>

namespace Desert::Assets
{
    namespace
    {
        // The one pixel format a volume may be in. Written into the header and CHECKED on read, so a file
        // from a future build that stored half-floats is refused by name instead of being read as bytes.
        constexpr uint32_t kFormatRgba8 = 0u;

        constexpr uint32_t kBytesPerVoxel = 4u;

        // Smallest and largest volume the generator will produce. The ceiling is the memory budget:
        // 128^3 in RGBA8 is 8 MiB, and doubling the axis would be 64 MiB on its own — the whole budget
        // allowed for this subsystem (Docs/Clouds decision D-9). The floor exists because the panel bakes a
        // small volume for a fast preview, and it is 64 rather than 32 for a reason that would otherwise be
        // discovered by an artist: the DEFAULT periods (2/4/3/6) need eight voxels per cell on the coarsest
        // of them, and 32/6 is five. A preview resolution that refuses the default parameters is a bad
        // first minute with a tool.
        constexpr uint32_t kMinResolution = 64u;
        constexpr uint32_t kMaxResolution = 128u;

        // Below this, gradient and cellular noise quantise onto the voxel grid and read as a lattice rather
        // than as cloud. It is the same rule the previous compute bake stated for its finest channel, and
        // it is what bounds the periods against the resolution rather than against taste.
        constexpr float kMinVoxelsPerCell = 8.0f;

        // The byte-level primitives moved to Engine/Assets/ContainerBytes.hpp when the modelling volume
        // (`.dcmv`) needed the same six functions and the same CRC. A checksum that exists twice is a
        // checksum that can disagree with itself, and the failure it produces — one container refusing a
        // file the other accepts — is the class of defect this programme keeps paying for.

        bool IsPowerOfTwo( uint32_t value )
        {
            return value != 0u && ( value & ( value - 1u ) ) == 0u;
        }

        Common::BoolResultStr ValidatePeriod( const char* name, float period, uint32_t resolution )
        {
            if ( !( period >= 1.0f ) || period != static_cast<float>( static_cast<int>( period ) ) )
                return Common::MakeFormattedError<bool>(
                     "{} must be a whole number of lattice cells and at least 1, got {}", name, period );

            const float voxelsPerCell = static_cast<float>( resolution ) / period;
            if ( voxelsPerCell < kMinVoxelsPerCell )
                return Common::MakeFormattedError<bool>(
                     "{} = {} leaves {:.1f} voxels per cell at resolution {}; below {:.0f} the noise "
                     "quantises onto the voxel grid and reads as a lattice",
                     name, period, voxelsPerCell, resolution, kMinVoxelsPerCell );

            return Common::MakeSuccess( true );
        }
    } // namespace

    const char* CloudNoiseChannelName( CloudNoiseChannel channel )
    {
        switch ( channel )
        {
            case CloudNoiseChannel::CurlyAlligatorLowFrequency:
                return "Curly-Alligator LF (wispy, coarse)";
            case CloudNoiseChannel::CurlyAlligatorHighFrequency:
                return "Curly-Alligator HF (wispy, fine)";
            case CloudNoiseChannel::AlligatorLowFrequency:
                return "Alligator LF (billowy, coarse)";
            case CloudNoiseChannel::AlligatorHighFrequency:
                return "Alligator HF (billowy, fine)";
        }
        return "unknown";
    }

    Common::BoolResultStr ValidateCloudNoiseVolumeParams( const CloudNoiseVolumeParams& params )
    {
        if ( params.Resolution < kMinResolution || params.Resolution > kMaxResolution ||
             !IsPowerOfTwo( params.Resolution ) )
            return Common::MakeFormattedError<bool>( "Resolution must be a power of two between {} and {}, got {}",
                                                     kMinResolution, kMaxResolution, params.Resolution );

        // The upper bound is where the shear stops belonging to the cloud it came from; the lower is
        // exactly zero, which is a legal choice (a volume with no curl at all is plain inverted Alligator).
        if ( !( params.CurlStrength >= 0.0f ) || params.CurlStrength > 0.5f )
            return Common::MakeFormattedError<bool>( "Curl Strength must lie in [0, 0.5], got {}",
                                                     params.CurlStrength );

        if ( auto r = ValidatePeriod( "Wispy Period LF", params.WispyPeriodLowFrequency, params.Resolution ); !r )
            return r;
        if ( auto r = ValidatePeriod( "Wispy Period HF", params.WispyPeriodHighFrequency, params.Resolution ); !r )
            return r;
        if ( auto r = ValidatePeriod( "Billow Period LF", params.BillowPeriodLowFrequency, params.Resolution );
             !r )
            return r;
        if ( auto r = ValidatePeriod( "Billow Period HF", params.BillowPeriodHighFrequency, params.Resolution );
             !r )
            return r;

        return Common::MakeSuccess( true );
    }

    std::vector<unsigned char> EncodeCloudNoiseVolume( const CloudNoiseVolumeData& data )
    {
        std::vector<unsigned char> out;
        out.reserve( kCloudNoiseHeaderSize + data.Voxels.size() );

        out.insert( out.end(), kCloudNoiseMagic, kCloudNoiseMagic + sizeof( kCloudNoiseMagic ) );
        WriteU32( out, kCloudNoiseContainerVersion );
        WriteU32( out, data.GeneratorVersion );
        WriteU32( out, data.Params.Resolution );
        WriteU32( out, kFormatRgba8 );

        // The channel meanings are STORED, not implied. A reader that finds an order it does not know can
        // say so; a reader that assumed the order would render the wispy noise as billows and look merely
        // wrong.
        WriteU32( out, static_cast<uint32_t>( CloudNoiseChannel::CurlyAlligatorLowFrequency ) );
        WriteU32( out, static_cast<uint32_t>( CloudNoiseChannel::CurlyAlligatorHighFrequency ) );
        WriteU32( out, static_cast<uint32_t>( CloudNoiseChannel::AlligatorLowFrequency ) );
        WriteU32( out, static_cast<uint32_t>( CloudNoiseChannel::AlligatorHighFrequency ) );

        WriteU32( out, data.Params.Seed );
        WriteF32( out, data.Params.CurlStrength );
        WriteF32( out, data.Params.WispyPeriodLowFrequency );
        WriteF32( out, data.Params.WispyPeriodHighFrequency );
        WriteF32( out, data.Params.BillowPeriodLowFrequency );
        WriteF32( out, data.Params.BillowPeriodHighFrequency );

        WriteU64( out, static_cast<uint64_t>( data.Voxels.size() ) );
        WriteU32( out, Crc32( data.Voxels.data(), data.Voxels.size() ) );

        out.insert( out.end(), data.Voxels.begin(), data.Voxels.end() );
        return out;
    }

    Common::ResultStr<CloudNoiseVolumeData> DecodeCloudNoiseVolume( const std::vector<unsigned char>& bytes )
    {
        if ( bytes.size() < kCloudNoiseHeaderSize )
            return Common::MakeFormattedError<CloudNoiseVolumeData>(
                 "file is {} bytes, shorter than the {}-byte header", bytes.size(), kCloudNoiseHeaderSize );

        const unsigned char* at = bytes.data();

        if ( std::memcmp( at, kCloudNoiseMagic, sizeof( kCloudNoiseMagic ) ) != 0 )
            return Common::MakeFormattedError<CloudNoiseVolumeData>(
                 "not a cloud noise volume: magic is '{:02X}{:02X}{:02X}{:02X}', expected 'DCNV'", at[0], at[1],
                 at[2], at[3] );

        const uint32_t containerVersion = ReadU32( at + 4 );
        if ( containerVersion != kCloudNoiseContainerVersion )
            return Common::MakeFormattedError<CloudNoiseVolumeData>(
                 "container version {} is not the {} this build reads", containerVersion,
                 kCloudNoiseContainerVersion );

        CloudNoiseVolumeData data;
        data.GeneratorVersion  = ReadU32( at + 8 );
        data.Params.Resolution = ReadU32( at + 12 );
        const uint32_t format  = ReadU32( at + 16 );

        if ( format != kFormatRgba8 )
            return Common::MakeFormattedError<CloudNoiseVolumeData>(
                 "pixel format {} is not RGBA8 ({}), which is the only format a volume may be in", format,
                 kFormatRgba8 );

        for ( uint32_t channel = 0; channel < 4u; ++channel )
        {
            const uint32_t stored = ReadU32( at + 20 + channel * 4u );
            if ( stored != channel )
                return Common::MakeFormattedError<CloudNoiseVolumeData>(
                     "channel {} declares meaning {}, but this build reads volumes whose channels are in the "
                     "deck's order (Curly-Alligator LF/HF, Alligator LF/HF)",
                     channel, stored );
        }

        data.Params.Seed                      = ReadU32( at + 36 );
        data.Params.CurlStrength              = ReadF32( at + 40 );
        data.Params.WispyPeriodLowFrequency   = ReadF32( at + 44 );
        data.Params.WispyPeriodHighFrequency  = ReadF32( at + 48 );
        data.Params.BillowPeriodLowFrequency  = ReadF32( at + 52 );
        data.Params.BillowPeriodHighFrequency = ReadF32( at + 56 );

        const uint64_t payloadBytes = ReadU64( at + 60 );
        const uint32_t storedCrc    = ReadU32( at + 68 );

        // The resolution and the payload length are two statements of one fact, and the whole class of
        // defects this programme keeps meeting is two statements of one fact that disagree. Checked here,
        // once, rather than trusted into an out-of-bounds upload later.
        const uint64_t expected = static_cast<uint64_t>( data.Params.Resolution ) * data.Params.Resolution *
                                  data.Params.Resolution * kBytesPerVoxel;
        if ( payloadBytes != expected )
            return Common::MakeFormattedError<CloudNoiseVolumeData>(
                 "header says {} payload bytes but a {}^3 RGBA8 volume is {} bytes", payloadBytes,
                 data.Params.Resolution, expected );

        if ( bytes.size() - kCloudNoiseHeaderSize != payloadBytes )
            return Common::MakeFormattedError<CloudNoiseVolumeData>(
                 "file is truncated: {} payload bytes present, {} declared", bytes.size() - kCloudNoiseHeaderSize,
                 payloadBytes );

        data.Voxels.assign( bytes.begin() + kCloudNoiseHeaderSize, bytes.end() );

        const uint32_t actualCrc = Crc32( data.Voxels.data(), data.Voxels.size() );
        if ( actualCrc != storedCrc )
            return Common::MakeFormattedError<CloudNoiseVolumeData>(
                 "payload checksum {:08X} does not match the {:08X} in the header; the file is corrupt", actualCrc,
                 storedCrc );

        // Validated LAST, so a corrupt file is reported as corrupt rather than as an illegal parameter set
        // read out of its wreckage.
        if ( auto valid = ValidateCloudNoiseVolumeParams( data.Params ); !valid )
            return Common::MakeFormattedError<CloudNoiseVolumeData>( "header parameters are not usable: {}",
                                                                     valid.GetError() );

        return Common::MakeSuccess( std::move( data ) );
    }
} // namespace Desert::Assets
