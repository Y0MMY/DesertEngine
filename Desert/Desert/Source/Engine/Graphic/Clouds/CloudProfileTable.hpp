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

        // ---- WHERE THIS TYPE IS, which is a different question from what shape it is -----------------
        //
        // These two are T3's, and they exist because the profile could not answer either of the two
        // questions below however it was authored:
        //
        //   * a cirrus is not a field of blobs that happen to be thin, it is a set of FIBROUS BANDS
        //     drawn out along the wind. No profile makes a band out of a round patch: the patch is the
        //     placement field's, and until T3 there was one placement field, isotropic, shared by
        //     everything in the sky;
        //   * a stratocumulus deck one kilometre thick made of three-kilometre cells is a single cell
        //     filling the zenith. The cell size is the placement field's period, and one period for the
        //     whole layer means the type that wants small cells and the type that wants large ones
        //     cannot both have them.
        //
        // Unreal answers the same two with `Layout_CloudGlobalPattern` — one CHANNEL of placement per
        // type — and `Layout_CloudPerTypeScale`, a scale per type on top of it (decision D-14,
        // Docs/Clouds/ANALYSIS_APPROACH.md §7, and EpicDoc_CloudMaterial.md §2-3). These are that pair.

        // The period of THIS type's placement field, as a MULTIPLE of the layer's Weather Tile Size.
        //
        // RELATIVE AND NOT ABSOLUTE, which is the opposite of the choice made for the altitudes above, and
        // the reason is that the two numbers are anchored to different things. An altitude is anchored to
        // meteorology — a stratus at 400 m is a fact about weather — so §5.1 of the analysis demands it be
        // absolute or no test can catch a layer that has drifted. A placement period is anchored to the
        // CAMERA: Max View Distance divided by the tile is the number of times the field repeats between
        // the viewer and the vanishing point, and five repeats against twenty is the difference between a
        // cumulus field and unmissable moire (CALIBRATION.md §4). That ratio is the layer's to keep, so a
        // type states how much coarser or finer than the layer it is and the pair stays calibrated.
        float PlacementScale;

        // How much longer the placement field's period is ALONG THE WIND than across it. 1 is a round
        // patch; above 1 the patches are drawn out downwind into bands.
        //
        // THE AXIS IS THE WIND'S rather than a second authored angle, because that is what the shape is
        // made of: fibrous cirrus is ice falling through a shear, and the streak lies along the flow by
        // construction. A separate angle would be a second statement of a direction the layer already
        // carries — two values that can disagree, the §2.3.1 defect class.
        float PlacementAnisotropy;
    };

    /**
     * HOW MANY KINDS OF CLOUD ONE SKY HOLDS AT ONCE, and the number is structural rather than chosen.
     *
     * The profile table is one RGBA image and a type owns one CHANNEL of it — which is Unreal's
     * arrangement exactly: its three layout textures fix four types to R, G, B and A (Stratocumulus,
     * Altostratus, Cirrostratus, Nimbostratus — EpicDoc_CloudMaterial.md §2-3). Four channels, four types.
     *
     * The LIBRARY on disk is not limited by this at all; a project may ship a hundred `.decloudtype`
     * files. What is limited is how many of them a single layer carries, and the limit is the width of a
     * texel.
     */
    inline constexpr uint32_t kCloudSpeciesSlots = 4;

    /// The shell a set of types needs: the union of their altitude ranges. Bottom above top means "no
    /// active type", which the packer answers with the built-in default rather than with a degenerate
    /// shell.
    struct CloudEnvelopeKm
    {
        float BottomKm;
        float TopKm;
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
     * THE ENVELOPE OF A SET, and it is the union of the members' ranges rather than any of them.
     *
     * This is the same rule T0 wrote for one type — the shell is computed, never authored — carried
     * unchanged to a set, which is the whole claim T1 made about the shape of this code and the one T3 had
     * to check. A union and not an intersection: a stratocumulus deck from 0.6 to 1.6 km standing beside a
     * congestus tower from 2.2 to 5.8 km needs a shell from 0.6 to 5.8, and marching only where they
     * overlap would render neither.
     *
     * @p count above kCloudSpeciesSlots is clamped rather than read past; a count of zero returns an
     * inverted range, which is the caller's signal that no type is active.
     */
    inline CloudEnvelopeKm CloudTypeSetEnvelopeKm( const CloudTypeShape* shapes, uint32_t count )
    {
        CloudEnvelopeKm envelope{ 0.0f, -1.0f };

        const uint32_t used = std::min( count, kCloudSpeciesSlots );
        for ( uint32_t i = 0; i < used; ++i )
        {
            const float base = CloudTypeBaseKm( shapes[i] );
            const float top  = CloudTypeTopKm( shapes[i] );

            if ( envelope.TopKm < envelope.BottomKm )
            {
                envelope.BottomKm = base;
                envelope.TopKm    = top;
                continue;
            }

            envelope.BottomKm = std::min( envelope.BottomKm, base );
            envelope.TopKm    = std::max( envelope.TopKm, top );
        }

        return envelope;
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

    // RGBA32F, AND ALL FOUR CHANNELS ARE THE POINT NOW. Channel k is species k's profile over the shared
    // envelope, which is Epic's own arrangement in Epic's own words: "each channel describes the profile
    // shape and relative altitude of a different cloud type" (EpicDoc_CloudMaterial.md §2).
    //
    // T0 wrote the profile into .r and zeroed the rest, and the note here said the three spare channels
    // were a property of the format rather than a reservation. They were both, as it turns out: the format
    // has no one-channel variant AND the fourth type was always going to want the fourth channel.
    inline constexpr uint32_t kCloudProfileTableChannels = 4;
    static_assert( kCloudProfileTableChannels == kCloudSpeciesSlots,
                   "One channel per species is what fixes the number of species at four." );

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
     * The envelope is the SET'S union of [base, top] — see CloudTypeSetEnvelopeKm — so the altitude axis
     * is spent entirely on air some type can put cloud in, rather than on the empty air a fixed ceiling
     * would put above a stratus.
     *
     * ONE ALTITUDE AXIS FOR EVERY SPECIES, and that is what makes the containment relation structural
     * rather than asserted. Each channel is evaluated at the SAME absolute altitude, so a species whose
     * band does not reach here writes a zero here — the table itself says which species are alive at an
     * altitude, and the march reads that in one fetch instead of testing four ranges. A species could not
     * be outside the envelope even if someone wanted it to be: the envelope is the union, so every
     * species' band lies inside the axis by construction.
     *
     * A slot beyond @p count leaves its channel zero, which is exactly the "not alive anywhere" state, so
     * an empty slot costs the march nothing and needs no flag to say so.
     */
    inline std::vector<float> CloudBuildProfileTable( const CloudTypeShape* shapes, uint32_t count )
    {
        const uint32_t        used     = std::min( count, kCloudSpeciesSlots );
        const CloudEnvelopeKm envelope = CloudTypeSetEnvelopeKm( shapes, used );

        std::vector<float> texels( kCloudProfileTableFloats, 0.0f );
        if ( used == 0 )
            return texels;

        const float bottomKm = envelope.BottomKm;
        const float spanKm   = std::max( envelope.TopKm - bottomKm, 1e-3f );

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

                for ( uint32_t k = 0; k < used; ++k )
                    texels[texel + k] = CloudProfileCurve( shapes[k], altitudeKm, pattern );
            }
        }

        return texels;
    }

    /// One type on its own, which is what every caller outside the renderer wants and what the whole
    /// library was before T3. Written in terms of the set so there is one generator and not two.
    inline std::vector<float> CloudBuildProfileTable( const CloudTypeShape& shape )
    {
        return CloudBuildProfileTable( &shape, 1u );
    }

    /**
     * THE ANISOTROPY OF THE PLACEMENT FIELD, mirrored from Common/CloudField.glslh's
     * CLOUD_COVERAGE_FREQ_{X,Y,Z}.
     *
     * TWO STATEMENTS OF THREE NUMBERS, and they are only safe because the relation is a test: the shader
     * header is compiled as C++ by Desert/Tests/Engine/CloudField, which has both spellings in scope and
     * asserts they are equal. The alternative — the packer not knowing the frequencies — is worse: the
     * basis vectors below would have to be sent as a scale plus an angle and rebuilt per sample, which is
     * a trigonometric rotation in the innermost loop of the march to avoid writing a number twice.
     *
     * They are Unreal's Noise1 coefficients normalised to the first horizontal axis (ShapeModel.md §11.2);
     * the long note on why one number could never have expressed them is in the shader header.
     */
    inline constexpr float kCloudCoverageFreqX = 1.0f;
    inline constexpr float kCloudCoverageFreqY = 2.182f;
    inline constexpr float kCloudCoverageFreqZ = 1.0665f;

    /// The two horizontal basis vectors of one species' placement field, in the world XZ plane, before the
    /// layer's own tile divides them. Four floats rather than two glm::vec2 so that this header stays free
    /// of every dependency, which is what lets the asset layer, the packer and two test suites all include
    /// it.
    struct CloudPlacementBasis
    {
        float AlongX;
        float AlongZ;
        float AcrossX;
        float AcrossZ;
    };

    /**
     * Build that basis from a type and the layer's wind.
     *
     * Pure, and separated from the packer so the tests can drive it directly and check the two properties
     * that matter: that the ACROSS-wind period is the type's own scale whatever the anisotropy is, and
     * that an anisotropy of 1 with a wind along +X reproduces the single-species field T1 shipped,
     * coefficient for coefficient.
     *
     * A wind with no horizontal part falls back to +X — a vertical or zero wind names no axis, and an
     * arbitrary axis chosen silently would rotate the whole sky the first time somebody zeroed the field.
     */
    inline CloudPlacementBasis CloudSpeciesPlacementBasis( const CloudTypeShape& shape, float windX, float windZ )
    {
        const float axisLength = std::sqrt( windX * windX + windZ * windZ );

        float axisX = 1.0f;
        float axisZ = 0.0f;
        if ( axisLength > 1e-6f )
        {
            axisX = windX / axisLength;
            axisZ = windZ / axisLength;
        }

        // Floored rather than trusted: the asset layer refuses a scale outside [0.05, 8] and an anisotropy
        // outside [0.1, 16], but this function is also reachable from a test and from a hand-built shape,
        // and a zero here is a division that puts an infinity into a texture coordinate.
        const float scale      = std::max( shape.PlacementScale, 1e-3f );
        const float anisotropy = std::max( shape.PlacementAnisotropy, 1e-3f );

        // ALONG THE WIND THE PERIOD IS LONGER, so the FREQUENCY is smaller — hence the division by the
        // anisotropy on this axis and not on the other. That is what turns a field of round patches into
        // bands drawn out downwind, and it is the only thing in this subsystem that can.
        const float alongFreq  = kCloudCoverageFreqX / ( scale * anisotropy );
        const float acrossFreq = kCloudCoverageFreqZ / scale;

        return CloudPlacementBasis{ axisX * alongFreq, axisZ * alongFreq, -axisZ * acrossFreq,
                                    axisX * acrossFreq };
    }
} // namespace Desert::Graphic
