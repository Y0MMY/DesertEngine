// The image-format vocabulary, tested as numbers.
//
// This exists because of a fault that could not have been caught by anything we had: GetBytesPerPixel
// handled exactly two of the five formats and ended in `return 0U;`. Every other format therefore got a
// bytes-per-pixel of 0 — silently — and CalculateImageSize multiplied that into a zero-byte staging
// buffer for a real image. Nothing failed at the point of the mistake; the corruption showed up later,
// somewhere with no visible connection to the format.
//
// The lookups are now total over ImageFormat and end in a hard stop rather than a number. Everything
// below is an exact byte count, and several cases fail outright against the old function.

#include <Engine/Core/Formats/ImageFormat.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace Formats = Desert::Core::Formats;

using Formats::CalculateImageSize;
using Formats::GetBytesPerPixel;
using Formats::GetImageAspect;
using Formats::ImageFormat;

namespace
{
    // Every enumerator of ImageFormat, once. If a format is added, this list is where the test is
    // extended — but the BUILD breaks first, in GetBytesPerPixel itself, which is the point.
    constexpr ImageFormat kAllFormats[] = { ImageFormat::RGBA8F,          ImageFormat::RGBA16F,
                                            ImageFormat::RGBA32F,         ImageFormat::BGRA8F,
                                            ImageFormat::DEPTH24STENCIL8, ImageFormat::DEPTH32F };
} // namespace

// ── Bytes per pixel: the exact size of every enumerator (CLD-96e) ─────────────────────────────────

TEST( ImageFormatBytesPerPixel, EveryEnumeratorHasItsRealSize )
{
    EXPECT_EQ( GetBytesPerPixel( ImageFormat::RGBA8F ), 4u );
    EXPECT_EQ( GetBytesPerPixel( ImageFormat::RGBA16F ), 8u );
    EXPECT_EQ( GetBytesPerPixel( ImageFormat::RGBA32F ), 16u );
    EXPECT_EQ( GetBytesPerPixel( ImageFormat::BGRA8F ), 4u );
    EXPECT_EQ( GetBytesPerPixel( ImageFormat::DEPTH24STENCIL8 ), 4u );
    EXPECT_EQ( GetBytesPerPixel( ImageFormat::DEPTH32F ), 4u );
}

// The property the deleted `return 0U;` used to violate: no declared format answers zero. A zero here
// is not a wrong number, it is an allocation of nothing for an image that exists.
TEST( ImageFormatBytesPerPixel, NoEnumeratorAnswersZero )
{
    for ( const ImageFormat format : kAllFormats )
        EXPECT_GT( GetBytesPerPixel( format ), 0u ) << "format index " << static_cast<uint32_t>( format );
}

// A half-float texel is exactly half of a 32-bit-float one — this is the whole reason RGBA16F was added,
// and it is what the per-renderer memory figures below are built on.
TEST( ImageFormatBytesPerPixel, HalfFloatIsHalfOfFullFloat )
{
    EXPECT_EQ( GetBytesPerPixel( ImageFormat::RGBA32F ), 2u * GetBytesPerPixel( ImageFormat::RGBA16F ) );
}

// ── 2D size arithmetic ────────────────────────────────────────────────────────────────────────────

TEST( ImageFormatSize, TwoDimensionalSizesAreExact )
{
    EXPECT_EQ( CalculateImageSize( 128u, 128u, ImageFormat::RGBA16F ), 131072ull );
    EXPECT_EQ( CalculateImageSize( 128u, 128u, ImageFormat::RGBA8F ), 65536ull );
    EXPECT_EQ( CalculateImageSize( 128u, 128u, ImageFormat::RGBA32F ), 262144ull );
    // The curl-noise and weather-map resources of the cloud programme.
    EXPECT_EQ( CalculateImageSize( 128u, 128u, ImageFormat::RGBA8F ), 65536ull );
    EXPECT_EQ( CalculateImageSize( 512u, 512u, ImageFormat::RGBA8F ), 1048576ull );
}

TEST( ImageFormatSize, AnEmptyExtentCostsNothing )
{
    EXPECT_EQ( CalculateImageSize( 0u, 128u, ImageFormat::RGBA16F ), 0ull );
    EXPECT_EQ( CalculateImageSize( 128u, 0u, ImageFormat::RGBA16F ), 0ull );
    EXPECT_EQ( CalculateImageSize( 128u, 128u, 0u, ImageFormat::RGBA8F ), 0ull );
}

// ── 3D size arithmetic (CLD-95) ───────────────────────────────────────────────────────────────────

TEST( ImageFormatSize, VolumeSizesAreExact )
{
    // Shape noise: 128^3 RGBA8F = 8 MiB.
    EXPECT_EQ( CalculateImageSize( 128u, 128u, 128u, ImageFormat::RGBA8F ), 8388608ull );
    // Detail noise: 32^3 RGBA8F = 128 KiB.
    EXPECT_EQ( CalculateImageSize( 32u, 32u, 32u, ImageFormat::RGBA8F ), 131072ull );
    // The same volume at 32-bit float is what we are NOT paying: 4x.
    EXPECT_EQ( CalculateImageSize( 128u, 128u, 128u, ImageFormat::RGBA32F ), 33554432ull );
}

