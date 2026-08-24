#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Desert::Graphic
{
    /**
     * WHAT A KIND OF CLOUD IS, as fourteen numbers — and nothing about how they are turned into a field.
     *
     * WHAT THIS HEADER USED TO ALSO HOLD, AND WHY IT DOES NOT. Until phase Э5 it carried a parametric
     * curve `f(altitude, placement pattern)` and the generator that evaluated it into a 256 x 64 RGBA32F
     * table the march sampled — decision D-13, and it was the right answer to the question it was asked.
     * The question changed. That table was one half of `coverage x profile`, and the other half was a
     * threshold on the Alligator noise, whose field is `best - second` and therefore ZERO wherever two
     * feature points contribute equally: a wall of zeros between every pair of cells, so two lobes could
     * never merge at any setting of any slider and the sky read as a deck of separate cushions.
     *
     * The profile is a three-dimensional field now — a sum of smoothed volumetric lumps joined by an
     * exponential smooth minimum, baked into a camera-centric volume by
     * Engine/Assets/CloudProceduralVolume.hpp — which is variant C of ANALYSIS_APPROACH.md §3 point 2 as
     * decision D-1 approved it. The numbers below did not change and NOT ONE OF THEM LOST ITS CONSUMER;
     * what changed is what reads them:
     *
     *   Base / Top Altitude               the band a cluster of lumps spans
     *   Edge Top Fraction                 how short the shallowest cluster is against the fullest
     *   Base Ramp Fraction                the thickness of the lowest lobe against the ones above it
     *   Top Taper                         how fast a stack narrows going up
     *   Anvil Altitude / Thickness / Strength   one extra, wider, flatter lump above the tower — which is
     *                                     the shape D-13 reached for a two-dimensional table to express,
     *                                     and a second lump expresses without a table at all
     *   Placement Scale / Anisotropy      the lattice a cluster is drawn in, and its stretch downwind
     *   Detail Character / Detail, Density, Extinction Factor   what the cloud is MADE of — these four
     *                                     still travel to the march in the parameter block, untouched
     *
     * THE ANCHOR OF §5.1 SURVIVES UNCHANGED and is the reason the altitudes are still ABSOLUTE kilometres
     * rather than fractions of a layer: a set of ratios is satisfied at any absolute scale, so a layer
     * that has drifted into the stratosphere fails no test unless some number is pinned in metres. These
     * are those numbers, and Desert/Tests/Engine/CloudType pins the shipped library against meteorology.
     *
     * THIS HEADER IS DEPENDENCY-FREE ON PURPOSE. It is included by the asset layer (which owns the
     * numbers), by CloudPayload.hpp (which computes the shell from a type's absolute altitudes), by the
     * procedural generator (which places the lumps) and by two test suites. Anything heavier here would
     * drag the renderer into a test that links nothing.
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

} // namespace Desert::Graphic
