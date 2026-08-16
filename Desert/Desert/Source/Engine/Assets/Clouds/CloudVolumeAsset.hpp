#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeFormat.hpp>

namespace Desert::Assets
{
    /**
     * @brief A baked hero-cloud volume (`.dvol`) as an asset.
     *
     * The asset owns the CPU-side voxels; the GPU side is Graphic::CloudVolumeAtlas, which copies them
     * into a tile. The split is deliberate — several entities can reference one volume and share a
     * single tile, so the atlas keys on the asset handle rather than on the entity.
     *
     * Unload() drops the voxels and the asset stops being ready. It does NOT touch the atlas: an asset
     * does not know which tiles reference it, and reaching into the renderer from here would be the
     * layering inversion the contract forbids. The atlas's own lease is what frees a tile.
     */
    class CloudVolumeAsset final : public AssetBase
    {
    public:
        CloudVolumeAsset( AssetPriority priority, const Common::Filepath& filepath );

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        bool IsReadyForUse() const override
        {
            return m_ReadyForUse;
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::CloudVolume;
        }

        /** @brief The decoded volume. Only meaningful while IsReadyForUse(). */
        const Graphic::CloudVolume& GetVolume() const
        {
            return m_Volume;
        }

    private:
        Graphic::CloudVolume m_Volume;
        bool                 m_ReadyForUse = false;
    };
} // namespace Desert::Assets
