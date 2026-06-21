#include "Material.hpp"
#include <Engine/Graphic/Image.hpp>

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

    void Material::RegisterProperty( IProperty* prop )
    {
        m_RegisteredProperties.push_back( prop );
    }

    // ---------------------------------------------------------------------------

    std::pair<UniformBufferProperty*, FieldProperty*> Material::FindFieldInAnyUB(
         std::string_view fieldName ) const
    {
        const std::string key( fieldName );
        for ( const auto& [ubName, idx] : m_MaterialExecutor->GetUniformBufferProperties() )
        {
            auto ubProp = m_MaterialExecutor->GetUniformBufferProperty( ubName );
            if ( !ubProp )
                continue;
            if ( auto* field = ubProp->GetField( key ) )
                return { ubProp.get(), field };
        }
        return { nullptr, nullptr };
    }

    void Material::UploadRegisteredProperties( std::unordered_set<UniformBufferProperty*>& outDirtyUBs )
    {
        for ( auto* prop : m_RegisteredProperties )
        {
            if ( !prop->IsDirty() )
                continue;

            if ( prop->GetKind() == PropertyKind::Texture2D )
            {
                void* texPtr = nullptr;
                prop->CopyValueTo( &texPtr );
                if ( texPtr )
                {
                    if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( std::string( prop->GetShaderName() ) ) )
                        texProp->SetImage( static_cast<const Image2D*>( texPtr ) );
                }
            }
            else
            {
                auto [ub, field] = FindFieldInAnyUB( prop->GetShaderName() );
                if ( field )
                {
                    std::byte buf[256] = {};
                    prop->CopyValueTo( buf );
                    field->SetRawBytes( buf, prop->GetByteSize() );
                    outDirtyUBs.insert( ub );
                }
            }

            prop->MarkClean();
        }
    }

    void Material::ApplyInstanceOverrides( const MaterialInstance*                    instance,
                                           std::unordered_set<UniformBufferProperty*>& outDirtyUBs )
    {
        const auto& props = instance->GetPropertySet();
        for ( const auto& [name, prop] : props.GetProperties() )
        {
            if ( !prop.bIsOverridden )
                continue;

            std::visit(
                 [&]( auto&& val )
                 {
                     using T = std::decay_t<decltype( val )>;
                     if constexpr ( std::is_same_v<T, void*> )
                     {
                         if ( val )
                         {
                             if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( name ) )
                                 texProp->SetImage( static_cast<const Image2D*>( val ) );
                         }
                     }
                     else
                     {
                         auto [ub, field] = FindFieldInAnyUB( name );
                         if ( field )
                         {
                             field->SetRawBytes( &val, sizeof( T ) );
                             outDirtyUBs.insert( ub );
                         }
                     }
                 },
                 prop.Value );
        }
    }

    void Material::Bind( const MaterialInstance* instance )
    {
        if ( !m_MaterialExecutor )
            return;

        std::unordered_set<UniformBufferProperty*> dirtyUBs;

        // 1. TProperty defaults → FieldProperty (only dirty ones)
        UploadRegisteredProperties( dirtyUBs );

        // 2. MaterialInstance overrides on top
        ApplyInstanceOverrides( instance, dirtyUBs );

        // 3. Flush all touched UBs: write dirty fields into the GPU buffer
        for ( auto* ub : dirtyUBs )
            ub->UpdateFields();

        OnBind( const_cast<MaterialInstance*>( instance ) );
    }

    void Material::CachePropertyNames()
    {
        if ( m_MaterialExecutor )
        {
            for ( const auto& [name, index] : m_MaterialExecutor->GetUniformBufferProperties() )
                m_PropertyNames.push_back( name );

            for ( const auto& [name, index] : m_MaterialExecutor->GetTexture2DProperties() )
                m_PropertyNames.push_back( name );

            for ( const auto& [name, index] : m_MaterialExecutor->GetTextureCubeProperties() )
                m_PropertyNames.push_back( name );
        }
    }

} // namespace Desert::Graphic
