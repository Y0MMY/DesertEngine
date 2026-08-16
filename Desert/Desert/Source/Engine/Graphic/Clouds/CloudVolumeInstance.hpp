#pragma once

#include <Engine/Graphic/Clouds/CloudVolumeFormat.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

namespace Desert::Graphic
{
    /**
     * One placed hero cloud, as the shader reads it.
     *
     * The GLSL half of this layout is `struct CloudVolumeInstanceData` in
     * Editor/Resources/Shaders/Common/CloudDensityVoxel.glslh, member for member and in this order. The
     * static_asserts below are what make a divergence a build error rather than a frame in which every
     * instance samples the wrong tile — a failure with no validation message and no obvious symptom.
     *
     * std430 gives a mat4 and a vec4 the same 16-byte alignment C++ gives them here (glm's vec4 has a
     * 4-byte alignment in this build — no SIMD gentypes — but the mat4 puts the vec4 at offset 64 either
     * way), so the offsets are the same in both languages with no padding anywhere.
     */
    struct CloudVolumeInstance
    {
        /**
         * World position -> the volume's own [0,1]^3 box coordinate, ready to hand to
         * CloudVolumeAtlasUvw. Everything is premultiplied here so the shader does ONE mat4 x vec4 and
         * has the box rejection in its hands: the entity's inverse world transform, the Y-up -> Z-up
         * turn, the divide by the baked extent, and the half-unit recentre. See MakeCloudVolumeInstance.
         */
        glm::mat4 WorldToLocal{ 1.0f };

        /**
         * x = the atlas tile index this instance's `.dvol` was leased into, as a float (values 0..7 are
         *     exact in a float, and the record is read as vec4s).
         * y = CloudVolumeData::DensityScale — multiplies the baked `.b` channel.
         * z = CloudVolumeData::DetailTypeBias — offsets the baked `.g` channel.
         * w = the std430 tail. Three floats occupy four slots after a mat4 whatever we do with the
         *     fourth, and naming it as padding is honest where inventing a fourth dial would not be.
         */
        glm::vec4 Params{ 0.0f };
    };

    static_assert( sizeof( CloudVolumeInstance ) == 80 );
    static_assert( offsetof( CloudVolumeInstance, WorldToLocal ) == 0 );
    static_assert( offsetof( CloudVolumeInstance, Params ) == 64 );
    static_assert( sizeof( CloudVolumeInstance ) % 16 == 0,
                   "std430 rounds a struct in an array up to its own 16-byte alignment; a C++ record "
                   "shorter than that would make every element after the first straddle the stride the "
                   "shader indexes with." );

    /**
     * The instance budget, and it is the atlas's tile count: eight distinct hero clouds resident at once
     * (teamlead Q2 — eight tiles of 128x128x64 RGBA8 = 32.00 MiB). Several entities may reference the
     * SAME `.dvol` and share one tile, so eight is a limit on distinct SHAPES, not on placements — but
     * the instance buffer is sized for eight placements too, because a scene that wants nine of the same
     * cloud is a scene that wants a different feature (scattering), not a bigger array.
     */
    inline constexpr uint32_t kMaxCloudVolumeInstances = 8;

    /**
     * The turn from the engine's frame to the volume's own.
     *
     * LOCAL Z IS UP. The `.dvol` is a 3D texture whose third dimension is the SMALL one (128 x 128 x 64),
     * which is what makes a cloud wide and thin rather than tall and narrow, and it is the reference
     * deck's own 512 x 512 x 64 layout (p. 85). The engine is Y-up. So the entity's frame is turned by
     * exactly this rotation before the extent divide, and phase 1a's handover flagged getting this
     * silently wrong as the single most likely failure of phase 1b — hence a named function with a test
     * on it rather than three literals inside a longer expression.
     *
     * It is a PROPER rotation (determinant +1, a quarter turn about X), not an axis swap: a mirror would
     * hand every baked cloud back left-right reversed, which is a thing nobody would notice on a blob and
     * everybody would notice on an authored silhouette.
     *
     *     entity +Y (world up)  ->  volume +Z   (the thin, vertical axis)
     *     entity +Z             ->  volume -Y
     *     entity +X            ->  volume +X
     */
    inline glm::mat4 CloudVolumeUpAxisRotation()
    {
        glm::mat4 rotation( 1.0f );
        rotation[0] = glm::vec4( 1.0f, 0.0f, 0.0f, 0.0f );
        rotation[1] = glm::vec4( 0.0f, 0.0f, 1.0f, 0.0f );
        rotation[2] = glm::vec4( 0.0f, -1.0f, 0.0f, 0.0f );
        return rotation;
    }

    /**
     * Build the record for one placed hero cloud.
     *
     * @param worldTransform the entity's world matrix. Its TRANSLATION places the volume's centre, its
     *                       ROTATION turns it, and its SCALE multiplies the extent the `.dvol` was baked
     *                       to cover. That last one is the teamlead's Q2 answer in one line: the world
     *                       size of a tile is a per-instance transform, so a closer fly-by is a different
     *                       scale on the same asset rather than a different atlas.
     * @param header         the baked volume's own header — the extent is read from HERE and from nowhere
     *                       else. CloudVolumeData deliberately has no extent field; a second copy of this
     *                       number in the component would be free to disagree with the file.
     * @param tileIndex      the atlas tile the volume was leased into.
     * @param densityScale   CloudVolumeData::DensityScale. Taken as a float rather than as the component,
     *                       so this header — and the test that drives it — needs nothing from the ECS.
     * @param detailTypeBias CloudVolumeData::DetailTypeBias.
     *
     * Pure: no GPU, no globals, no clock. Which is what lets the "local Z is up" property be a test.
     */
    inline CloudVolumeInstance MakeCloudVolumeInstance( const glm::mat4&         worldTransform,
                                                        const CloudVolumeHeader& header, uint32_t tileIndex,
                                                        float densityScale, float detailTypeBias )
    {
        // Guarded because a division is what follows, and a zero extent is a NaN in every voxel of the
        // instance rather than a missing cloud. ValidateCloudVolumeHeader already rejects a non-positive
        // extent at load, so this can only fire on a header that never came from a file.
        const glm::vec3 extent( header.ExtentX > 0.0f ? header.ExtentX : 1.0f,
                                header.ExtentY > 0.0f ? header.ExtentY : 1.0f,
                                header.ExtentZ > 0.0f ? header.ExtentZ : 1.0f );

        // Read right to left: undo the entity's placement, turn Y-up into Z-up, divide by the box the
        // volume covers (so the box becomes [-0.5, 0.5]^3), and recentre onto [0, 1]^3.
        glm::mat4 recentre( 1.0f );
        recentre[3] = glm::vec4( 0.5f, 0.5f, 0.5f, 1.0f );

        glm::mat4 normalize( 1.0f );
        normalize[0][0] = 1.0f / extent.x;
        normalize[1][1] = 1.0f / extent.y;
        normalize[2][2] = 1.0f / extent.z;

        CloudVolumeInstance instance;
        instance.WorldToLocal =
             recentre * normalize * CloudVolumeUpAxisRotation() * glm::inverse( worldTransform );
        // Density Scale is repaired at the boundary rather than left to the shader's clamp: it multiplies
        // the baked channel, and a negative one would invert it rather than dim it.
        instance.Params =
             glm::vec4( static_cast<float>( tileIndex ), glm::max( densityScale, 0.0f ), detailTypeBias, 0.0f );
        return instance;
    }
} // namespace Desert::Graphic
