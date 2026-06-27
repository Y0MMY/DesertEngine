#include "MaterialService.hpp"

#include <Engine/Graphic/Materials/MaterialFactory.hpp>

namespace Desert::Runtime
{
    Common::BoolResultStr MaterialService::Register( const std::shared_ptr<Assets::MaterialAsset>& materialAsset )
    {
        if ( !materialAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Material asset is invalid" );
        }

        auto handle   = materialAsset->GetMetadata().Handle;
        auto material = Graphic::MaterialFactory::CreateMaterial( materialAsset.get() );

        m_Materials[handle] = material;

        m_ExternalToInternal[materialAsset->GetMaterialUUID()] = handle;

        return BOOLSUCCESS;
    }

    Graphic::Material* MaterialService::Get( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Materials.find( handle );
        return ( it != m_Materials.end() ) ? it->second.get() : nullptr;
    }

    void MaterialService::Clear()
    {
    }

    Graphic::Material* MaterialService::GetByExternalHandle( const Common::UUID& handle ) const
    {
        auto it = m_ExternalToInternal.find( handle );
        if ( it == m_ExternalToInternal.end() )
        {
            return nullptr;
        }

        return Get( it->second );
    }

    Assets::AssetHandle MaterialService::GetAssetHandleByExternal( const Common::UUID& uuid ) const
    {
        auto it = m_ExternalToInternal.find( uuid );
        if ( it == m_ExternalToInternal.end() )
        {
            return Common::UUID::Null();
        }

        return it->second;
    }

} // namespace Desert::Runtime
