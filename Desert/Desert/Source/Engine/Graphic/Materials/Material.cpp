#include "Material.hpp"

namespace Desert::Graphic
{

    Material::Material( std::string&& debugName, std::string&& shaderName )
         : m_MaterialExecutor(
                std::move( Graphic::MaterialExecutor::Create( std::move( debugName ), std::move( shaderName ) ) ) )

    {
        CachePropertyNames();
    }

    MaterialInstancePtr Material::CreateInstance( const std::string& name )
    {
        return std::make_shared<MaterialInstance>(
             this, name.empty() ? m_MaterialExecutor->GetDubugName() + "_Instance" : name );
    }

    void Material::SetDefaultParameter( const std::string& name, const MaterialPropertyValue& value,
                                        MaterialPropertyType type )
    {
        m_DefaultProperties.SetProperty( name, value, type );
    }

    void Material::SetFloat( const std::string& propertyName, float value )
    {
        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( propertyName ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &value ), sizeof( float ) );
        }
        else
        {
            LOG_WARN( "Material [{0}]: Property '{1}' (Float) not found in shader.", m_MaterialExecutor->GetDubugName(), propertyName );
        }
    }

    void Material::SetInt( const std::string& propertyName, int value )
    {
        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( propertyName ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &value ), sizeof( int ) );
        }
        else
        {
            LOG_WARN( "Material [{0}]: Property '{1}' (Int) not found in shader.", m_MaterialExecutor->GetDubugName(), propertyName );
        }
    }

    void Material::SetVec3( const std::string& propertyName, const glm::vec3& value )
    {
        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( propertyName ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &value ), sizeof( glm::vec3 ) );
        }
        else
        {
            LOG_WARN( "Material [{0}]: Property '{1}' (Vec3) not found in shader.", m_MaterialExecutor->GetDubugName(), propertyName );
        }
    }

    void Material::SetVec4( const std::string& propertyName, const glm::vec4& value )
    {
        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( propertyName ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &value ), sizeof( glm::vec4 ) );
        }
        else
        {
            LOG_WARN( "Material [{0}]: Property '{1}' (Vec4) not found in shader.", m_MaterialExecutor->GetDubugName(), propertyName );
        }
    }

    void Material::SetMat4( const std::string& propertyName, const glm::mat4& value )
    {
        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( propertyName ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &value ), sizeof( glm::mat4 ) );
        }
        else
        {
            LOG_WARN( "Material [{0}]: Property '{1}' (Mat4) not found in shader.", m_MaterialExecutor->GetDubugName(), propertyName );
        }
    }

    void Material::SetTexture( const std::string& propertyName, Texture2D* texture )
    {
        if ( auto prop = m_MaterialExecutor->GetTexture2DProperty( propertyName ) )
        {
            // prop->SetImage( texture );
        }
        else
        {
            LOG_WARN( "Material [{0}]: Texture property '{1}' (2D) not found in shader.", m_MaterialExecutor->GetDubugName(), propertyName );
        }
    }

    void Material::SetTexture( const std::string& propertyName, TextureCube* texture )
    {
        if ( auto prop = m_MaterialExecutor->GetTextureCubeProperty( propertyName ) )
        {
            // prop->SetTexture( texture );
        }
        else
        {
            LOG_WARN( "Material [{0}]: Texture property '{1}' (Cube) not found in shader.", m_MaterialExecutor->GetDubugName(), propertyName );
        }
    }

    void Material::Bind( const MaterialInstance* instance )
    {
        if ( !m_MaterialExecutor )
            return;

        // Apply all properties from instance
        const auto& props = instance->GetPropertySet();
        for ( const auto& [name, prop] : props.GetProperties() )
        {
            if ( prop.bIsOverridden )
            {
                ApplyPropertyToExecutor( name, prop );
            }
        }

        OnBind( const_cast<MaterialInstance*>( instance ) );
    }

    void Material::ApplyPropertyToExecutor( const std::string&                           name,
                                            const MaterialPropertySet::MaterialProperty& property )
    {
        switch ( property.Type )
        {
            case MaterialPropertyType::Float:
                if ( auto val = std::get_if<float>( &property.Value ) )
                    SetFloat( name, *val );
                break;
            case MaterialPropertyType::Int:
                if ( auto val = std::get_if<int>( &property.Value ) )
                    SetInt( name, *val );
                break;
            case MaterialPropertyType::Vec3:
                if ( auto val = std::get_if<glm::vec3>( &property.Value ) )
                    SetVec3( name, *val );
                break;
            case MaterialPropertyType::Vec4:
                if ( auto val = std::get_if<glm::vec4>( &property.Value ) )
                    SetVec4( name, *val );
                break;
            case MaterialPropertyType::Mat4:
                if ( auto val = std::get_if<glm::mat4>( &property.Value ) )
                    SetMat4( name, *val );
                break;
            case MaterialPropertyType::Texture:
                if ( auto val = std::get_if<void*>( &property.Value ) )
                {
                    // Try both texture types
                    if ( auto tex2D = static_cast<Texture2D*>( *val ) )
                        SetTexture( name, tex2D );
                    else if ( auto texCube = static_cast<TextureCube*>( *val ) )
                        SetTexture( name, texCube );
                }
                break;
            default:
                break;
        }
    }

    void Material::CachePropertyNames()
    {
        if ( m_MaterialExecutor )
        {
            // Cache uniform buffer property names
            for ( const auto& [name, index] : m_MaterialExecutor->GetUniformBufferProperties() )
            {
                m_PropertyNames.push_back( name );
            }

            // Cache texture property names
            for ( const auto& [name, index] : m_MaterialExecutor->GetTexture2DProperties() )
            {
                m_PropertyNames.push_back( name );
            }

            for ( const auto& [name, index] : m_MaterialExecutor->GetTextureCubeProperties() )
            {
                m_PropertyNames.push_back( name );
            }
        }
    }

} // namespace Desert::Graphic