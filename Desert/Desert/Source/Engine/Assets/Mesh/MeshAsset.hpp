#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <Engine/Graphic/Mesh.hpp>

namespace Desert::Assets
{
    class MeshAsset final : public AssetBase
    {
    public:
        using AssetBase::AssetBase;

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

    private:
        std::shared_ptr<Mesh> m_Mesh;
        bool m_ReadyForUse = false;
    };

} // namespace Desert::Assets