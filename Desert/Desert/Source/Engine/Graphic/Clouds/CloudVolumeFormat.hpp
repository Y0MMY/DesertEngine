#pragma once

#include <Common/Core/ResultStr.hpp>

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// The `.dvol` file — a baked "hero cloud" density volume (VOXEL_CLOUD_PATH.md §1, §5(d)).
//
// WHY A BINARY LAYOUT AND NOT rfl::json. Everything else this engine serialises is a handful of
// authored fields, and `rfl::json` is right for those. A volume is one million RGBA8 texels; a JSON
// document cannot hold them without base64 (a third larger, and a parse allocation the size of the
// payload), and a JSON *header* followed by a binary payload puts the payload at a variable offset,
// so nothing can seek to voxel (x,y,z) without first re-parsing the text. The header below is
// therefore a fixed 40-byte record with `static_assert`ed offsets, exactly as `Common::Utils::PakFile`
// documents `.dpak`, and the voxels follow it tightly packed.
//
//   [ u32 magic "DVOL" | u32 version | u32 w | u32 h | u32 d | u32 channelLayout      ]  40-byte header
//   [ f32 extentX | f32 extentY | f32 extentZ | f32 signedDistanceRange               ]
//   [ rgba8 * w*h*d, x fastest then y then z                                          ]  payload
//
// ENDIANNESS. Little-endian, asserted at compile time. Both targets (ARM64 macOS, x64 Windows) are
// little-endian; a byte-swapping reader would be code no configuration we ship ever executes, which
// is the kind of untested path the delivery contract exists to keep out.
//
// UNITS. World units, i.e. CENTIMETRES, throughout — `Extent*` and `SignedDistanceRange` alike. The
// design doc quotes the reference deck in metres (8 m voxels, a 1024 m hero cumulus); those numbers
// are multiplied by 100 exactly once, at the point a bake description is written, and never again.

namespace Desert::Graphic
{
    // ---- Channel semantics ------------------------------------------------------------------------
    //
    // One enumerator today, and it is still an enum rather than a comment because it is the field that
    // lets a reader REFUSE a volume whose channels mean something else, instead of sampling garbage
    // through the right-sized buffer. A second layout (a profile-only far-LOD atlas, §4.4) is the
    // obvious future one and would arrive as a second enumerator plus a version bump.
    enum class CloudVolumeChannelLayout : uint32_t
    {
        // R = Dimensional Profile   [0,1] — interior depth: 1 in the core, 0 at and outside the surface.
        //                                   NOT density (deck pp. 84/98; VOXEL_CLOUD_PATH.md §1.2).
        // G = Detail Type           [0,1] — 0 = wispy, 1 = billowy (deck p. 108).
        // B = Density Scale         [0,1] — a linear per-voxel density multiplier (deck p. 118).
        // A = Signed Distance       packed, see EncodeSignedDistance below — negative inside.
        //
        // R IS THE GATE. Deck p. 86 is `if (profile > 0) uprez else 0`, so G and B are only ever read
        // where R is non-zero — and they are deliberately left DEFINED outside the cloud rather than
        // zeroed. Zeroing them would look tidier in a slice dump and would be wrong: a trilinear tap at
        // the silhouette blends a voxel inside the surface with one outside it, and an outside voxel
        // forced to a Detail Type of 0 would drag every rim in the cloud toward the wispy end of an axis
        // the author never touched.
        ProfileTypeScaleDistance = 0,
    };

    // ---- The header -------------------------------------------------------------------------------

    // 'D','V','O','L' read little-endian.
    inline constexpr uint32_t kCloudVolumeMagic = 0x4C4F5644u;

    // Bumped whenever the meaning of any byte changes. A reader rejects anything it does not equal —
    // there is no "best effort" path, because a volume read at the wrong version is a cloud-shaped
    // artefact somewhere in the sky rather than an error anybody can trace.
    inline constexpr uint32_t kCloudVolumeVersion = 1u;

    struct CloudVolumeHeader
    {
        uint32_t Magic         = kCloudVolumeMagic;
        uint32_t Version       = kCloudVolumeVersion;
        uint32_t Width         = 0;
        uint32_t Height        = 0;
        uint32_t Depth         = 0;
        uint32_t ChannelLayout = static_cast<uint32_t>( CloudVolumeChannelLayout::ProfileTypeScaleDistance );

        // The world-space box this volume was baked to cover, in world units (cm), as a FULL extent
        // (not a half extent). It is the authoring reference size: at runtime the instance's transform
        // scales it, per the teamlead's Q2 — "the world extent a tile covers is a per-instance
        // transform". Kept in the file so a bake can be reproduced and so the voxel size (Extent/Dim)
        // can be reported without a scene.
        float ExtentX = 0.0f;
        float ExtentY = 0.0f;
        float ExtentZ = 0.0f;

        // The +/- world-unit range the `.a` channel encodes. Distances beyond it saturate.
        float SignedDistanceRange = 0.0f;
    };

