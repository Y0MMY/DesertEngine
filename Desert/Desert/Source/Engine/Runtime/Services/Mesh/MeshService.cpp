#include "MeshService.hpp"

#include <Engine/Geometry/MeshFactory.hpp>

namespace Desert::Runtime
{
    Common::BoolResultStr MeshService::Register( const std::shared_ptr<Assets::MeshAsset>& meshAsset )
    {
        if ( !meshAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Mesh asset is invalid" );
        }

        m_Meshes[meshAsset->GetMetadata().Handle] = Graphic::MeshFactory::Create( meshAsset );
        return BOOLSUCCESS;
    }

    std::shared_ptr<Desert::Mesh> MeshService::Get( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Meshes.find( handle );
        return ( it != m_Meshes.end() ) ? it->second : nullptr;
    }

    void MeshService::Clear()
    {
    }

    std::optional<bool> MeshService::IsSkinned( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Meshes.find( handle );
        return ( it != m_Meshes.end() ) ? std::make_optional( it->second->IsSkinned() ) : std::nullopt;
    }

} // namespace Desert::Runtime
