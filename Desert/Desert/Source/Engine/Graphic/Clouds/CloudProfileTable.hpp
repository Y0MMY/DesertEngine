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
     *   * it gives every cloud of a species the same height wherever it is in the sky, so a species is
     *     flat at the rim of a patch and flat at its core, when the whole point of a cumulus field is
     *     that the thick middle of a patch is where the tower is;
     *   * it is single-humped by construction, so a cumulonimbus ANVIL — a second lobe of cloud at the
     *     tropopause, spreading far wider than the tower that fed it — is not reachable by any choice of
     *     ramp constants. `baseRamp * topRamp` has one maximum, always.
     *
     * WHY THE GENERATOR SURVIVES. A table nobody can author is a table nobody can ship: T0 has no asset
     * and no editor for one (that is T1), so the table has to come from somewhere on a scene that was
     * never touched by an artist. It comes from here — eleven numbers per species, evaluated into
     * 256 x 64 texels once per species change. When T1 wraps the table in an asset, THIS is what the
     * asset's authoring surface generates from, and the runtime side does not change at all: it already
     * reads a table.
     *
     * THIS HEADER IS DEPENDENCY-FREE ON PURPOSE. It is included by the ECS component (which owns the
     * species enum an artist picks), by CloudPayload.hpp (which computes the envelope from the species'
     * absolute altitudes) and by Desert/Tests/Engine/CloudField (which asserts that what the generator
     * writes is what the shader reads). Anything heavier here would drag the renderer into a test that
     * links nothing.
     *
     * UNITS: KILOMETRES, and they are ABSOLUTE altitudes above the ground rather than fractions of a
     * layer. That is the §5.1 anchor in its strongest form — a set of ratios is satisfied at any absolute
     * scale, so a layer that has drifted into the stratosphere fails no test unless some number is
     * pinned in metres. These are those numbers, and Desert/Tests/Engine/CloudField pins them against
     * meteorology.
     */

    // The species a layer can be. FOUR, and the ceiling is not taste: Epic's layout textures carry one
    // species per RGBA channel, which is the structural limit on how many can share one sky
    // (EpicDoc_CloudMaterial.md §1). T0 puts ONE of them in a layer; T3 is what puts several in one.
    //
    // Reflected: DesertHeaderTool scans every header under Engine/ for enum definitions before it parses
    // the reflected structs, so the combo in the Details panel and the integer in the .desce file both
    // come from this declaration.
    enum class CloudSpecies
    {
        Stratus,
        CumulusMediocris,
        CumulusCongestus,
        Cumulonimbus
    };

    inline constexpr uint32_t kCloudSpeciesCount = 4;

    /**
     * Everything the generator needs to draw one species' family of curves.
     *
     * The pattern axis is what makes it a FAMILY. `EdgeTopFraction` is how much of its full height the
     * species reaches where the placement pattern has only just crossed into cloud; at the core of a
     * patch it reaches all of it. A stratus is near 1 — a sheet is a sheet everywhere — and a congestus
     * is near a tenth, which is the difference between a flat pad at the rim of a patch and a tower in
     * its middle.
     */
    struct CloudSpeciesShape
    {
        float BaseAltitudeKm;   // where the cloud base sits — the lifting condensation level of the species
        float TopAltitudeKm;    // the top it reaches at the CORE of a placement patch
        float EdgeTopFraction;  // fraction of (Top - Base) it reaches where the patch has just begun
        float BaseRampFraction; // fraction of the cloud's own height the base ramp takes to reach full
        float TopTaper;         // fraction of the cloud's own height over which the top melts away
        float AnvilAltitudeKm;  // centre of the SECOND lobe; zero strength means the species has none
        float AnvilThicknessKm; // half-height of that lobe
        float AnvilStrength;    // how dense the anvil is against the tower that feeds it
        float DetailCharacter;  // 0 = wispy erosion, 1 = billowy — the species' edge, not its silhouette
        float DensityFactor;    // how much matter this species is made of, against the artist's own scale
    };

    // The library. Every altitude here is meteorology and is asserted as such by
    // Desert/Tests/Engine/CloudField: a stratus that has grown past 600 m, or a cumulonimbus whose base
    // has left the 0.5-1.5 km band, is a failing test rather than a sky somebody will call "wrong" three
    // weeks later.
    //
    //   Stratus            a featureless sheet a few hundred metres thick, sitting almost on the ground.
    //                      EdgeTopFraction 0.88 because a sheet does not thin into a pad at its rim.
    //   CumulusMediocris   the fair-weather heap: base on the condensation level, top a kilometre above.
    //   CumulusCongestus   the same base, four times the height, and a rim that is a flat pad —
    //                      EdgeTopFraction 0.15. This is the species the ⬛ show contrasts against the
    //                      other two.
    //   Cumulonimbus       a tower to nine kilometres plus the ANVIL, a lobe at 9.5 km that the curve
    //                      form cannot produce and the table can. The anvil is what makes the species
    //                      recognisable from the ground, and it is why the profile is multimodal.
    inline const CloudSpeciesShape& CloudSpeciesShapeOf( CloudSpecies species )
    {
        static const CloudSpeciesShape kLibrary[kCloudSpeciesCount] = {
             // Base   Top   Edge  Ramp  Taper  AnvilKm  AnvilTh  AnvilStr  Detail  Density
             { 0.15f, 0.55f, 0.88f, 0.12f, 0.35f, 0.0f, 0.0f, 0.0f, 0.05f, 0.70f },
             { 0.90f, 1.90f, 0.45f, 0.06f, 0.45f, 0.0f, 0.0f, 0.0f, 0.70f, 1.00f },
             { 2.20f, 5.80f, 0.15f, 0.04f, 0.50f, 0.0f, 0.0f, 0.0f, 1.00f, 1.15f },
             { 0.90f, 9.00f, 0.12f, 0.04f, 0.40f, 9.5f, 1.8f, 0.85f, 0.85f, 1.35f },
        };

        const uint32_t index = static_cast<uint32_t>( species );
        // A hand-edited scene can carry any integer at all in the species field, and reading past this
        // array would be undefined behaviour rather than a wrong-looking sky. Clamped rather than
        // asserted because the loader has already accepted the file by the time this is called.
        return kLibrary[index < kCloudSpeciesCount ? index : 0u];
    }

    // The bottom and the top of the shell a layer of this species needs. The ANVIL is above the tower, so
    // the top is not simply TopAltitudeKm — a species whose second lobe is outside the shell would have
    // that lobe silently cut off by the layer geometry, which is the "sky was a ceiling" defect in a new
    // costume (commit 54330ab9).
    inline float CloudSpeciesBaseKm( const CloudSpeciesShape& shape )
    {
        return shape.BaseAltitudeKm;
    }

    inline float CloudSpeciesTopKm( const CloudSpeciesShape& shape )
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
     * write — gives every cloud a rounded bottom, which reads as fog lying in the air.
     */
    inline float CloudProfileCurve( const CloudSpeciesShape& shape, float altitudeKm, float pattern )
    {
        const float p = std::clamp( pattern, 0.0f, 1.0f );

        const float span = shape.TopAltitudeKm - shape.BaseAltitudeKm;
        if ( span <= 0.0f )
            return 0.0f;

        // THE HEIGHT THIS COLUMN REACHES, and the whole reason the profile is two-dimensional. At the rim
        // of a patch the species gets EdgeTopFraction of its span; at the core it gets all of it.
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
    // shell that is the SPECIES' OWN span rather than a fixed ten kilometres it is a fine grid: the
    // thinnest species in the library, a 400 m stratus, still gets all 256 rows of it.
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
     * The envelope is the species' own [base, top] — see CloudSpeciesBaseKm / CloudSpeciesTopKm — so the
     * altitude axis is spent entirely on the cloud rather than on the empty air a fixed ceiling would
     * put above a stratus.
     */
    inline std::vector<float> CloudBuildProfileTable( CloudSpecies species )
    {
        const CloudSpeciesShape& shape = CloudSpeciesShapeOf( species );

        const float bottomKm = CloudSpeciesBaseKm( shape );
        const float spanKm   = std::max( CloudSpeciesTopKm( shape ) - bottomKm, 1e-3f );

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
