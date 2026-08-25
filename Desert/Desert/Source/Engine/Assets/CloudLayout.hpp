#pragma once

#include <Common/Core/ResultStr.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Desert::Assets
{
    /**
     * @file
     * @brief The PAINTED CLOUD LAYOUT (`.dclayout`): where the artist says clouds are, and where they are
     *        not, as data rather than as a hash.
     *
     * WHY IT EXISTS. The owner asked for two things in one breath — "a cloud material editor like Unreal's,
     * so I can load noise textures for the cloud shape" and "is there a parameter to do this by HAND". The
     * first is refused and stays refused (decision D-5: no material graph; our graph cannot compile into
     * the march loop, cannot sample 3D textures and cannot contain a loop). The second is this file. The
     * two are not the same request: Unreal's layout textures are DATA, and data is the one part of that
     * material we can take without taking the graph — the same formula this programme has already applied
     * three times (`.decloudtype`, `.dcnv`, `.dcmv`): Unreal's semantics, our formats.
     *
     * WHAT UNREAL DOES, AND WHICH HALF OF IT IS HERE. Docs/Clouds/RESEARCH_LAYOUT_TEXTURES.md is the full
     * account; the three facts this file is built on are:
     *
     *   1. `Layout_CloudGlobalPattern` — ONE CHANNEL PER CLOUD TYPE, each an independent two-dimensional
     *      field of "does this kind of cloud live here". Not a partition, not a set of blend weights: the
     *      channels overlap freely and the conflict is settled by a `max` at the very end. That is the
     *      arrangement our own four species slots already have, so the mapping is exact.
     *   2. `Layout_GlobalCloudMask` — regional ADD and REMOVE, and it is ADDITIVE: it is summed into the
     *      assembled shape ("Add additive mask" is the author's own node comment) and subtracts only by
     *      carrying a negative weight. There is no multiply and no remap anywhere on that path.
     *   3. `Layout_CloudHeightProfile` is NOT here, and its absence is a decision with a bearing input.
     *      Unreal needs a `f(altitude, pattern value)` table because its placement field is two-dimensional
     *      and the table is the only vertical structure it has. Ours is geometry: a lump has three radii and
     *      an altitude, and `fill` in CloudProceduralVolume.cpp — how deep inside the coverage threshold a
     *      cell fell — already drives the stack's vertical band through the type's Edge Top Fraction. That
     *      IS `f(altitude, pattern value)`, expressed once. A painted table would be a SECOND way to state
     *      the same thing (contract §2.3.1) and would fight the §SIL2 calibration, where the lump's aspect
     *      and the erosion's strength are one quantity guarded by a test that names both numbers.
     *
     * THE PAYLOAD IS READ AT THE BAKE AND NEVER IN THE MARCH, and that is the whole cost argument. Unreal
     * samples its three layout textures on every material evaluation — once per march step and again at
     * seven further sites on the shadow rays (VolumetricCloud.usf:858, 991, 1012, 1092, 1106, 1181, 1317,
     * 1920, 2186) — because it has no precomputed field at all. We do: the field is a baked periodic volume
     * and the march performs ONE fetch of it. So the painted layer enters where placement is already
     * decided, once per LATTICE CELL, and adds not one instruction to the hottest pass of the frame.
     *
     * THE ONE REAL CONSTRAINT IS PERIODICITY, and it is why the placement below carries an integer rather
     * than a length. The modelling volume must be exactly periodic over the region: everything past the
     * region is REPEAT sampling of it, and the wrap seam is measured at 0.950/255 against 1.239/255 between
     * ordinary neighbours (CALIBRATION.md §RW). A world-anchored painting whose period did not divide the
     * region would put a hard discontinuity across every region face — a defect an order of magnitude
     * larger than the seam that exists. Hence `RepeatsPerRegion`, a whole number: divisibility is a
     * property of the TYPE and not something somebody has to remember to check.
     */

    /// How many channels a pattern table carries, and it is not a preference: a channel is one SPECIES
    /// SLOT of the layer, and a layer holds four (Graphic::kCloudSpeciesSlots, CLOUD_SPECIES_SLOTS in
    /// Common/CloudField.glslh). Unreal fixes the same four to R, G, B and A.
    ///
    /// NOTE WHAT A CHANNEL MEANS HERE AND WHAT IT MEANS AT EPIC. Theirs is a NAMED GENUS — R stratocumulus,
    /// G altostratus, B cirrostratus, A nimbostratus — because their four types are compiled in for ever.
    /// Ours is the layer's SLOT, whatever type the artist dropped into it, and the library on disk is not
    /// limited by this number at all. That is the more general arrangement and it costs nothing.
    inline constexpr uint32_t kCloudLayoutChannels = 4u;

    /// Bounds on the table's side. Below the floor a painting cannot describe a cell of the placement
    /// lattice at all; the ceiling is the memory decision D-9 grants the subsystem — 2048 squared would be
    /// 16.0 MiB of pattern, which is twice the modelling volume it feeds.
    inline constexpr uint32_t kCloudLayoutMinResolution = 4u;
    inline constexpr uint32_t kCloudLayoutMaxResolution = 1024u;

    /**
     * @brief A decoded layout: up to two tables, the means the bake needs, and the number the staleness
     *        check compares.
     *
     * EITHER TABLE MAY BE ABSENT, and absent is spelt as an EMPTY VECTOR rather than as a flag. One
     * representation of "there is no mask", so a mask that is present-but-empty cannot exist and no reader
     * has to decide which of two fields to believe.
     */
    struct CloudLayoutData
    {
        /// Side of both tables in texels. Square, because the layout tiles the world on a square period and
        /// a non-square table would stretch the painting by an aspect nobody authored.
        uint32_t Resolution = 0u;

        /// RGBA8, `4 * Resolution * Resolution` bytes, x fastest. Channel k is species slot k's own field
        /// of "is there cloud of this kind here", 0 = none, 255 = as much as the slider allows. Empty when
        /// the layout carries no pattern.
        std::vector<unsigned char> Pattern;

        /// R8, `Resolution * Resolution` bytes. 128 is NEUTRAL, above it adds cloud and below it removes —
        /// the signed convention Unreal expresses with a negative weight instead, chosen here because a
        /// painter's mid-grey is the value an artist can actually flood-fill. Empty when there is no mask.
        std::vector<unsigned char> Mask;

        /// The mean of each pattern channel over the whole table, 0..1, computed ONCE when the file is
        /// encoded and carried in the header.
        ///
        /// THIS IS THE FIELD THAT KEEPS DECISION D-20 TRUE, and it is worth stating why a mean is stored at
        /// all rather than recomputed. The pattern modulates a cell's coverage about the slider, and it is
        /// applied ZERO-MEAN so that binding a layout REDISTRIBUTES cloud instead of adding it — exactly
        /// the discipline the procedural patch field it replaces already keeps ("symmetric about the
        /// slider", CloudProceduralVolume.cpp). Without it, a bright painting would raise the sky's cover
        /// and the `Coverage` -> fraction-of-sky mapping that every shipped scene was re-authorised against
        /// would have to be measured again for every painting anybody ever makes.
        ///
        /// Stored rather than derived because the bake visits a few hundred CELLS and would otherwise sweep
        /// a million texels to learn one number, and because a mean computed twice is two numbers that can
        /// disagree.
        float PatternMean[kCloudLayoutChannels] = { 0.0f, 0.0f, 0.0f, 0.0f };

        /// CRC-32 of the payload, the same one the container verifies. It is what
        /// Assets::CloudProceduralFieldParams carries so that swapping the painting, or editing it in place,
        /// makes the cached volume stale — see the note on CloudProceduralFieldParams::LayoutContentHash for
        /// why the PARAMS carry a number and the BAKE takes the bytes.
        uint32_t ContentHash = 0u;

        bool HasPattern() const
        {
            return !Pattern.empty();
        }

        bool HasMask() const
        {
            return !Mask.empty();
        }

        /// True when this layout can change a single cell — i.e. when it carries at least one table. A
        /// decoded file always does; ValidateCloudLayoutData refuses one that carries neither, because a
        /// layout that cannot move anything is a slot an artist fills and never sees.
        bool IsUsable() const
        {
            return Resolution > 0u && ( HasPattern() || HasMask() );
        }
    };

    /**
     * @brief Where the painting sits in the world and how hard it pushes — the layer's half of the
     *        arrangement, as opposed to the file's half above.
     *
     * It is a plain value type and it lives in the bake's parameters, because every one of these five
     * numbers changes the baked volume and therefore has to make the cache stale.
     */
    struct CloudLayoutPlacement
    {
        /// How many times the painting repeats across the region, a WHOLE number.
        ///
        /// WHY AN INTEGER AND NOT A PERIOD IN KILOMETRES, which is how Unreal spells the same control
        /// (`Layout_CloudGlobalScale`, 256 km). Their layout has nothing to stay in step with; ours has to
        /// divide the region exactly or the volume stops being periodic and the far field grows a seam. A
        /// float period would make divisibility a thing somebody validates — and a validator that can be
        /// forgotten is the defect class §2.3.1 names. As an integer the relation cannot be expressed
        /// wrongly. At the shipped 48 km region, 1 gives a 48 km period and 16 gives 3 km, which is one
        /// lattice cell.
        uint32_t RepeatsPerRegion = 1u;

        /// Quarter turns of the painting about the world's vertical axis, 0..3.
        ///
        /// QUARTER TURNS AND NOT AN ANGLE, for the same reason and it is the same relation: a square
        /// lattice maps onto itself under a quarter turn and under nothing else, so a free angle breaks the
        /// periodicity that the integer above was chosen to protect. Unreal rotates freely
        /// (`Layout_GlobalTexturePlacement.a`) because it has no baked volume to keep in step. The
        /// divergence is deliberate and the bearing input is the measured wrap seam.
        uint32_t QuarterTurns = 0u;

        /// Where the painting's origin sits in the world, kilometres. Continuous, and it does NOT threaten
        /// the periodicity: sliding a periodic function along its own axis leaves it periodic.
        glm::vec2 OffsetKm{ 0.0f, 0.0f };

        /// How hard the painted pattern rules the coverage, 0..1.
        ///
        /// AT ZERO THE PROCEDURAL PATCH FIELD TAKES OVER, and that is not a fallback bolted on — it is the
        /// single-source rule. A cell's coverage has ONE modulator: the painting when there is one and it
        /// is turned up, the hash otherwise. Both applied at once would be two mechanisms deciding one
        /// number, which §1.3 and §4.2 of the contract forbid and which is exactly what the research found
        /// Unreal does not have to worry about, having no procedural patch at all.
        float PatternStrength = 1.0f;

        /// How hard the painted mask adds and removes, 0..1. Additive and ASYMMETRIC — that is its job, and
        /// it is Unreal's semantics unchanged. It is safe for D-20 in a way the pattern is not, because a
        /// layout with no mask table contributes exactly nothing and an artist who paints one is asking for
        /// the sky to change.
        float MaskStrength = 1.0f;
    };

    bool CloudLayoutPlacementEqual( const CloudLayoutPlacement& a, const CloudLayoutPlacement& b );

    /// Rejects a placement the bake cannot honour, naming the offending number. Pure, so a caller can refuse for
    /// the same reason the bake refuses rather than the two disagreeing about what is legal.
    Common::BoolResultStr ValidateCloudLayoutPlacement( const CloudLayoutPlacement& placement );

    /// Rejects a decoded layout that cannot be sampled: no resolution, both tables absent, a table whose
    /// length disagrees with the resolution, a mean outside 0..1.
    Common::BoolResultStr ValidateCloudLayoutData( const CloudLayoutData& data );

    /**
     * @brief World kilometres to a position in the painting, in TABLE UNITS that wrap at 1.
     *
     * THE ONE RELATION THIS FUNCTION EXISTS TO KEEP: for any point, any offset, any rotation and any
     * repeat count, `CloudLayoutUv(p) == CloudLayoutUv(p + (regionSizeKm, 0))` and likewise on the other
     * axis, modulo 1. That is what makes the baked volume periodic with a painting bound, and
     * Desert/Tests/Engine/CloudPlacementSpectrum asserts it directly rather than trusting the argument
     * above — a quarter turn replaced by any other angle, or a fractional repeat count, turns it red.
     */
    glm::vec2 CloudLayoutUv( const CloudLayoutPlacement& placement, float regionSizeKm, const glm::vec2& worldKm );

    /**
     * @brief Bilinear, wrapping sample of one pattern channel, 0..1.
     *
     * WRAPPING AND NOT CLAMPED, because the painting tiles the world by construction and a clamped edge
     * would smear the last row of texels across everything beyond one period — the same mistake the
     * modelling volume's own note refuses for the same reason.
     *
     * @param slot species slot, 0..kCloudLayoutChannels-1. Out of range returns 0, which reads as "this
     *        kind of cloud is not painted here" and is the honest answer for a slot the painting does not
     *        describe.
     */
    float SampleCloudLayoutPattern( const CloudLayoutData& data, uint32_t slot, const glm::vec2& uv );

    /**
     * @brief Bilinear, wrapping sample of the mask, returned SIGNED in -1..1.
     *
     * The stored byte is 0..255 with 128 neutral, and this is where that convention is turned into the
     * number the coverage expression adds. One place performs the conversion, so a mask that reads as
     * inverted is a bug in one line rather than in every caller.
     */
    float SampleCloudLayoutMask( const CloudLayoutData& data, const glm::vec2& uv );

    /// The container layout's version. Bumped when a FIELD moves, independently of anything about the
    /// meaning of the pixels.
    inline constexpr uint32_t kCloudLayoutContainerVersion = 1u;

    /// Byte length of the container header. Exposed because the round-trip test asserts the whole file
    /// length, and a header that grew without this moving would pass a test that meant nothing.
    inline constexpr size_t kCloudLayoutHeaderSize = 48u;

    /// The four bytes every container starts with.
    inline constexpr char kCloudLayoutMagic[4] = { 'D', 'C', 'L', 'Y' };

    /// The extension the Content Browser, the file dialog and the drag-and-drop payload all agree on.
    inline constexpr const char* kCloudLayoutExtension = ".dclayout";

    /**
     * @brief Serialises a layout into the container.
     *
     * The four channel means are RECOMPUTED here from the pattern rather than trusted from the argument,
     * which is the one place they can be made to agree with the pixels. A mean that travelled in from a
     * caller could describe a different painting than the one being written, and the symptom would be a
     * sky whose cover drifts from its slider for no reason anybody could see.
     */
    Common::ResultStr<std::vector<unsigned char>> EncodeCloudLayout( const CloudLayoutData& data );

    /**
     * @brief Parses a container back into a layout, or says why it could not.
     *
     * REFUSES RATHER THAN GUESSES, each refusal naming the number that was wrong: a wrong magic, an
     * unknown container version, a resolution outside its bounds, a payload length that disagrees with the
     * resolution, a truncated file, a checksum that does not match. A layout that decoded to zeros would
     * render as a sky with no cloud in it and nothing in the log — which is the shape of defect this
     * programme keeps paying for.
     */
    Common::ResultStr<CloudLayoutData> DecodeCloudLayout( const std::vector<unsigned char>& bytes );

    /**
     * @brief Builds a layout from an 8-bit RGBA image, which is what "load a texture" means for the owner.
     *
     * @param pixels  RGBA8, `4 * width * height`, x fastest — the layout every image loader in this tree
     *                already produces.
     * @param channelForSlot  which SOURCE channel feeds each species slot, 0..3 each. It exists because a
     *                        painting is usually greyscale: an artist draws one shape and wants it on slot
     *                        1, and without this they would have to author an RGBA image to say so.
     * @param takeMask  when true the source's alpha becomes the mask table; when false the layout carries
     *                  no mask at all. NOT "alpha is always the mask", because an opaque PNG has alpha 255
     *                  everywhere, which under the signed convention would be a mask that adds cloud to the
     *                  whole sky — a silent, uniform, wrong answer.
     *
     * Non-square and oversized sources are REFUSED by name rather than resampled: resampling is an opinion
     * about the artist's painting, and one taken silently is the worst kind.
     */
    Common::ResultStr<CloudLayoutData>
    MakeCloudLayoutFromImage( const std::vector<unsigned char>& pixels, uint32_t width, uint32_t height,
                              const uint32_t channelForSlot[kCloudLayoutChannels], bool takeMask );
} // namespace Desert::Assets
