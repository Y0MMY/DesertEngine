#pragma once

#include <Engine/ECS/VolumetricCloudsComponent.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Desert::Graphic
{
    /**
     * The CLOUD TYPE axis: authored vertical-profile curves, baked into a small 2D lookup table.
     *
     * WHY THIS EXISTS. Cloud Type used to blend three fixed trapezoids — stratus, stratocumulus,
     * cumulus — and a trapezoid product `pow(baseIn, p) * pow(topOut, q)` is monotone up times monotone
     * down. Every value of the knob therefore produced the SAME cloud at a different height: the four
     * remap heights moved and nothing else could. That is precisely what "I drag Cloud Type and the
     * cloud only stretches upward" is, and it is a property of the parameterisation, not of the tuning.
     *
     * Nubis3 p. 19 writes the vertical profile as
     *
     *     vertical_profile = CloudTopType( h, top_type ) * CloudBottomType( h, bottom_type )
     *
     * where the two Type terms are columns of AUTHORED 2D textures — arbitrary curves, so a step along
     * the type axis can turn a smooth top-out into a stepped billowy one, or leave a stalk under a
     * flaring head. This header is that pair of textures: one RGBA8 lookup of (height x type) whose
     * rows are the anchor forms below, generated on the CPU because the curves are authored data and a
     * 3 KiB table costs less to upload once than a compute pass costs to write.
     *
     * THE STRICT-SUBSET PROPERTY. Three of the six rows are exactly the component's existing
     * trapezoids, with a form term of 1. Everything the old blend could express, the table still
     * expresses, bit for bit up to 8-bit quantisation — which is what makes this a widening of the
     * knob rather than a retuning of it, and what CloudMath asserts.
     *
     * WHAT THE SHADER DOES WITH IT. Common/CloudProfile.glslh, which owns the sampling convention:
     * texel centres in both axes (so the endpoints are the curves' own endpoints and neither axis
     * depends on the sampler's addressing mode) and the same
     * `pow(bottom, BaseGradientPower) * pow(top, TopGradientPower) * form` the builder assumes.
     */

    // Height taps per row. Piecewise-linear curves sampled at N points and linearly reconstructed err by
    // at most (slope x tap spacing) / 4 at a knee, so the TAP COUNT IS SET BY THE SHARPEST AUTHORED KNEE
    // and has to move whenever that does.
    //
    // It did. The base-in ramps are now authored from the length a condensation level justifies rather
    // than from a shape (see VolumetricCloudsComponent.hpp): the shelf's is 0.03 of the layer and the
    // cumulus's 0.05, against the 0.08 that set this constant when it was 128. At 128 taps a 0.05 ramp
    // reconstructs 0.066 low at its knee — more than twice the 0.03 CloudMath asserts the strict-subset
    // property with, and the assertion caught it. At 512 the same knee errs 0.010 and the shelf's 0.016,
    // both inside it.
    //
    // The cost is 12 KiB per layer instead of 3, on a table baked once per weather change. A profile
    // reconstructed a knee's worth low is a cloud base that fades in over the ramp it was authored to
    // end, which is the whole defect this width exists to avoid.
    inline constexpr uint32_t kCloudProfileLutWidth = 512;

    // Anchor forms along the type axis, in order of convective development. Cloud Type 0..1 maps
    // linearly onto row 0..5 and the sampler interpolates between neighbours, so the axis reads
    // scud -> shelf -> stratocumulus -> cumulus -> congestus -> anvil.
    inline constexpr uint32_t kCloudProfileLutTypes = 6;

    enum class CloudProfileForm : uint32_t
    {
        Stratus       = 0, // the component's Stratus Gradient, unchanged
        Shelf         = 1, // hard flat base, solid slab, lighter turret above
        Stratocumulus = 2, // the component's Stratocumulus Gradient, unchanged
        Cumulus       = 3, // the component's Cumulus Gradient, unchanged
        Congestus     = 4, // cauliflower: full through the middle, tapering both ways
        Anvil         = 5, // a foot, a pinched stalk, a head that flares against the ceiling
    };

    /**
     * One row of the table: the trapezoid the row starts from and the FORM term that bends it into a
     * shape a trapezoid cannot reach.
     *
     * Form is (centre, half-width, amount) in normalized layer height. The term is
     * `1 + amount * raised-cosine(h; centre, half-width)`, so amount 0 leaves the trapezoid exactly as
     * it is, a negative amount pinches a waist into it and a positive one thickens a belly. A waist is
     * the whole reason the term exists: a profile that goes low and then high again is NOT a product of
     * a monotone rise and a monotone fall, and no trapezoid pair can produce one.
     */
    struct CloudProfileAnchor
    {
        glm::vec4 Gradient{ 0.0f, 0.0f, 1.0f, 1.0f };
        glm::vec3 Form{ 0.0f, 0.0f, 0.0f };
    };

    /** GLSL's smoothstep-free remap, the same one Common/CloudGeometry.glslh's CloudRemapRange is. */
    inline float CloudProfileRemap( float value, float inLow, float inHigh, float outLow, float outHigh )
    {
        const float t = ( value - inLow ) / glm::max( inHigh - inLow, 1e-6f );
        return outLow + glm::clamp( t, 0.0f, 1.0f ) * ( outHigh - outLow );
    }

    /**
     * The form term at @p height. A raised cosine so the bend has no corner: a corner in the profile
     * shows up as a horizontal crease across every cloud in the sky at the same altitude.
     *
     * Returns exactly 1 for a zero half-width, which is how the three legacy rows stay legacy.
     */
    inline float CloudProfileFormTerm( float height, const glm::vec3& form )
    {
        const float halfWidth = form.y;
        if ( halfWidth <= 0.0f || form.z == 0.0f )
            return 1.0f;

        const float d = glm::clamp( std::fabs( height - form.x ) / halfWidth, 0.0f, 1.0f );
        const float k = 0.5f + 0.5f * std::cos( d * 3.14159265358979f );
        return glm::clamp( 1.0f + form.z * k, 0.0f, 2.0f );
    }

    /** The bottom-shaping curve of a row: 0 below the cloud's own base, 1 once inside it. */
    inline float CloudProfileBottomCurve( float height, const glm::vec4& gradient )
    {
        return CloudProfileRemap( height, gradient.x, gradient.y, 0.0f, 1.0f );
    }

    /** The top-shaping curve of a row: 1 inside the cloud, 0 above its own ceiling. */
    inline float CloudProfileTopCurve( float height, const glm::vec4& gradient )
    {
        return CloudProfileRemap( height, gradient.z, gradient.w, 1.0f, 0.0f );
    }

    /**
     * The six rows, read off the component.
     *
     * The three legacy rows carry a zero Form deliberately and have no field for it: they ARE the
     * trapezoids the old knob blended, and giving them a bend to author would be an invitation to break
     * the one property that makes this change safe to land on existing scenes.
     */
    inline std::array<CloudProfileAnchor, kCloudProfileLutTypes>
    CloudProfileAnchors( const ECS::VolumetricCloudData& data )
    {
        std::array<CloudProfileAnchor, kCloudProfileLutTypes> anchors{};

        anchors[static_cast<uint32_t>( CloudProfileForm::Stratus )] =
             CloudProfileAnchor{ data.StratusGradient, glm::vec3( 0.0f ) };
        anchors[static_cast<uint32_t>( CloudProfileForm::Shelf )] =
             CloudProfileAnchor{ data.ShelfGradient, data.ShelfProfileForm };
        anchors[static_cast<uint32_t>( CloudProfileForm::Stratocumulus )] =
             CloudProfileAnchor{ data.StratocumulusGradient, glm::vec3( 0.0f ) };
        anchors[static_cast<uint32_t>( CloudProfileForm::Cumulus )] =
             CloudProfileAnchor{ data.CumulusGradient, glm::vec3( 0.0f ) };
        anchors[static_cast<uint32_t>( CloudProfileForm::Congestus )] =
             CloudProfileAnchor{ data.CongestusGradient, data.CongestusProfileForm };
        anchors[static_cast<uint32_t>( CloudProfileForm::Anvil )] =
             CloudProfileAnchor{ data.AnvilGradient, data.AnvilProfileForm };

        return anchors;
    }

    /**
     * Bake the table. RGBA8, @p kCloudProfileLutWidth x @p kCloudProfileLutTypes, row-major, one row
     * per anchor:
     *
     *   R  the bottom-shaping curve
     *   G  the top-shaping curve
     *   B  the form term, halved so the [0, 2] range fits an unsigned byte
     *   A  255
     *
     * The two curves are stored SEPARATELY rather than pre-multiplied because Base Gradient Power and
     * Top Gradient Power are runtime knobs: baking them in would make every drag of either slider a
     * texture re-upload, and the shader pays two `pow` calls today either way.
     *
     * Alpha carries nothing. RGBA8 is the only 8-bit format the engine creates (Core::Formats::
     * ImageFormat), the table needs three channels, and 3 KiB is not worth a format.
     */
    inline std::vector<unsigned char> BuildCloudProfileLut( const ECS::VolumetricCloudData& data )
    {
        const auto anchors = CloudProfileAnchors( data );

        std::vector<unsigned char> texels(
             static_cast<std::size_t>( kCloudProfileLutWidth ) * kCloudProfileLutTypes * 4u, 0 );

        for ( uint32_t row = 0; row < kCloudProfileLutTypes; ++row )
        {
            const CloudProfileAnchor& anchor = anchors[row];
            for ( uint32_t tap = 0; tap < kCloudProfileLutWidth; ++tap )
            {
                const float height = static_cast<float>( tap ) / static_cast<float>( kCloudProfileLutWidth - 1u );
                const float bottom = CloudProfileBottomCurve( height, anchor.Gradient );
                const float top    = CloudProfileTopCurve( height, anchor.Gradient );
                const float form   = CloudProfileFormTerm( height, anchor.Form );

                const std::size_t at = ( static_cast<std::size_t>( row ) * kCloudProfileLutWidth + tap ) * 4u;
                texels[at + 0] = static_cast<unsigned char>( glm::clamp( bottom, 0.0f, 1.0f ) * 255.0f + 0.5f );
                texels[at + 1] = static_cast<unsigned char>( glm::clamp( top, 0.0f, 1.0f ) * 255.0f + 0.5f );
                texels[at + 2] =
                     static_cast<unsigned char>( glm::clamp( form * 0.5f, 0.0f, 1.0f ) * 255.0f + 0.5f );
                texels[at + 3] = 255;
            }
        }

        return texels;
    }

} // namespace Desert::Graphic
