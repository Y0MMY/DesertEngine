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
        Mesh*                 Get( const Assets::AssetHandle& handle ) const; // todo: raw ptr
        void                  Clear();
        std::optional<bool>                                   /*TOOD: error class*/
        IsSkinned( const Assets::AssetHandle& handle ) const; // invalid handle returns nullopt
    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Mesh>> m_Meshes;
    };
} // namespace Desert::Runtime