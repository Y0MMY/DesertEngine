#pragma once

#include <Engine/Geometry/Mesh.hpp>

#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Runtime
{
    class MeshService
    {
    public:
        Common::BoolResultStr Register( const std::shared_ptr<Assets::MeshAsset>& meshAsset );
        Assets::AssetHandle   RegisterProcedural( const std::shared_ptr<Mesh>& mesh );
        
        Mesh*                 Get( const Assets::AssetHandle& handle ) const;
        Assets::MeshAsset*    GetAsset( const Assets::AssetHandle& handle ) const;

        void                  Clear();
        std::optional<bool>   IsSkinned( const Assets::AssetHandle& handle ) const;

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Mesh>>              m_Meshes;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::MeshAsset>> m_MeshAssets;
    };
} // namespace Desert::Runtime