    // The layout is the file format, so it is pinned rather than trusted to the compiler. Anything that
    // moves one of these breaks the build, not a frame.
    static_assert( sizeof( CloudVolumeHeader ) == 40 );
    static_assert( alignof( CloudVolumeHeader ) == 4 );
    static_assert( offsetof( CloudVolumeHeader, Magic ) == 0 );
    static_assert( offsetof( CloudVolumeHeader, Version ) == 4 );
    static_assert( offsetof( CloudVolumeHeader, Width ) == 8 );
    static_assert( offsetof( CloudVolumeHeader, Height ) == 12 );
    static_assert( offsetof( CloudVolumeHeader, Depth ) == 16 );
    static_assert( offsetof( CloudVolumeHeader, ChannelLayout ) == 20 );
    static_assert( offsetof( CloudVolumeHeader, ExtentX ) == 24 );
    static_assert( offsetof( CloudVolumeHeader, ExtentY ) == 28 );
    static_assert( offsetof( CloudVolumeHeader, ExtentZ ) == 32 );
    static_assert( offsetof( CloudVolumeHeader, SignedDistanceRange ) == 36 );
    static_assert( std::endian::native == std::endian::little,
                   "The .dvol layout is little-endian; a big-endian target needs an explicit swapping "
                   "reader, not a silent reinterpretation." );

    inline constexpr uint32_t kCloudVolumeChannels    = 4; // RGBA8
    inline constexpr size_t   kCloudVolumeHeaderBytes = sizeof( CloudVolumeHeader );

    // ---- The signed-distance channel --------------------------------------------------------------
    //
    // ENCODING, and why it is this one. The `.a` byte holds `trunc(clamp(d/range, -1, 1) * 127) + 128`,
    // so 128 is exactly zero, 1..127 is outside and 129..255 is inside, and the decoded magnitude is
    // NEVER larger than the true one:
    //
    //     |decode(encode(d))| = |trunc(n * 127)| / 127 * range  <=  |n| * range = |d|
    //
    // That inequality is the whole point. The reference deck states the failure on each side of this
    // trade outright (p. 159: "Too low = extra steps. Too High = rendering artifacts") — a marcher that
    // uses the distance as a step lower bound (`step = max(sdf, adaptive)`, p. 163) is merely SLOW when
    // the distance is under-estimated and LEAKS THROUGH SURFACES when it is over-estimated. Truncation
    // toward zero, not rounding to nearest, is what buys the guarantee.
    //
    // The cost of that choice is one quantisation step of dead band at the surface: a point less than
    // `range/127` inside decodes to exactly 0, which reads as "not inside". That band is far thinner
    // than a voxel at every scale we bake, and the Dimensional Profile is ~0 there anyway.
    //
    // Byte 0 is never produced by the encoder (the smallest it emits is 1). The decoder still clamps,
    // so a corrupted or hand-made file cannot hand back a distance outside +/- range.

    inline constexpr uint8_t kCloudVolumeDistanceZero  = 128;
    inline constexpr int32_t kCloudVolumeDistanceScale = 127;

    inline uint8_t EncodeSignedDistance( float distance, float range )
    {
        if ( !( range > 0.0f ) )
            return kCloudVolumeDistanceZero;

        float normalized = distance / range;
        normalized       = normalized < -1.0f ? -1.0f : ( normalized > 1.0f ? 1.0f : normalized );

        // Truncation toward zero: C++ float->int conversion does exactly that, for both signs.
        const int32_t quantized =
             static_cast<int32_t>( normalized * static_cast<float>( kCloudVolumeDistanceScale ) );
        return static_cast<uint8_t>( quantized + kCloudVolumeDistanceZero );
    }

    inline float DecodeSignedDistance( uint8_t encoded, float range )
    {
        int32_t quantized = static_cast<int32_t>( encoded ) - kCloudVolumeDistanceZero;
        quantized         = quantized < -kCloudVolumeDistanceScale ? -kCloudVolumeDistanceScale : quantized;
        quantized         = quantized > kCloudVolumeDistanceScale ? kCloudVolumeDistanceScale : quantized;
        return static_cast<float>( quantized ) / static_cast<float>( kCloudVolumeDistanceScale ) * range;
    }

    // ---- The in-memory volume ---------------------------------------------------------------------

    // A `.dvol` after reading, or before writing. `Voxels` is exactly Width*Height*Depth*4 bytes, x
    // fastest — the layout `Image3DSpecification::Data` wants, so an upload is a move rather than a
    // repack.
    struct CloudVolume
    {
        CloudVolumeHeader          Header{};
        std::vector<unsigned char> Voxels;
    };

    inline constexpr size_t CloudVolumeVoxelIndex( const CloudVolumeHeader& header, uint32_t x, uint32_t y,
                                                   uint32_t z )
    {
        return ( static_cast<size_t>( z ) * header.Height * header.Width +
                 static_cast<size_t>( y ) * header.Width + x ) *
               kCloudVolumeChannels;
    }

