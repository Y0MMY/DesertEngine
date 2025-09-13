#include "ShaderService.hpp"

namespace Desert::Runtime
{

    Common::BoolResult ShaderService::Register( const std::shared_ptr<Assets::ShaderAsset>& shaderAsset )
    {
        if ( !shaderAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Shader asset is invalid" );
        }

        m_Shaders[shaderAsset->GetMetadata().Handle] = Graphic::Shader::Create( shaderAsset );
        m_NameToHandleMap[m_Shaders[shaderAsset->GetMetadata().Handle]->GetName()] =
             shaderAsset->GetMetadata().Handle;
        return BOOLSUCCESS;
    }

    std::shared_ptr<Graphic::Shader> ShaderService::GetByName( const std::string& name ) const
    {
        auto handleIt = m_NameToHandleMap.find( name );
        if ( handleIt != m_NameToHandleMap.end() )
        {
            return Get( handleIt->second );
        }
        return nullptr;
    }

    std::shared_ptr<Desert::Graphic::Shader> ShaderService::Get( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Shaders.find( handle );
        return ( it != m_Shaders.end() ) ? it->second : nullptr;
    }

    void ShaderService::Clear()
    {
    }

} // namespace Desert::Runtime