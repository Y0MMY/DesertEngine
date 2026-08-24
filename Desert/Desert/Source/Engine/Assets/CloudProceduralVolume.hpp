#pragma once

#include <Engine/Assets/CloudModellingVolume.hpp>
#include <Engine/Graphic/Clouds/CloudProfileTable.hpp>

#include <Common/Core/ResultStr.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Desert::Assets
{
    /**
     * @file
     * @brief The PROCEDURAL modelling volume: a camera-centric 3D texture whose shape field is a sum of
     *        smoothed volumetric lumps placed by a hash, joined by the same exponential smooth minimum
     *        phase Э4 built for sculpted bodies.
     *
     * WHY IT EXISTS, AND THE DEFECT IT IS THE CURE FOR. The procedural producer took its coverage from a
     * threshold on the Alligator noise, and `Alligator = best - second` IS ZERO WHEREVER TWO FEATURE
     * POINTS CONTRIBUTE EQUALLY. There is a wall of zeros between every pair of cells, so two lobes cannot
     * merge at any setting of any slider, and a procedural sky reads as a DECK OF SEPARATE CUSHIONS rather
     * than as a convective mass. Three tasks measured that independently — T2 measured the field, T0 showed
     * the profile does not move it horizontally, T3 showed that several independent placement fields do not
     * help — and each of them worked around it rather than removing it.
     *
     * The exponential smooth minimum has no wall of zeros. Merging is its DEFINING PROPERTY and not a
     * setting: `-r*ln(SUM exp(-d_k/r))` of two overlapping solids is one connected body with a smoothed
     * crease, and no choice of r can put a zero between them. That is the whole of the cure, and it is
     * variant C of Docs/Clouds/ANALYSIS_APPROACH.md §3 point 2 — approved as decision D-1 and then
     * substituted for the cheaper threshold — put back.
     *
     * WHAT IS REUSED RATHER THAN REWRITTEN. Everything below the placement: `CloudModellingBlob`, the three
     * primitives, the signed distances, the join, the canonical sort and the softmax weights are phase Э4's
     * and are called here, not copied (Engine/Assets/CloudModellingVolume.hpp). This file adds exactly two
     * things Э4 did not have — a hash that puts the lumps in the sky instead of an artist, and a bake that
     * scrolls with the camera and wraps at the region's edge.
     *
     * WHAT IS NEW AND HAS TO BE ARGUED FOR:
     *
     *   * THE VOLUME IS PERIODIC IN X AND Z. Every sampler in this engine is LINEAR/REPEAT, so a volume
     *     that was not periodic would show a HARD SEAM at every region boundary — the field would jump from
     *     one edge's value to the other's. Wrapping the lumps across the boundary at bake time costs one
     *     extra splat per lump near an edge and makes REPEAT exactly continuous, which is what turns "what
     *     happens outside the region" from a defect into the degenerate far path of §3 point 3: beyond the
     *     region the sky is this region again, at a distance where a cloud's angular size is already below
     *     what the march resolves (§4.5).
     *
     *   * THE REGION SNAPS TO THE LUMP LATTICE. A lump's identity is the hash of its ABSOLUTE cell index,
     *     so a cell that is in the region before a shift and after it produces the identical lump — which
     *     is what makes the field invariant under scrolling anywhere more than the influence cutoff inside
     *     both regions. Snapping to the lattice is what makes "the same cell" a meaningful statement;
     *     without it every shift would re-roll every lump and the sky would boil. The same snap, for the
     *     same reason, that the cloud shadow map takes against its 20 km grid, where it was measured at
     *     0.545/255 of boiling with the snap against 2.291/255 without it.
     */

    /// The volume's shape, FIXED like the sculpted volume's is, and for the same reason: the shader needs
    /// it as compile-time numbers, and two statements of one extent is the defect class §2.3.1 names.
    /// Mirrored by CLOUD_PROCEDURAL_VOLUME_WIDTH and its two siblings in
    /// Editor/Resources/Shaders/Common/CloudField.glslh; Desert/Tests/Engine/CloudProceduralField asserts
    /// the two agree.
    ///
    /// 256 x 32 x 256 RGBA8 is 8.00 MiB, and the number is chosen by TWO bounds rather than by ambition:
    ///
    ///   * BELOW, by what the march can find. Trilinear filtering cannot express a feature narrower than
    ///     two voxels, and the march SEARCHES at CloudFinestResolvableChordKm — 125 m at the component's
    ///     Max Steps — so a voxel finer than 62.5 m would put structure into the volume that no ray can be
    ///     relied on to sample. At the shipped 48 km region a voxel is 187.5 m, which is three times that
    ///     bound with room to spare.
    ///   * ABOVE, by the memory decision D-9 grants the whole subsystem. Docs/Clouds/CALIBRATION.md §A0
    ///     measured 20.67 MiB occupied before this volume existed and 4.00 MiB per sculpted body; 8.00 MiB
    ///     here leaves 35.33 MiB, which is still the EIGHT hero clouds phase A2 shipped. Variant C's
    ///     512 x 512 x 32 would have been 32.00 MiB and would have cut that to six.
    ///
    /// The vertical axis is the short one and is the LAYER, not a distance: 32 voxels spread over the
    /// shell's own thickness, so a 3.5 km layer gives 109 m of vertical resolution and a thin stratus deck
    /// gets the same 32 samples a cumulonimbus does.
    inline constexpr uint32_t kCloudProceduralVolumeWidth  = 256u; // x, world east
    inline constexpr uint32_t kCloudProceduralVolumeHeight = 32u;  // y, up, spanning the layer exactly
    inline constexpr uint32_t kCloudProceduralVolumeDepth  = 256u; // z, world north

    inline constexpr uint32_t kCloudProceduralBytesPerVoxel = 4u;

    inline constexpr uint64_t kCloudProceduralVoxelBytes =
         static_cast<uint64_t>( kCloudProceduralVolumeWidth ) * kCloudProceduralVolumeHeight *
         kCloudProceduralVolumeDepth * kCloudProceduralBytesPerVoxel;

    /**
     * @brief Everything the placement needs about one KIND of cloud, and nothing about where the camera is.
     *
     * It is `Graphic::CloudTypeShape` plus the two numbers a shape cannot carry because they belong to the
     * layer — the lattice the layer's Weather Tile Size sets, and the wind axis the layer's direction sets.
     * Kept as a separate struct rather than passing the shape and the layer separately so that the
     * generator has ONE input and a test can hold a single value.
     */
    struct CloudProceduralSpecies
    {
        /// The kind of cloud, exactly as the `.decloudtype` asset stores it. Every one of its fourteen
        /// numbers is read here or by the march; none is decorative.
        Graphic::CloudTypeShape Shape{};

        /// The side of the lattice cell one cluster of lumps is drawn in, kilometres, ALREADY carrying this
        /// species' Placement Scale. A cluster is what used to be one cell of the Alligator, and it is now
        /// a pile of overlapping lumps rather than a single cushion.
        float CellKm = 3.0f;

        /// How much longer the cell is along the wind than across it — this species' Placement Anisotropy,
        /// applied to the LATTICE rather than to a noise frequency. A cirrus' fibrous bands are cells drawn
        /// out downwind, which is the same statement the noise made with its basis vectors and is exact
        /// here rather than approximate.
        float Anisotropy = 1.0f;
    };

    /**
     * @brief Everything the bake needs. Pure input: no camera object, no device, no asset manager.
     *
     * DISTANCES ARE KILOMETRES throughout, matching Common/CloudGeometry.glslh and the sculpted volume
     * beside it. The one thing here that is not a length is the seed.
     */
    struct CloudProceduralFieldParams
    {
        /// The horizontal side of the region the volume covers, kilometres. It is also the PERIOD the
        /// volume tiles with, because the bake wraps — see the file note.
        float RegionSizeKm = 48.0f;

        /// The shell the volume spans vertically: the layer's base altitude and its thickness, kilometres.
        /// The volume's 32 rows are spread over exactly this, so a voxel's height is Thickness/32.
        float LayerBottomKm    = 1.5f;
        float LayerThicknessKm = 3.5f;

        /// The radius over which two lumps FUSE, kilometres — the reciprocal of the smooth minimum's
        /// sharpness, and the same field the sculpted recipe carries.
        ///
        /// IT MUST STAY SMALL AGAINST A LUMP, and the reason is arithmetic rather than taste: the join
        /// inflates the surface by `BlendRadiusKm * ln(sum of weights in range)`, so with hundreds of
        /// overlapping lumps a generous radius does not soften the crease, it FLOODS THE SKY. The fusion
        /// comes from the lumps OVERLAPPING — which is how the shipped sculpted recipe makes one body out
        /// of eight — and the radius only decides how sharp the crease between them is.
        float BlendRadiusKm = 0.06f;

        /// How deep inside the body the Dimensional Profile reaches 1, kilometres. The normalisation of the
        /// distance field, exactly as in the sculpted volume.
        float ProfileDepthKm = 0.35f;

        /// What fraction of the lattice's cells carry a cluster, 0..1. THE SLIDER NOW ADDRESSES A FRACTION
        /// OF SKY DIRECTLY, which is what the quantile map it replaces was introduced to fake: a cell is
        /// alive when its hash falls below this number, so 0 is exactly empty and 1 is exactly full for any
        /// seed, with no distribution to calibrate against.
        float Coverage = 0.24f;

        /// How sharply a cell goes from empty to full, > 0. A cell whose hash lands just under the coverage
        /// threshold grows a SMALL cluster; one well under it grows a full-sized one, and this is the width
        /// of that ramp inverted. It is the same field the layer already exposes and it keeps its meaning:
        /// above 1 the sky is decisively cloud or decisively clear, below 1 the sizes spread out.
        float CoverageContrast = 1.0f;

        /// The realization. Two layers with different seeds have unrelated skies; the same seed and the
        /// same region origin always bake the same bytes.
        uint32_t Seed = 1u;

        /// The horizontal wind direction the lattice's anisotropy is measured against, world XZ. Need not
        /// be normalized; a zero vector means east, which is what CloudSpeciesPlacementBasis also does.
        glm::vec2 WindAxis{ 1.0f, 0.0f };

        /// The finest chord the march can be relied on to FIND, kilometres — CloudFinestResolvableChordKm
        /// at the component's Max Steps, handed in rather than assumed.
        ///
        /// IT IS AN INPUT AND NOT A CONSTANT because it is one half of a RELATION, and this programme has
        /// paid for that relation twice. The generator clamps every lump so that its smallest diameter
        /// clears this number: a lump the march cannot find is not a thin cloud, it is speckle that appears
        /// and disappears with the ray's jitter. Desert/Tests/Engine/CloudProceduralField asserts the
        /// clamp holds for every shipped type at every quality tier.
        float ResolvableChordKm = 0.125f;

        /// The kinds of cloud in this layer, in the packed order the renderer resolved them. At most
        /// Graphic::kCloudSpeciesSlots, because a species owns one CHANNEL of the volume.
        std::vector<CloudProceduralSpecies> Species;
    };

    /// Rejects parameters the generator cannot honour, with the offending number in the message. Pure, so
    /// the component's validation and the bake refuse for the same reason rather than disagreeing.
    Common::BoolResultStr ValidateCloudProceduralParams( const CloudProceduralFieldParams& params );

    /**
     * @brief Where the region's corner sits for a camera at @p cameraXKm, @p cameraZKm — SNAPPED.
     *
     * The snap is to the coarsest species' lattice cell, so that a cell inside the region before a shift is
     * the same cell after it and hashes to the identical lump. Returned as a pure function of the camera
     * and the parameters so that the renderer, the bake and the test all answer the question once.
     *
     * @return the MINIMUM corner of the region in world kilometres. The region spans
     *         [origin, origin + RegionSizeKm] on both horizontal axes.
     */
    glm::vec2 CloudProceduralRegionOriginKm( const CloudProceduralFieldParams& params, float cameraXKm,
                                             float cameraZKm );

    /// The snap step the function above quantises to, kilometres — the coarsest cell in the layer. Exposed
    /// because the test asserts the invariance across exactly one step of it, and a test that computed its
    /// own step would be testing its own arithmetic.
    float CloudProceduralSnapKm( const CloudProceduralFieldParams& params );

    /**
     * @brief The lumps one species puts in one region. PURE — the same inputs always give the same lumps,
     *        in the same order, byte for byte.
     *
     * Centres are in WORLD kilometres with y an absolute altitude, not relative to the region, because a
     * lump's identity is its place in the world and the region is only the window it is baked through.
     *
     * WHAT DECIDES A LUMP. Every field of the species' shape, and each of them is named here because a
     * stored number nobody reads is the dead data this contract forbids:
     *
     *   Base/Top Altitude      the band the stack of lumps spans
     *   Edge Top Fraction      how short the SHALLOWEST cluster is against the fullest one
     *   Base Ramp Fraction     the vertical radius of the lowest lump, as a fraction of the band
     *   Top Taper              how fast the horizontal radius shrinks going up the stack
     *   Anvil Altitude/Thickness/Strength   one extra, wider, flatter lump above the tower
     *   Placement Scale        already folded into CellKm by the caller
     *   Placement Anisotropy   already folded into Anisotropy by the caller
     *
     * and the four material numbers — Detail Character, Detail/Density/Extinction Factor — are NOT read
     * here: they travel to the march in the parameter block as they always did, because they describe what
     * the cloud is made of rather than where it is.
     *
     * @param slot which channel of the volume this species owns; it decorrelates the hash, so two species
     *        with identical shapes in different slots put their clusters in different places.
     */
    std::vector<CloudModellingBlob> GenerateCloudProceduralBlobs( const CloudProceduralFieldParams& params,
                                                                  uint32_t slot, const glm::vec2& regionOriginKm );

    /**
     * @brief Bakes the whole volume: one channel per species, RGBA8, the layout Graphic::Image3D uploads.
     *
     * ONE CHANNEL PER SPECIES and not one channel per Nubis quantity, and this is the one place where this
     * phase departs from the letter of variant C §3 point 2. That text gives the join's softmax weights to
     * Detail Type and Density Scale — which is right, and which is what the SCULPTED volume does — but it
     * was written before phase T3 put four kinds of cloud in one sky. Four channels can carry four species'
     * profiles or one species' four quantities, and the shipped state has four species with per-species
     * material numbers already reaching the march through the parameter block. Spending the channels on
     * quantities the march already has would have cost three of the four species. The divergence is
     * reported in the phase's report rather than hidden here.
     *
     * The softmax weights are not wasted: WITHIN a species they blend the lumps' own Detail Type and
     * Density Scale, and since a species' lumps share both, that blend is the identity — which is why
     * spending a channel on it would have bought nothing at all.
     *
     * @param params  validated by ValidateCloudProceduralParams; an invalid set is an error, never a
     *                silently substituted default.
     * @param regionOriginKm  the region's minimum corner, from CloudProceduralRegionOriginKm.
     * @return exactly kCloudProceduralVoxelBytes bytes, or an error naming what was wrong.
     */
    Common::ResultStr<std::vector<unsigned char>>
    BakeCloudProceduralVolume( const CloudProceduralFieldParams& params, const glm::vec2& regionOriginKm );

    /// How many lumps the whole region holds, summed over the species — the quantity the bake's cost is
    /// linear in, exposed so the renderer can log it beside the milliseconds rather than guessing.
    size_t CountCloudProceduralBlobs( const CloudProceduralFieldParams& params, const glm::vec2& regionOriginKm );
} // namespace Desert::Assets
