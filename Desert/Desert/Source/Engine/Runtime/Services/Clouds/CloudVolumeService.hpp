#pragma once

#include <Engine/Assets/Clouds/CloudVolumeAsset.hpp>

#include <memory>
#include <unordered_map>

namespace Desert::Runtime
{
    /**
     * @brief Handle -> baked hero-cloud volume, for the renderer.
     *
     * The renderer has to turn CloudVolumeData::Volume into the voxels the atlas uploads, and the
     * AssetManager that owns them is a layer object (the Editor's, the Runtime's) that neither a render
     * system nor an ECS system may reach. This is the same crossing SkyboxService makes for the same
     * reason, kept as small as it can be: a map, one Register, one Get.
     *
     * It hands back the ASSET and not a copy of the volume: a `.dvol` is 4 MiB, the atlas copies it into
     * a tile exactly once per lease, and a second owning copy would double the resident cost of every
     * hero cloud for the lifetime of the scene.
     *
     * Register is idempotent — re-registering the same handle replaces the entry, which is what a
     * re-imported `.dvol` needs.
     */
    class CloudVolumeService
    {
    public:
        Common::BoolResultStr Register( const std::shared_ptr<Assets::CloudVolumeAsset>& volumeAsset );

        /** @brief The asset behind @p handle, or nullptr when nothing registered it. */
        std::shared_ptr<Assets::CloudVolumeAsset> Get( const Assets::AssetHandle& handle ) const;

        void Clear();

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::CloudVolumeAsset>> m_Volumes;
    };
} // namespace Desert::Runtime
