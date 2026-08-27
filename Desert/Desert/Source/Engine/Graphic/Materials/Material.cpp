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

    void Material::UploadRegisteredProperties()
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
                if ( ub && field )
                {
                    std::byte buf[256] = {};
                    prop->CopyValueTo( buf );
                    ub->WriteField( field, buf, prop->GetByteSize() );
                }
            }

            prop->MarkClean();
        }
    }

    void Material::ApplyInstanceOverrides( const MaterialInstance* instance )
    {
        const auto& props = instance->GetPropertySet();
        for ( const auto& [name, prop] : props.GetProperties() )
        {
            if ( !prop.bIsOverridden )
                continue;

            // Local alias: AppleClang 15 can't capture structured bindings in lambdas yet.
            const auto& propName = name;
            std::visit(
                 [&]( auto&& val )
                 {
                     using T = std::decay_t<decltype( val )>;
                     if constexpr ( std::is_same_v<T, void*> )
                     {
                         if ( val )
                         {
                             if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( propName ) )
                                 texProp->SetImage( static_cast<const Image2D*>( val ) );
                         }
                     }
                     else
                     {
                         auto [ub, field] = FindFieldInAnyUB( propName );
                         if ( ub && field )
                         {
                             ub->WriteField( field, &val, sizeof( T ) );
                         }
                     }
                 },
                 prop.Value );
        }
    }

    // Flush every UB that still has dirty fields. A field stays dirty for frames-in-flight frames, so
    // this writes the new data into EACH per-frame-in-flight buffer copy (not just the copy for the
    // frame it first changed on). Flushing only the UBs "touched" this frame would leave the other
    // copies at their initial zero contents, so any frame presenting those indices would render the
    // mesh black/garbage — the source of the per-frame flicker.
    //
    // Whole-filled buffers report no dirty fields and are therefore skipped without being listed
    // anywhere; that used to be DataDrivenMaterial's job to remember.
    void Material::FlushFieldFilledUniformBuffers()
    {
        if ( !m_MaterialExecutor )
            return;

        for ( const auto& [ubName, idx] : m_MaterialExecutor->GetUniformBufferProperties() )
        {
            auto ubProp = m_MaterialExecutor->GetUniformBufferProperty( ubName );
            if ( ubProp && ubProp->HasDirtyFields() )
                ubProp->UpdateFields();
        }
    }

    void Material::Bind( const MaterialInstance* instance )
    {
        if ( !m_MaterialExecutor )
            return;

        // 1. TProperty defaults → FieldProperty (only dirty ones)
        UploadRegisteredProperties();

        // 2. MaterialInstance overrides on top
        ApplyInstanceOverrides( instance );

        // 3. Get them onto the GPU.
        FlushFieldFilledUniformBuffers();

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