    inline constexpr uint64_t CloudVolumePayloadBytes( const CloudVolumeHeader& header )
    {
        return static_cast<uint64_t>( header.Width ) * header.Height * header.Depth * kCloudVolumeChannels;
    }

    inline constexpr uint64_t CloudVolumeFileBytes( const CloudVolumeHeader& header )
    {
        return kCloudVolumeHeaderBytes + CloudVolumePayloadBytes( header );
    }

    // ---- Validation -------------------------------------------------------------------------------
    //
    // Data errors, so `ResultStr` with the actual numbers in the message — never an exception and never
    // a quiet default (DEV_CONTRACT §1.4). Every branch below names the value it rejected, because the
    // alternative is somebody staring at an empty sky with no idea which of five things went wrong.

    inline Common::BoolResultStr ValidateCloudVolumeHeader( const CloudVolumeHeader& header )
    {
        if ( header.Magic != kCloudVolumeMagic )
            return Common::MakeFormattedError<bool>( "Not a .dvol file: magic is {:#010x}, expected {:#010x}",
                                                     header.Magic, kCloudVolumeMagic );

        if ( header.Version != kCloudVolumeVersion )
            return Common::MakeFormattedError<bool>( ".dvol version {} is not supported (this build reads {})",
                                                     header.Version, kCloudVolumeVersion );

        if ( header.ChannelLayout != static_cast<uint32_t>( CloudVolumeChannelLayout::ProfileTypeScaleDistance ) )
            return Common::MakeFormattedError<bool>(
                 ".dvol channel layout {} is not supported (this build reads {})", header.ChannelLayout,
                 static_cast<uint32_t>( CloudVolumeChannelLayout::ProfileTypeScaleDistance ) );

        if ( header.Width == 0 || header.Height == 0 || header.Depth == 0 )
            return Common::MakeFormattedError<bool>( ".dvol dimensions {}x{}x{} contain a zero", header.Width,
                                                     header.Height, header.Depth );

        if ( !( header.ExtentX > 0.0f ) || !( header.ExtentY > 0.0f ) || !( header.ExtentZ > 0.0f ) )
            return Common::MakeFormattedError<bool>(
                 ".dvol world extent {}x{}x{} world units is not strictly positive", header.ExtentX,
                 header.ExtentY, header.ExtentZ );

        if ( !( header.SignedDistanceRange > 0.0f ) )
            return Common::MakeFormattedError<bool>(
                 ".dvol signed-distance range {} world units is not strictly positive",
                 header.SignedDistanceRange );

        return Common::MakeSuccess( true );
    }

    // ---- Serialisation ----------------------------------------------------------------------------

    inline Common::ResultStr<std::vector<unsigned char>> WriteCloudVolume( const CloudVolume& volume )
    {
        const auto valid = ValidateCloudVolumeHeader( volume.Header );
        if ( !valid.IsSuccess() )
            return Common::MakeError<std::vector<unsigned char>>( valid.GetError() );

        const uint64_t expected = CloudVolumePayloadBytes( volume.Header );
        if ( volume.Voxels.size() != expected )
            return Common::MakeFormattedError<std::vector<unsigned char>>(
                 ".dvol payload is {} bytes but {}x{}x{} RGBA8 needs {}", volume.Voxels.size(),
                 volume.Header.Width, volume.Header.Height, volume.Header.Depth, expected );

        std::vector<unsigned char> bytes( static_cast<size_t>( CloudVolumeFileBytes( volume.Header ) ) );
        std::memcpy( bytes.data(), &volume.Header, kCloudVolumeHeaderBytes );
        std::memcpy( bytes.data() + kCloudVolumeHeaderBytes, volume.Voxels.data(), volume.Voxels.size() );
        return Common::MakeSuccess( std::move( bytes ) );
    }

    inline Common::ResultStr<CloudVolume> ReadCloudVolume( const unsigned char* bytes, size_t size )
    {
        if ( bytes == nullptr )
            return Common::MakeError<CloudVolume>( ".dvol read from a null buffer" );

        if ( size < kCloudVolumeHeaderBytes )
            return Common::MakeFormattedError<CloudVolume>( ".dvol is {} bytes, shorter than its {}-byte header",
                                                            size, kCloudVolumeHeaderBytes );

        CloudVolume volume;
        std::memcpy( &volume.Header, bytes, kCloudVolumeHeaderBytes );

        const auto valid = ValidateCloudVolumeHeader( volume.Header );
        if ( !valid.IsSuccess() )
            return Common::MakeError<CloudVolume>( valid.GetError() );

        const uint64_t needed = CloudVolumeFileBytes( volume.Header );
        if ( static_cast<uint64_t>( size ) != needed )
            return Common::MakeFormattedError<CloudVolume>(
                 ".dvol is {} bytes but its {}x{}x{} RGBA8 header describes {}", size, volume.Header.Width,
                 volume.Header.Height, volume.Header.Depth, needed );

        volume.Voxels.assign( bytes + kCloudVolumeHeaderBytes, bytes + size );
        return Common::MakeSuccess( std::move( volume ) );
    }
} // namespace Desert::Graphic
