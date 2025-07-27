#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetEvents.hpp>

#include <Engine/Graphic/Geometry/Mesh.hpp>

namespace Desert::Assets
{
    class MeshAsset final : public AssetBase, public AssetsEventSystem
    {
    public:
        explicit MeshAsset( const AssetPriority priority, const Common::Filepath& filepath );

        virtual Common::BoolResult Load() override;
        virtual Common::BoolResult Unload() override;

        const auto GetMesh() const
        {
            return m_Mesh;
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
        std::shared_ptr<Mesh> m_Mesh;
        bool                  m_ReadyForUse = false;
    };

} // namespace Desert::Assets