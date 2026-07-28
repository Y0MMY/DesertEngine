#include "ShaderService.hpp"

namespace Desert::Runtime
{

    Common::BoolResultStr ShaderService::Register( const std::shared_ptr<Assets::ShaderAsset>& shaderAsset )
    {
        if ( !shaderAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Shader asset is invalid" );
        }

        const auto shader                            = Graphic::Shader::Create( shaderAsset );
        m_Shaders[shaderAsset->GetMetadata().Handle] = shader;
        m_NameToHandleMap[shader->GetName()]         = shaderAsset->GetMetadata().Handle;

        // DSL multi-pass shaders: every named pass is its own program, addressable as
        // "<Shader>/<Pass>" (e.g. GetByName("Unlit/Shadow")).
        for ( const auto& passName : shader->GetProgramMeta().PassNames )
        {
            auto passShader                      = Graphic::Shader::Create( shaderAsset, {}, passName );
            m_PassShaders[passShader->GetName()] = passShader;
        }

        return BOOLSUCCESS;
    }

    std::shared_ptr<Graphic::Shader> ShaderService::GetByName( const std::string& name ) const
    {
        auto handleIt = m_NameToHandleMap.find( name );
        if ( handleIt != m_NameToHandleMap.end() )
        {
            return Get( handleIt->second );
        }

        auto passIt = m_PassShaders.find( name );
        if ( passIt != m_PassShaders.end() )
        {
            return passIt->second;
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

    std::vector<std::string> ShaderService::GetAllNames() const
    {
        std::vector<std::string> names;
        names.reserve( m_NameToHandleMap.size() );
        for ( const auto& [name, handle] : m_NameToHandleMap )
            names.push_back( name );
        return names;
    }

} // namespace Desert::Runtime