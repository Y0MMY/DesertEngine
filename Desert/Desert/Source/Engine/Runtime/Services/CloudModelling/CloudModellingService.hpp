#pragma once

#include <Engine/Assets/CloudModellingVolumeAsset.hpp>
#include <Engine/Graphic/Image.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace Desert::Runtime
{
    /// The image the march samples every hero cloud of a frame through, and how many bodies are in it.
    /// A `Volume` of nullptr means no body was asked for; the caller binds the fallback image, which it
    /// must do in that case rather than binding nothing — a declared `sampler3D` with no image is an
    /// INVALID descriptor set, and this engine's compute path answers one by skipping the whole dispatch.
    struct CloudModellingAtlasBinding
    {
        Graphic::Image3D* Volume    = nullptr;
        uint32_t          SlabCount = 0;
    };

    /**
     * @brief Owns the GPU side of the sculpted cloud bodies: ONE `Image3D` holding the bodies a frame
     *        actually needs, laid end to end along the depth axis.
     *
     * The same shape as CloudNoiseService next door, and it exists for the same reason: the asset layer
     * must not know about Vulkan, and the renderer must not know how to read a file.
     *
     * THERE IS NO DEFAULT HERE, and that is the difference from CloudNoiseService. An empty noise slot
     * must resolve to something because every scene with clouds needs a shape; an empty HERO CLOUD slot
     * means the artist has not chosen a body, and the honest answer is that there is no hero cloud —
     * inventing one would put a cloud in the sky nobody authored. `HasBody( 0 )` is therefore false
     * without a word, and a handle that names a volume nobody registered is false WITH one.
     *
     * WHY AN ATLAS AND NOT ONE IMAGE PER BODY. A0 bound a single `sampler3D` and could draw one body per
     * frame; several DIFFERENT bodies need several volumes reachable from one dispatch, and this engine's
     * shader reflection refuses arrays of descriptors in so many words (VulkanShaderReflection.cpp). The
     * version that compiles without touching the descriptor machinery is one image, addressed by
     * arithmetic — see the note beside CLOUD_MODELLING_ATLAS_MAX_SLABS in Common/CloudAuthored.glslh.
     *
     * IT IS BUILT ON DEMAND AND HOLDS ONLY WHAT THE SCENE USES. A project may carry fifty `.dcmv` files;
     * a frame pays 4.00 MiB for each body an entity actually names, not for the library. A fixed
     * eight-slab atlas would cost 32.00 MiB in every scene with one hero cloud in it, and against decision
     * D-9's 64 MiB that is the difference between a subsystem that fits at 1920x1080 and one that does
     * not.
     */
    class CloudModellingService
    {
    public:
        /// Keeps @p asset so its voxels can be laid into an atlas. The bytes are NOT copied: the asset
        /// holds 4 MiB of them already and a second copy would be a second thing to keep in step.
        Common::BoolResultStr Register( const std::shared_ptr<Assets::CloudModellingVolumeAsset>& asset );

        /// Whether a body is loaded for @p handle. An empty slot is silence; a handle nobody registered is
        /// logged, because the artist chose a body and it is not there.
        bool HasBody( const Assets::AssetHandle& handle );

        /// The authored SIZE of that volume in kilometres, which the renderer needs to build the
        /// instance's transform and its bounds. Returned beside the image rather than looked up from the
        /// asset again, because the two must describe the same file and a second lookup is a second
        /// chance to describe a different one.
        glm::vec3 GetSizeKm( const Assets::AssetHandle& handle );

        /**
         * @brief The atlas holding exactly @p bodies, in that order — built if the request has changed
         *        since the last one, reused otherwise.
         *
         * @param bodies  distinct, registered handles, at most Graphic::kCloudModellingAtlasMaxSlabs of
         *                them. The caller owns the de-duplication because the caller is what knows which
         *                entity asked for what, and slab i is bodies[i].
         *
         * REBUILT ONLY WHEN THE REQUEST OR A REVISION CHANGES. Uploading 4 MiB per body every frame would
         * cost more than the march does; uploading when the scene's set of hero bodies changes costs it
         * once, where the scene is already being loaded.
         */
        CloudModellingAtlasBinding EnsureAtlas( const std::vector<Assets::AssetHandle>& bodies );

        /// Bumped whenever the atlas is rebuilt. The renderer compares it to decide whether the descriptor
        /// it bound last frame still points at the same image.
        uint32_t GetGeneration() const
        {
            return m_Generation;
        }

        void Clear();

    private:
        struct Entry
        {
            std::shared_ptr<Assets::CloudModellingVolumeAsset> Asset;
            glm::vec3                                          SizeKm{ 0.0f };
            uint32_t                                           Revision = 0;
        };

        std::unordered_map<Assets::AssetHandle, Entry> m_Volumes;

        std::shared_ptr<Graphic::Image3D> m_Atlas;

        /// What the live atlas was built from: slab i is m_AtlasSlabs[i] at revision m_AtlasRevisions[i].
        /// Both are compared against the next request, and a difference in EITHER is a rebuild — a hot
        /// reload changes the second without touching the first.
        std::vector<Assets::AssetHandle> m_AtlasSlabs;
        std::vector<uint32_t>            m_AtlasRevisions;

        uint32_t m_Generation = 0;
    };
} // namespace Desert::Runtime
