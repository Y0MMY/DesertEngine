#pragma once

#include <Engine/Geometry/Mesh.hpp>

namespace Desert::Runtime
{
    class MeshService
    {
    public:
        Common::BoolResultStr                  Register( const std::shared_ptr<Assets::MeshAsset>& meshAsset );
        std::shared_ptr<Mesh> Get( const Assets::AssetHandle& handle ) const;
        void                  Clear();

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Mesh>> m_Meshes;
    };
} // namespace Desert::Runtime