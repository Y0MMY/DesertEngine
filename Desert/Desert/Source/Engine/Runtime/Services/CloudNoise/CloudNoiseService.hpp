#pragma once

#include <Engine/Assets/CloudNoiseVolumeAsset.hpp>
#include <Engine/Graphic/Image.hpp>

#include <memory>
#include <unordered_map>

namespace Desert::Runtime
{
    /**
     * @brief Owns the GPU side of the cloud noise volumes: one `Image3D` per loaded `.dcnv`.
     *
     * The same shape as SkyboxService and TextureService, and it exists for the same reason they do: the
     * asset layer must not know about Vulkan, and the renderer must not know how to read a file. It also
     * settles a question the old bake got wrong by construction — the volume is now uploaded ONCE and
     * shared by every view, where before each VolumetricCloudRenderer baked its own 8 MiB copy and an
     * editor with two viewports paid for two.
     *
     * THE EMPTY SLOT IS NOT AN ERROR. A component with no volume chosen renders with the built-in default,
     * because a scene that nobody has authored a volume for still has to have a sky. That is a load-bearing
     * requirement of the whole programme rather than a convenience, and it is why `Get( 0 )` is a documented
     * answer and not a null return.
     *
     * A handle that names a volume nobody registered IS an error and is logged as one, with the handle in
     * the message: falling through to the default silently would render a sky that is merely not the one
     * the artist chose, which is the least diagnosable thing this subsystem can do.
     */
    class CloudNoiseService
    {
    public:
        /// Uploads @p asset's voxels into a volume texture and caches it under the asset's handle. Called
        /// again for the same asset after a hot reload; a changed revision re-uploads, an unchanged one is
        /// a no-op.
        Common::BoolResultStr Register( const std::shared_ptr<Assets::CloudNoiseVolumeAsset>& asset );

        /// Nominates the volume the empty slot resolves to. The preloader calls this for the built-in
        /// default; a project may ship its own by giving it the same file name.
        void SetDefault( const Assets::AssetHandle& handle );

        /// The volume a component's slot resolves to, or nullptr when there is not even a default — in
        /// which case the caller must not draw, and the reason is already in the log.
        Graphic::Image3D* Get( const Assets::AssetHandle& handle );

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
            uint32_t                          Revision = 0;
        };

        std::unordered_map<Assets::AssetHandle, Entry> m_Volumes;
        Assets::AssetHandle                            m_Default{ 0 };
        uint32_t                                       m_Generation             = 0;
        bool                                           m_ReportedMissingDefault = false;
    };
} // namespace Desert::Runtime