// A volume with depth 1 is the same number of bytes as the 2D image of the same face.
TEST( ImageFormatSize, DepthOneAgreesWithTheTwoDimensionalOverload )
{
    for ( const ImageFormat format : kAllFormats )
        EXPECT_EQ( CalculateImageSize( 128u, 64u, 1u, format ), CalculateImageSize( 128u, 64u, format ) )
             << "format index " << static_cast<uint32_t>( format );
}

// 64-bit arithmetic, not 32-bit: a 2048^3 RGBA8F volume is 32 GiB, and computing it in uint32_t would
// wrap to 0 — the same silent-wrong-size failure the zero bytes-per-pixel used to cause.
TEST( ImageFormatSize, LargeVolumesDoNotWrap )
{
    EXPECT_EQ( CalculateImageSize( 2048u, 2048u, 2048u, ImageFormat::RGBA8F ), 34359738368ull );
    EXPECT_EQ( CalculateImageSize( 65536u, 65536u, ImageFormat::RGBA8F ), 17179869184ull );
}

// ── The per-SceneRenderer cost the format change buys (CLD-34) ────────────────────────────────────
//
// Three full-resolution targets (scatter + two history) at 1920x1080, and the same at half resolution.
// These are the figures quoted in the requirements; they are asserted here so a format or resolution
// change moves a number in a test instead of a memory graph.

TEST( ImageFormatSize, CloudTargetBudgetAtFullResolution )
{
    const uint64_t oneTarget = CalculateImageSize( 1920u, 1080u, ImageFormat::RGBA16F );
    EXPECT_EQ( oneTarget, 16588800ull );
    EXPECT_EQ( 3ull * oneTarget, 49766400ull ); // 47.46 MiB

    // What RGBA32F would have cost for the same three targets: 94.92 MiB.
    EXPECT_EQ( 3ull * CalculateImageSize( 1920u, 1080u, ImageFormat::RGBA32F ), 99532800ull );
}

TEST( ImageFormatSize, CloudTargetBudgetAtHalfResolution )
{
    const uint64_t oneTarget = CalculateImageSize( 960u, 540u, ImageFormat::RGBA16F );
    EXPECT_EQ( oneTarget, 4147200ull );
    EXPECT_EQ( 3ull * oneTarget, 12441600ull ); // 11.86 MiB

    // Half resolution is a quarter of the pixels, so a quarter of the bytes.
    EXPECT_EQ( 4ull * oneTarget, CalculateImageSize( 1920u, 1080u, ImageFormat::RGBA16F ) );
}

// ── Aspect selection (CLD-96c) ────────────────────────────────────────────────────────────────────
//
// This is the half of the depth-transition helper that can be checked without a device. Naming COLOR on
// a depth image, or DEPTH alone on a packed depth+stencil one, is a barrier validation error — and it is
// why the scene depth attachment could not be handed to a compute sampler at all.

TEST( ImageFormatAspect, ColourFormatsAreColourOnly )
{
    EXPECT_EQ( GetImageAspect( ImageFormat::RGBA8F ), Formats::ImageAspect_Colour );
    EXPECT_EQ( GetImageAspect( ImageFormat::RGBA16F ), Formats::ImageAspect_Colour );
    EXPECT_EQ( GetImageAspect( ImageFormat::RGBA32F ), Formats::ImageAspect_Colour );
    EXPECT_EQ( GetImageAspect( ImageFormat::BGRA8F ), Formats::ImageAspect_Colour );
}

TEST( ImageFormatAspect, PackedDepthStencilNamesBothPlanes )
{
    const uint32_t aspect = GetImageAspect( ImageFormat::DEPTH24STENCIL8 );
    EXPECT_TRUE( aspect & Formats::ImageAspect_Depth );
    EXPECT_TRUE( aspect & Formats::ImageAspect_Stencil );
    EXPECT_FALSE( aspect & Formats::ImageAspect_Colour );
}

TEST( ImageFormatAspect, DepthOnlyFormatDoesNotClaimAStencil )
{
    const uint32_t aspect = GetImageAspect( ImageFormat::DEPTH32F );
    EXPECT_TRUE( aspect & Formats::ImageAspect_Depth );
    EXPECT_FALSE( aspect & Formats::ImageAspect_Stencil );
    EXPECT_FALSE( aspect & Formats::ImageAspect_Colour );
}

// No format is aspect-less, and no format claims colour together with depth: both would produce a
// barrier Vulkan rejects.
TEST( ImageFormatAspect, EveryEnumeratorNamesExactlyOneFamily )
{
    for ( const ImageFormat format : kAllFormats )
    {
        const uint32_t aspect = GetImageAspect( format );
        EXPECT_NE( aspect, 0u ) << "format index " << static_cast<uint32_t>( format );

        const bool colour = ( aspect & Formats::ImageAspect_Colour ) != 0;
        const bool depth  = ( aspect & ( Formats::ImageAspect_Depth | Formats::ImageAspect_Stencil ) ) != 0;
        EXPECT_NE( colour, depth ) << "format index " << static_cast<uint32_t>( format );
    }
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
