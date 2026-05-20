#include "MaterialInstance.hpp"
#include "Material.hpp"

namespace Desert::Graphic
{
    MaterialInstance::MaterialInstance( Material* parentMaterial, const std::string& name )
         : m_ParentMaterial( parentMaterial ), m_Name( name )
    {
        DESERT_VERIFY( parentMaterial, "Parent material cannot be null" );

        // Copy default properties from parent material
        m_Properties.CopyFrom( parentMaterial->GetDefaultProperties() );
    }

    float MaterialInstance::GetFloat( const std::string& name, float defaultValue ) const
    {
        auto value = ResolveProperty( name );
        if ( std::holds_alternative<float>( value ) )
            return std::get<float>( value );
        if ( std::holds_alternative<int>( value ) )
            return static_cast<float>( std::get<int>( value ) );
        return defaultValue;
    }

    int MaterialInstance::GetInt( const std::string& name, int defaultValue ) const
    {
        auto value = ResolveProperty( name );
        if ( std::holds_alternative<int>( value ) )
            return std::get<int>( value );
        if ( std::holds_alternative<float>( value ) )
            return static_cast<int>( std::get<float>( value ) );
        return defaultValue;
    }

    bool MaterialInstance::GetBool( const std::string& name, bool defaultValue ) const
    {
        auto value = ResolveProperty( name );
        if ( std::holds_alternative<bool>( value ) )
            return std::get<bool>( value );
        return defaultValue;
    }

    glm::vec2 MaterialInstance::GetVec2( const std::string& name, const glm::vec2& defaultValue ) const
    {
        auto value = ResolveProperty( name );
        if ( std::holds_alternative<glm::vec2>( value ) )
            return std::get<glm::vec2>( value );
        return defaultValue;
    }

    glm::vec3 MaterialInstance::GetVec3( const std::string& name, const glm::vec3& defaultValue ) const
    {
        auto value = ResolveProperty( name );
        if ( std::holds_alternative<glm::vec3>( value ) )
            return std::get<glm::vec3>( value );
        return defaultValue;
    }

    glm::vec4 MaterialInstance::GetVec4( const std::string& name, const glm::vec4& defaultValue ) const
    {
        auto value = ResolveProperty( name );
        if ( std::holds_alternative<glm::vec4>( value ) )
            return std::get<glm::vec4>( value );
        return defaultValue;
    }

    glm::mat4 MaterialInstance::GetMat4( const std::string& name, const glm::mat4& defaultValue ) const
    {
        auto value = ResolveProperty( name );
        if ( std::holds_alternative<glm::mat4>( value ) )
            return std::get<glm::mat4>( value );
        return defaultValue;
    }

    void* MaterialInstance::GetTexture( const std::string& name ) const
    {
        auto value = ResolveProperty( name );
        if ( std::holds_alternative<void*>( value ) )
            return std::get<void*>( value );
        return nullptr;
    }

    void MaterialInstance::SetFloat( const std::string& name, float value )
    {
        m_Properties.SetProperty( name, value, MaterialPropertyType::Float );
        m_bNeedsApply = true;
        PropagateToChildren();
    }

    void MaterialInstance::SetInt( const std::string& name, int value )
    {
        m_Properties.SetProperty( name, value, MaterialPropertyType::Int );
        m_bNeedsApply = true;
        PropagateToChildren();
    }

    void MaterialInstance::SetBool( const std::string& name, bool value )
    {
        m_Properties.SetProperty( name, value, MaterialPropertyType::Bool );
        m_bNeedsApply = true;
        PropagateToChildren();
    }

    void MaterialInstance::SetVec2( const std::string& name, const glm::vec2& value )
    {
        m_Properties.SetProperty( name, value, MaterialPropertyType::Vec2 );
        m_bNeedsApply = true;
        PropagateToChildren();
    }

    void MaterialInstance::SetVec3( const std::string& name, const glm::vec3& value )
    {
        m_Properties.SetProperty( name, value, MaterialPropertyType::Vec3 );
        m_bNeedsApply = true;
        PropagateToChildren();
    }

    void MaterialInstance::SetVec4( const std::string& name, const glm::vec4& value )
    {
        m_Properties.SetProperty( name, value, MaterialPropertyType::Vec4 );
        m_bNeedsApply = true;
        PropagateToChildren();
    }

    void MaterialInstance::SetMat4( const std::string& name, const glm::mat4& value )
    {
        m_Properties.SetProperty( name, value, MaterialPropertyType::Mat4 );
        m_bNeedsApply = true;
        PropagateToChildren();
    }

    void MaterialInstance::SetTexture( const std::string& name, void* texture )
    {
        m_Properties.SetProperty( name, texture, MaterialPropertyType::Texture );
        m_bNeedsApply = true;
        PropagateToChildren();
    }

    void
    MaterialInstance::SetParameters( const std::vector<std::pair<std::string, MaterialPropertyValue>>& params )
    {
        for ( const auto& [name, value] : params )
        {
            // Auto-detect type (simplified - you'd want proper type detection)
            if ( std::holds_alternative<float>( value ) )
                SetFloat( name, std::get<float>( value ) );
            else if ( std::holds_alternative<int>( value ) )
                SetInt( name, std::get<int>( value ) );
            else if ( std::holds_alternative<bool>( value ) )
                SetBool( name, std::get<bool>( value ) );
            else if ( std::holds_alternative<glm::vec3>( value ) )
                SetVec3( name, std::get<glm::vec3>( value ) );
            else if ( std::holds_alternative<void*>( value ) )
                SetTexture( name, std::get<void*>( value ) );
        }
    }

    void MaterialInstance::SetParameters( const MaterialPropertySet& properties )
    {
        m_Properties.CopyFrom( properties );
        m_bNeedsApply = true;
        PropagateToChildren();
    }

    MaterialInstancePtr MaterialInstance::CreateChildInstance( const std::string& name )
    {
        auto child              = std::make_shared<MaterialInstance>( m_ParentMaterial, name );
        child->m_ParentInstance = shared_from_this();
        child->m_Properties.CopyFrom( m_Properties );
        m_ChildInstances.push_back( child );
        return child;
    }

    bool MaterialInstance::HasParameter( const std::string& name ) const
    {
        return m_Properties.HasProperty( name );
    }

    void MaterialInstance::Apply()
    {
        if ( !m_bNeedsApply )
            return;

        if ( m_ParentMaterial )
        {
            m_ParentMaterial->Bind( this );
        }

        m_bNeedsApply = false;
    }

    MaterialPropertyValue MaterialInstance::ResolveProperty( const std::string& name ) const
    {
        // Check local override first
        if ( m_Properties.HasProperty( name ) )
            return m_Properties.GetProperty( name );

        // Check parent instance
        auto parent = m_ParentInstance.lock();
        if ( parent )
            return parent->ResolveProperty( name );

        // Check parent material defaults
        if ( m_ParentMaterial && m_ParentMaterial->GetDefaultProperties().HasProperty( name ) )
            return m_ParentMaterial->GetDefaultProperties().GetProperty( name );

        return float( 0.0f );
    }

    void MaterialInstance::PropagateToChildren()
    {
        for ( auto& child : m_ChildInstances )
        {
            child->MarkNeedsApply();
        }
    }
} // namespace Desert::Graphic