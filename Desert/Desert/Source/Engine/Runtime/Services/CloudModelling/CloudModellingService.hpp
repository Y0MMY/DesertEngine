#pragma once

#include <Engine/Assets/CloudModellingVolumeAsset.hpp>
#include <Engine/Graphic/Image.hpp>

#include <memory>
#include <unordered_map>

namespace Desert::Runtime
{
    /**
     * @brief Owns the GPU side of the sculpted cloud bodies: one `Image3D` per loaded `.dcmv`.
     *
     * The same shape as CloudNoiseService next door, and it exists for the same reason: the asset layer
     * must not know about Vulkan, and the renderer must not know how to read a file. It also means a
     * volume is uploaded ONCE and shared by every view, so an editor with two viewports pays for one
     * 4 MiB volume rather than two.
     *
     * THERE IS NO DEFAULT HERE, and that is the difference from CloudNoiseService. An empty noise slot
     * must resolve to something because every scene with clouds needs a shape; an empty HERO CLOUD slot
     * means the artist has not chosen a body, and the honest answer is that there is no hero cloud —
     * inventing one would put a cloud in the sky nobody authored. `Get( 0 )` therefore returns nullptr
     * without a word, and a handle that names a volume nobody registered returns nullptr WITH one.
     *
     * ONE VOLUME PER FRAME IS ALL THE MARCH CAN BIND, and that is stated here rather than discovered: the
     * shader has a single `sampler3D` for the authored producer, so every live instance shares it. The
     * renderer picks the first ready volume and logs the entities whose choice it could not honour.
     * Several instances of the SAME body are free and are the ordinary case; several DIFFERENT bodies
     * need the atlas, which is phase A2.
     */
    class CloudModellingService
    {
    public:
        /// Uploads @p asset's voxels into a volume texture and caches it under the asset's handle. Called
        /// again for the same asset after a hot reload; a changed revision re-uploads, an unchanged one is
        /// a no-op.
        Common::BoolResultStr Register( const std::shared_ptr<Assets::CloudModellingVolumeAsset>& asset );

        /// The volume a component's slot names, or nullptr when the slot is empty or the file was never
        /// registered — the second of which is logged with the handle in the message.
        Graphic::Image3D* Get( const Assets::AssetHandle& handle );

        /// The authored SIZE of that volume in kilometres, which the renderer needs to build the
        /// instance's transform and its bounds. Returned beside the image rather than looked up from the
        /// asset again, because the two must describe the same file and a second lookup is a second
        /// chance to describe a different one.
        glm::vec3 GetSizeKm( const Assets::AssetHandle& handle );

        /// Bumped whenever any registered volume is re-uploaded. The renderer compares it to decide
        /// whether the descriptor it bound last frame still points at the same image.
        uint32_t GetGeneration() const
        {
            return m_Generation;
        }

        void Clear();

    private:
        struct Entry
        {
            std::shared_ptr<Graphic::Image3D> Volume;
            glm::vec3                         SizeKm{ 0.0f };
            uint32_t                          Revision = 0;
        };

        std::unordered_map<Assets::AssetHandle, Entry> m_Volumes;
        uint32_t                                       m_Generation = 0;
    };
} // namespace Desert::Runtime
