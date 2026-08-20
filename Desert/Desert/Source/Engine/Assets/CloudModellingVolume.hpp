#pragma once

#include <Common/Core/ResultStr.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Desert::Assets
{
    /**
     * @file
     * @brief The cloud MODELLING volume: the recipe a hero cloud's body is sculpted from, the container it
     *        is stored in (`.dcmv`), and the pure functions that turn one into the other.
     *
     * WHAT IT IS FOR. Docs/Clouds/ANALYSIS_APPROACH.md §4 gives the cloud field two producers behind one
     * seam. The procedural one is infinite and needs no content, and it has a measured limit that three
     * separate tasks found independently: the Alligator's lobes DO NOT MERGE by construction — the field
     * is `best - second`, which lays a zero between every pair of cells — so a procedural sky reads as a
     * deck of separate cushions and can never be one fused convective mass. This volume is the other
     * producer, and a fused mass is exactly what it is for.
     *
     * WHY A SEPARATE FORMAT AND NOT MORE OF `.dcnv`, since a `.dcnv` is also four channels of RGBA8 in a
     * little-endian container. Four reasons, and each of them is a field that would have had to become
     * "meaningful for one kind of volume only":
     *
     *   1. `.dcnv`'s header IS a noise recipe — a seed, four lattice periods, a curl strength. A sculpted
     *      body has none of them, and a struct whose fields mean something for half its files is the
     *      shape of defect this programme has paid for most often.
     *   2. `.dcnv` is CUBIC by construction: one `Resolution` scalar, validated as a power of two, with
     *      the payload length checked as `R^3`. This volume is 128 x 64 x 128 — three independent extents,
     *      because a cloud is wider than it is tall and a cubic volume spends half its bytes on air.
     *   3. The channel meanings are stored and CHECKED on read in both formats, and they are disjoint
     *      sets. Two magics refuse each other's files by name; one magic could only refuse them by
     *      guessing from a field further in.
     *   4. `.dcnv`'s validator enforces "at least eight voxels per lattice cell", which is a statement
     *      about noise and is meaningless about a sculpted body.
     *
     * IT IS THE SAME ASSET SYSTEM ALL THE SAME (contract §2.2): `AssetTypeID::CloudModellingVolume`,
     * `AssetBase`, `AssetManager`, the Content Browser, the drag-and-drop payload and the hot reload are
     * the ones every other asset uses, and the byte primitives are the ones `.dcnv` uses
     * (Engine/Assets/ContainerBytes.hpp). What is new is a format, not a second way to own files.
     *
     * WHY NOT DDS/KTX/BCn — unchanged from `.dcnv` and repeated because it is the first question asked:
     * there is not one of them anywhere in this tree, `ImageFormat` knows RGBA8F / RGBA16F / RGBA32F /
     * BGRA8F and the two depth formats, and on Apple Silicon the BC family is not exposed for 3D textures
     * at all (ANALYSIS_APPROACH.md §2.2).
     *
     * THE FILE CARRIES ITS OWN RECIPE, exactly as a `.dcnv` does, and for the same reason: the blobs, the
     * blend radius and the two normalisation distances are all in the header, so any volume can be
     * re-baked, compared or edited from what is written in it. That is also what makes the sculpting tool
     * of phase A1 an editor for this struct rather than a new file format.
     */

    /// What a channel of a modelling volume CONTAINS. Stored per channel in the header, so a volume is
    /// self-describing and a mislabelled or re-ordered one is a diagnosable error rather than a cloud that
    /// simply looks wrong.
    ///
    /// The first three are the deck's own set and its own order (Nubis Cubed, SIGGRAPH 2023, p.85 and
    /// p.96: "Dimensional Profile / Detail Type / Density Scale"). The fourth is ours and is named on the
    /// enumerator.
    enum class CloudModellingChannel : uint32_t
    {
        DimensionalProfile = 0, ///< how deep inside the body, 0 at the surface and 1 in the core
        DetailType         = 1, ///< 0 wispy, 1 billowy — which erosion the up-rez applies here
        DensityScale       = 2, ///< per-voxel multiplier on how much matter is in this part of the body
        /// The body DILATED by the bake's margin: 1 throughout the body, falling to 0 a stated distance
        /// outside it. It is the conservative shell — above zero wherever the profile is, and extending
        /// past it, never the other way round — and the CUTOUT is what reads it: an instance that
        /// suppresses the procedural field suppresses it here and only here, so a hero cloud does not cut
        /// a rectangular hole in the deck around it. See Common/CloudField.glslh.
        CutoutEnvelope = 3,
    };

    const char* CloudModellingChannelName( CloudModellingChannel channel );

    /**
     * @brief One smooth lump the body is made of.
     *
     * An ELLIPSOID and not a sphere, because the difference is the whole vocabulary: a cumulus base is a
     * flattened disc, a congestus tower is a prolate lobe, and a lenticular is a lens. Three radii cost
     * two floats over one and remove the need for a second primitive type.
     *
     * The two material numbers are per-blob and NOT per-voxel-authored: the union below turns them into a
     * smooth per-voxel field for free (see GenerateCloudModellingVolume), which is the second of the three
     * properties that made the exponential smooth-min the right join.
     */
    struct CloudModellingBlob
    {
        /// Centre, kilometres, relative to the CENTRE of the volume's box. Relative rather than absolute
        /// so that a recipe is a description of a cloud rather than of a place — the entity's transform is
        /// what puts it in the sky.
        glm::vec3 CentreKm{ 0.0f };

        /// Semi-axes, kilometres. All three strictly positive; a zero axis is a degenerate ellipsoid whose
        /// distance function divides by zero, so it is refused rather than clamped.
        glm::vec3 RadiiKm{ 0.25f };

        /// 0 = wispy, 1 = billowy. The character of the erosion the up-rez cuts into this lump's part of
        /// the body — the same axis Graphic::CloudTypeShape::DetailCharacter is on, so a hero cloud and a
        /// procedural species mean the same thing by it.
        float DetailType = 1.0f;

        /// 0..1, how much matter this lump carries relative to the instance's own Density Factor. A wispy
        /// tail is thinner than the core it grew from, and this is where that is said.
        float DensityScale = 1.0f;
    };

    /**
     * @brief Everything the generator needs to produce a modelling volume, and nothing it does not.
     *
     * Every field is written into the container and read back out of it; the sculpting tool of phase A1
     * edits exactly this struct.
     */
    struct CloudModellingVolumeRecipe
    {
        /// The world extent the body was sculpted at, kilometres, before the entity's own scale. At the
        /// default (2 x 1 x 2 km over 128 x 64 x 128 voxels) a voxel is 15.6 m on every axis.
        ///
        /// FIFTEEN METRES IS NOT A DEGRADATION, IT IS A TRANSFER OF WORK. Guerrilla store 8 m per voxel
        /// and the deck says in the same breath that 8 m of data yields 0.5 m of VISIBLE detail, because
        /// the detail is carried by the up-rez noise and not by the voxel (p.122). The volume carries the
        /// SILHOUETTE; Common/CloudField.glslh's erosion carries the edge. Both halves already exist.
        glm::vec3 SizeKm{ 2.0f, 1.0f, 2.0f };

        /// The radius over which two lumps FUSE, kilometres. It is the reciprocal of the exponential
        /// smooth-min's sharpness, expressed as a length because a length is what an artist can see: two
        /// lobes a quarter of this apart read as one body, two lobes ten times this apart read as two.
        ///
        /// It is also what makes the join non-optional to get right: the union INFLATES the surface by
        /// about `BlendRadiusKm * ln(blobCount)`, which is why ValidateCloudModellingRecipe accounts for
        /// it rather than checking the blobs' own extents.
        float BlendRadiusKm = 0.04f;

        /// How deep inside the body the Dimensional Profile reaches 1, kilometres. It is the
        /// normalisation of the distance field, and it is the one number that decides how much of the
        /// cloud the erosion is allowed to eat: the erosion is weighted by `1 - Profile`, so a small value
        /// makes a body that is solid almost everywhere and a large one makes a body that is edge all the
        /// way through.
        float ProfileDepthKm = 0.30f;

        /// How far OUTSIDE the body the cutout envelope still reads above zero, kilometres. It is the
        /// distance at which a procedural cloud is allowed to come back after a hero cloud has pushed it
        /// away, and it is a bake parameter rather than a runtime one because it is a dilation of THIS
        /// body and the bake is where the body's distance field exists.
        float EnvelopeMarginKm = 0.12f;

        /// The lumps, in any order. The join is commutative AND associative, so this is a set that happens
        /// to be stored in a sequence — nothing downstream may depend on the order, and
        /// Desert/Tests/Engine/CloudAuthored asserts a shuffled recipe bakes byte for byte the same
        /// volume.
        std::vector<CloudModellingBlob> Blobs;
    };

    /// The generator's own version, written into every file. Bumped when the SCULPTING MATHS changes, so a
    /// volume baked before a change is recognisable as such instead of silently disagreeing with the code
    /// that now claims to have produced it.
    ///
    /// 1 — ellipsoid signed distance joined by exponential smooth-min, with the union's softmax weights
    ///     carrying Detail Type and Density Scale (ANALYSIS_APPROACH.md §3, PLAN_AUTHORED_CLOUDS.md §3).
    inline constexpr uint32_t kCloudModellingGeneratorVersion = 1u;

    /// The container layout's own version, independent of the maths. Bumped when a FIELD moves.
    inline constexpr uint32_t kCloudModellingContainerVersion = 1u;

    /// The volume's shape, FIXED BY THE FORMAT. Mirrored by CLOUD_MODELLING_VOLUME_WIDTH and its two
    /// siblings in Editor/Resources/Shaders/Common/CloudAuthored.glslh, which needs them as compile-time
    /// numbers to pull a fetch in by half a texel; Desert/Tests/Engine/CloudAuthored asserts the two
    /// statements agree.
    ///
    /// The vertical axis is HEIGHT and it is the short one. 128 x 64 x 128 in RGBA8 is 4.00 MiB, and the
    /// budget arithmetic is in Docs/Clouds/CALIBRATION.md §A0.
    inline constexpr uint32_t kCloudModellingVolumeWidth  = 128u; // x, world east
    inline constexpr uint32_t kCloudModellingVolumeHeight = 64u;  // y, up
    inline constexpr uint32_t kCloudModellingVolumeDepth  = 128u; // z, world north

    inline constexpr uint32_t kCloudModellingBytesPerVoxel = 4u;

    inline constexpr uint64_t kCloudModellingVoxelBytes =
         static_cast<uint64_t>( kCloudModellingVolumeWidth ) * kCloudModellingVolumeHeight *
         kCloudModellingVolumeDepth * kCloudModellingBytesPerVoxel;

    /// A decoded volume: the recipe that produced it, and the voxels themselves.
    struct CloudModellingVolumeData
    {
        CloudModellingVolumeRecipe Recipe;
        uint32_t                   GeneratorVersion = kCloudModellingGeneratorVersion;

        /// RGBA8, four bytes per voxel, tightly packed with x varying fastest and z slowest — the layout
        /// `vkCmdCopyBufferToImage` expects for a whole-volume copy with no row padding, which is how
        /// Graphic::Image3D uploads its `Data`. Stated here because the generator and the reader both have
        /// to agree on it and neither can see the other.
        std::vector<unsigned char> Voxels;
    };

    /**
     * @brief Rejects a recipe the generator cannot honour, with the offending number in the message.
     *
     * A pure function, so the sculpting tool can grey out its Bake button for the same reason the loader
     * refuses the file, rather than the two disagreeing about what is legal.
     *
     * THE INTERESTING CHECK IS THE LAST ONE: the body, INFLATED BY THE JOIN, must fit inside the box with
     * the envelope's margin to spare. A body that touches its own boundary is a cloud with a flat face
     * where the volume was cut, and — because every sampler in this engine is REPEAT — its top would blend
     * with its own bottom through the trilinear filter. The inflation term is `BlendRadiusKm * ln(N)`,
     * which is the exact amount an exponential smooth-min of N equal distances pushes the zero set
     * outward, so the bound is arithmetic rather than a guess.
     */
    Common::BoolResultStr ValidateCloudModellingRecipe( const CloudModellingVolumeRecipe& recipe );

    /**
     * @brief Bakes a recipe into voxels.
     *
     * Pure and GPU-free: in a recipe, out 4 MiB of RGBA8 in the layout above. Roughly one second per
     * volume in a debug build, which is why the result is SHIPPED AS A FILE rather than generated at load
     * — the same decision, for the same reason, that `.dcnv` records on kCloudNoiseDefaultVolumeName.
     *
     * The construction, and why each half of it is this and not something else:
     *
     *   * EACH LUMP is an ellipsoid signed distance. Inigo Quilez's bounded form `k0*(k0-1)/k1`, which is
     *     EXACT for a sphere and a tight underestimate otherwise — an underestimate is the safe direction,
     *     because it makes the body slightly smaller than the bound Validate checked.
     *   * THE JOIN is the exponential smooth minimum, `-ln(sum(exp(-d_k/r)))*r`. Three properties, and all
     *     three are load-bearing (PLAN_AUTHORED_CLOUDS.md §3): it is COMMUTATIVE AND ASSOCIATIVE, so the
     *     order lumps were sculpted in cannot move the result and the tool need not store one; its
     *     SOFTMAX WEIGHTS are a partition of unity over the lumps, so Detail Type and Density Scale fall
     *     out of the join instead of being invented separately; and the result IS a normalised distance
     *     field, which is the exact quantity Guerrilla derive their Dimensional Profile from (deck p.85),
     *     obtained analytically rather than by a distance transform.
     *   * THE SUM IS SHIFTED by the smallest distance before it is exponentiated. Without that shift
     *     `exp(-d/r)` overflows for any point more than about 700 blend radii outside the body, which at
     *     the shipped 40 m radius is 28 km — closer than the corners of many boxes — and the answer comes
     *     back as an infinity that quantises to a solid voxel. Shifting is algebraically the identity.
     *
     * @return the voxels, or an error naming what went wrong. A recipe Validate accepts always bakes;
     *         the one error this can return that Validate cannot is the empirical one — a body that
     *         reaches the volume's boundary after all — and it names the face it touched.
     */
    Common::ResultStr<std::vector<unsigned char>>
    GenerateCloudModellingVolume( const CloudModellingVolumeRecipe& recipe );

    /**
     * @brief Serialises a volume into the container.
     *
     * Total: any @p data whose recipe Validate accepts encodes. The result is exactly
     * `kCloudModellingHeaderSize + 32 * blobCount + kCloudModellingVoxelBytes` bytes.
     */
    std::vector<unsigned char> EncodeCloudModellingVolume( const CloudModellingVolumeData& data );

    /**
     * @brief Parses a container back into a volume, or says why it could not.
     *
     * REFUSES RATHER THAN GUESSES, and each refusal names the number that was wrong: a wrong magic, an
     * unknown container version, extents that are not the format's, a truncated file, a payload whose
     * checksum disagrees, a recipe Validate rejects. A silent fallback here would be a hero cloud rendered
     * from whatever bytes happened to be in the file, which is the single hardest class of defect to trace
     * back.
     */
    Common::ResultStr<CloudModellingVolumeData>
    DecodeCloudModellingVolume( const std::vector<unsigned char>& bytes );

    /// Byte length of the container's FIXED header — the part before the blob array. Exposed because the
    /// round-trip test asserts the total file size, and a header that grew without this constant moving
    /// would pass a test that meant nothing.
    inline constexpr size_t kCloudModellingHeaderSize = 88u;

    /// Bytes per blob in the file: eight floats, in the order the encoder writes them.
    inline constexpr size_t kCloudModellingBlobBytes = 32u;

    /// The four bytes every container starts with. `DCMV` — Desert Cloud Modelling Volume, beside `DCNV`,
    /// Desert Cloud Noise Volume.
    inline constexpr char kCloudModellingMagic[4] = { 'D', 'C', 'M', 'V' };

    /// The extension the Content Browser, the file dialog and the drag-and-drop payload all agree on.
    inline constexpr const char* kCloudModellingVolumeExtension = ".dcmv";

    /**
     * @brief The recipe the shipped example volume is baked from, and the one a new asset starts from.
     *
     * A CUMULUS CONGESTUS AS A FUSED MASS: a flattened base, two shoulders, a body and a tower that grow
     * out of one another, plus a wispy tail. It is content expressed in code for exactly as long as phase
     * A0 lasts — the sculpting tool of A1 authors this struct in a panel — and it is here rather than in
     * the tool because `Tools/CloudVolumeBaker` is a mechanism and a specific cloud is content
     * (`desert-engine-dev`, "hardcoding content into a mechanism"). The precedent is
     * Assets::CloudTypeDefaultShape, which is the same shape of answer for the same reason: a scene that
     * nobody has sculpted for still has to have something to look at.
     *
     * ITS PURPOSE IS TO BE SOMETHING THE PROCEDURAL FIELD CANNOT BE. Every lump here overlaps its
     * neighbour by more than the blend radius, so the join fuses them into one connected body with a
     * single surface — which is precisely what `best - second` forbids by construction.
     */
    const CloudModellingVolumeRecipe& CloudModellingDefaultRecipe();

    /// The file name the shipped example is written under, under `Resources/Assets/Clouds/Volumes/`.
    inline constexpr const char* kCloudModellingDefaultVolumeName = "HeroCloud_Congestus.dcmv";

    /// Where the library lives, RELATIVE to the project's assets root — the string a scene stores in a
    /// hero cloud's slot. It must agree with Common::Constants::Path::CLOUD_VOLUME_PATH, which is the same
    /// directory expressed against the project root; Desert/Tests/Engine/CloudAuthored asserts that the
    /// second ends with the first rather than trusting the two to be edited together.
    inline constexpr const char* kCloudModellingAssetsRelativeDir = "Clouds/Volumes/";
} // namespace Desert::Assets
