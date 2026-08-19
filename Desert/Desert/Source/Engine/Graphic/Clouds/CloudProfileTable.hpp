#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Desert::Graphic
{
    /**
     * THE VERTICAL PROFILE IS A TWO-DIMENSIONAL TABLE, and the parametric curve below is its GENERATOR
     * rather than its storage. Decision D-13 (Docs/Clouds/ANALYSIS_APPROACH.md §7).
     *
     * WHY A TABLE AND NOT A CURVE. Unreal's profile is a texture sampled at
     * `float2( NormAltitudeInLayer, layoutValue )` — one axis altitude, the other THE VALUE OF THE
     * PLACEMENT PATTERN AT THAT POINT OF THE SKY — and Epic's own description of the channel is "the
     * profile shape *and relative altitude* of a different cloud type"
     * (Docs/Clouds/UEReference/Documentation/ShapeModel.md §3, L2320). A curve `f(h)` cannot express
     * either half of that:
     *
     *   * it gives every cloud of a type the same height wherever it is in the sky, so a type is flat at
     *     the rim of a patch and flat at its core, when the whole point of a cumulus field is that the
     *     thick middle of a patch is where the tower is;
     *   * it is single-humped by construction, so a cumulonimbus ANVIL — a second lobe of cloud at the
     *     tropopause, spreading far wider than the tower that fed it — is not reachable by any choice of
     *     ramp constants. `baseRamp * topRamp` has one maximum, always.
     *
     * WHY THE GENERATOR SURVIVES, AND WHERE ITS INPUT NOW COMES FROM. T0 kept eleven numbers per species
     * in a table compiled into this header, because it had no asset to read them from. T1 gave it one:
     * the numbers are the payload of a `.decloudtype` (Engine/Assets/CloudTypeData.hpp), the artist edits
     * them in the Cloud Type panel, and this file kept exactly the half that is MATHS. The runtime did not
     * change at all — it already read a table.
     *
     * THIS HEADER IS DEPENDENCY-FREE ON PURPOSE. It is included by the asset layer (which owns the
     * numbers), by CloudPayload.hpp (which computes the shell from a type's absolute altitudes) and by
     * Desert/Tests/Engine/CloudField (which asserts that what the generator writes is what the shader
     * reads). Anything heavier here would drag the renderer into a test that links nothing.
     *
     * UNITS: KILOMETRES, and they are ABSOLUTE altitudes above the ground rather than fractions of a
     * layer. That is the §5.1 anchor in its strongest form — a set of ratios is satisfied at any absolute
     * scale, so a layer that has drifted into the stratosphere fails no test unless some number is
     * pinned in metres. These are those numbers, and Desert/Tests/Engine/CloudType pins the SHIPPED
     * LIBRARY against meteorology — the library being content now, the anchor moved to where the content
     * is rather than staying on a table that no longer exists.
     */

    /**
     * Everything the generator needs to draw one cloud type's family of curves, plus the three factors
     * that describe what the type is MADE OF rather than what shape it is.
     *
     * The pattern axis is what makes the profile a FAMILY. `EdgeTopFraction` is how much of its full
     * height the type reaches where the placement pattern has only just crossed into cloud; at the core
     * of a patch it reaches all of it. A stratus is near 1 — a sheet is a sheet everywhere — and a
     * congestus is near a tenth, which is the difference between a flat pad at the rim of a patch and a
     * tower in its middle.
     *
     * THE THREE FACTORS ARE MULTIPLIERS ON THE ARTIST'S OWN SCALES, not second copies of them. The
     * component carries Density Scale, Extinction Scale and Detail Strength for the LAYER; a type carries
     * how much of each it is relative to a cumulus. The product is formed once, in PackCloudParams, so
     * "1" on the component keeps meaning "this type as it is" whichever type is in the slot. Two absolute
     * values for one quantity would be two numbers that can disagree — the defect class §2.3.1 of the
     * contract is about.
     */
    struct CloudTypeShape
    {
        float BaseAltitudeKm;   // where the cloud base sits — the lifting condensation level of the type
        float TopAltitudeKm;    // the top it reaches at the CORE of a placement patch
        float EdgeTopFraction;  // fraction of (Top - Base) it reaches where the patch has just begun
        float BaseRampFraction; // fraction of the cloud's own height the base ramp takes to reach full
        float TopTaper;         // fraction of the cloud's own height over which the top melts away
        float AnvilAltitudeKm;  // centre of the SECOND lobe; zero strength means the type has none
        float AnvilThicknessKm; // half-height of that lobe
        float AnvilStrength;    // how dense the anvil is against the tower that feeds it
        float DetailCharacter;  // 0 = wispy erosion, 1 = billowy — the type's edge, not its silhouette
        float DetailFactor;     // multiplies the layer's Detail Strength: how deeply the erosion cuts
        float DensityFactor;    // multiplies the layer's Density Scale: how much matter this type is
        float ExtinctionFactor; // multiplies the layer's Extinction Scale: how opaque that matter is
    };

    // The bottom and the top of the shell a layer of this type needs. The ANVIL is above the tower, so
    // the top is not simply TopAltitudeKm — a type whose second lobe is outside the shell would have that
    // lobe silently cut off by the layer geometry, which is the "sky was a ceiling" defect in a new
    // costume (commit 54330ab9).
    inline float CloudTypeBaseKm( const CloudTypeShape& shape )
    {
        return shape.BaseAltitudeKm;
    }

    inline float CloudTypeTopKm( const CloudTypeShape& shape )
    {
        const float anvilTop = shape.AnvilStrength > 0.0f ? shape.AnvilAltitudeKm + shape.AnvilThicknessKm : 0.0f;
        return std::max( shape.TopAltitudeKm, anvilTop );
    }

    /**
     * The generator: what fraction of a cloud's body sits at @p altitudeKm when the placement pattern at
     * this point of the sky reads @p pattern.
     *
     * PURE, and in absolute kilometres rather than in fractions of anything, so the meteorological anchor
     * is testable directly on this function without a layer in the picture.
     *
     * The two ends are deliberately asymmetric because real cloud is: a cumulus base sits flat on the
     * lifting condensation level, while the top tapers over half the cloud's height because that is where
     * the rising parcel is losing against the air around it. A symmetric profile — the obvious thing to
     * write — gives every cloud a rounded bottom, which reads as fog lying in the air. A lenticular is the
     * ONE type in the library that wants the symmetric answer, and it gets it by authoring a base ramp as
     * long as its taper rather than by a second code path.
     */
    inline float CloudProfileCurve( const CloudTypeShape& shape, float altitudeKm, float pattern )
    {
        const float p = std::clamp( pattern, 0.0f, 1.0f );

        const float span = shape.TopAltitudeKm - shape.BaseAltitudeKm;
        if ( span <= 0.0f )
            return 0.0f;

        // THE HEIGHT THIS COLUMN REACHES, and the whole reason the profile is two-dimensional. At the rim
        // of a patch the type gets EdgeTopFraction of its span; at the core it gets all of it.
        const float top =
             shape.BaseAltitudeKm + span * ( shape.EdgeTopFraction + ( 1.0f - shape.EdgeTopFraction ) * p );

        float core = 0.0f;
        if ( top > shape.BaseAltitudeKm )
        {
            const float h = ( altitudeKm - shape.BaseAltitudeKm ) / ( top - shape.BaseAltitudeKm );
            if ( h > 0.0f && h < 1.0f )
            {
                const float baseRamp = std::min( h / std::max( shape.BaseRampFraction, 1e-3f ), 1.0f );
                const float topRamp  = std::min( ( 1.0f - h ) / std::max( shape.TopTaper, 1e-3f ), 1.0f );
                core                 = baseRamp * topRamp;
            }
        }

        // THE SECOND LOBE. Its altitude does not move with the pattern — an anvil is pinned to the
        // tropopause, not to how thick the patch under it is — so it appears at a FIXED height and only
        // its presence grows with the pattern. Below the band where the tower is tall enough to feed one
        // there is no anvil at all; above it the anvil is a shelf that reaches far outside the tower, and
        // between the two the profile has a genuine gap. That gap is what no single-humped curve has.
        float anvil = 0.0f;
        if ( shape.AnvilStrength > 0.0f && shape.AnvilThicknessKm > 0.0f )
        {
            const float distance = std::abs( altitudeKm - shape.AnvilAltitudeKm ) / shape.AnvilThicknessKm;
            const float lobe     = std::max( 1.0f - distance * distance, 0.0f );

            const float t        = std::clamp( ( p - 0.30f ) / 0.55f, 0.0f, 1.0f );
            const float presence = t * t * ( 3.0f - 2.0f * t );

            anvil = shape.AnvilStrength * lobe * presence;
        }

        // MAX AND NOT A SUM, for the same reason the seam joins its two producers with a max
        // (ANALYSIS_APPROACH.md §4.3, confirmed against the graph at ShapeModel.md L2007): adding two
        // overlapping bodies makes the overlap denser than either, which is exactly the wrong answer
        // where a tower passes through its own anvil.
        return std::clamp( std::max( core, anvil ), 0.0f, 1.0f );
    }

    // The table's own resolution.
    //
    // 256 ALONG THE ALTITUDE AXIS is Unreal's (T_Profile_08 is 256 x 256, textures_3.png), and against a
    // shell that is the TYPE'S OWN span rather than a fixed ten kilometres it is a fine grid: the
    // thinnest type in the library, a 400 m stratus, still gets all 256 rows of it.
    //
    // 64 ALONG THE PATTERN AXIS rather than 256. The pattern axis carries how the shape morphs from the
    // rim of a patch to its core, and that morph is a two-point interpolation plus one smoothstep — it
    // has no detail at the sixteenth of a per cent 256 columns would resolve, and a quarter of the memory
    // is a quarter of the memory.
    inline constexpr uint32_t kCloudProfileTableAltitudeTexels = 256;
    inline constexpr uint32_t kCloudProfileTablePatternTexels  = 64;

    // RGBA32F because it is the only float format Core::Formats::ImageFormat offers that this engine can
    // upload from a std::vector<float> without a half-precision conversion of our own. The profile lives
    // in .r; .g, .b and .a are written zero and nothing reads them. That is a property of the FORMAT and
    // not a reservation — there is no single-channel image format in this engine, exactly as there is no
    // two-channel one for the march's depth guide, and the same note stands beside that image.
    inline constexpr uint32_t kCloudProfileTableChannels = 4;

    inline constexpr uint32_t kCloudProfileTableFloats =
         kCloudProfileTableAltitudeTexels * kCloudProfileTablePatternTexels * kCloudProfileTableChannels;

    /**
     * Evaluate the generator into the texels of the table the march samples.
     *
     * THE AXES ARE THE SHADER'S, EXACTLY. Texel (i, j) holds the profile at
     * altitude = envelopeBottom + (i + 0.5) / 256 * envelopeSpan and pattern = (j + 0.5) / 64, which is
     * the position a linear sampler reports for texture coordinate ((i + 0.5) / 256, (j + 0.5) / 64).
     * Getting that half-texel wrong shifts every cloud base by 20 m and is invisible in a frame;
     * Desert/Tests/Engine/CloudField compares this function against the shader's read of its output and
     * turns the half-texel into a failing test.
     *
     * The envelope is the type's own [base, top] — see CloudTypeBaseKm / CloudTypeTopKm — so the altitude
     * axis is spent entirely on the cloud rather than on the empty air a fixed ceiling would put above a
     * stratus.
     */
    inline std::vector<float> CloudBuildProfileTable( const CloudTypeShape& shape )
    {
        const float bottomKm = CloudTypeBaseKm( shape );
        const float spanKm   = std::max( CloudTypeTopKm( shape ) - bottomKm, 1e-3f );

        std::vector<float> texels( kCloudProfileTableFloats, 0.0f );

        for ( uint32_t j = 0; j < kCloudProfileTablePatternTexels; ++j )
        {
            const float pattern =
                 ( static_cast<float>( j ) + 0.5f ) / static_cast<float>( kCloudProfileTablePatternTexels );

            for ( uint32_t i = 0; i < kCloudProfileTableAltitudeTexels; ++i )
            {
                const float fraction =
                     ( static_cast<float>( i ) + 0.5f ) / static_cast<float>( kCloudProfileTableAltitudeTexels );
                const float altitudeKm = bottomKm + fraction * spanKm;

                const size_t texel = ( static_cast<size_t>( j ) * kCloudProfileTableAltitudeTexels + i ) *
                                     kCloudProfileTableChannels;
                texels[texel] = CloudProfileCurve( shape, altitudeKm, pattern );
            }
        }

        return texels;
    }
} // namespace Desert::Graphic
