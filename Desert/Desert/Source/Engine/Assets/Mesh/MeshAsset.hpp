#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetEvents.hpp>

namespace Desert::Assets
{
    class MeshAsset final : public AssetBase, public AssetsEventSystem
    {
    public:
        explicit MeshAsset( const AssetPriority priority, const Common::Filepath& filepath );

        virtual Common::BoolResult Load() override;
        virtual Common::BoolResult Unload() override;

        const auto& GetRawData() const
        {
            return m_RawData;
        }

        virtual bool IsReadyForUse() const
        {
            return m_ReadyForUse;
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Mesh;
        }

    private:
        bool                 m_ReadyForUse = false;
        std::vector<uint8_t> m_RawData;
    };

} // namespace Desert::Assets