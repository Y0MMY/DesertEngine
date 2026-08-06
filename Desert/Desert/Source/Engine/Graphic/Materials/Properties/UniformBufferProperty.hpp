#pragma once

#include <Engine/Graphic/Materials/Properties/MaterialProperty.hpp>

#include "FieldProperty.hpp"
#include <Engine/ShaderResources/UniformBuffer.hpp>

namespace Desert::Graphic
{
    class UniformBufferProperty : public MaterialProperty
    {
    public:
        UniformBufferProperty( const std::shared_ptr<ShaderResources::UniformBuffer>& buffer ) : m_Buffer( buffer )
        {
            m_FieldProperties.assign( buffer->GetFields().begin(), buffer->GetFields().end() );

            for ( size_t i = 0; i < m_FieldProperties.size(); ++i )
            {
                m_FieldIndexMap[m_FieldProperties[i].GetFieldInfo().Name] = i;
            }
        }

        void Apply( MaterialBackend* backend ) override
        {
            if ( IsDirty() )
            {
                backend->ApplyUniformBuffer( this );
                MarkClean();
            }
        }

        std::unique_ptr<MaterialProperty> Clone() const override
        {
            return nullptr; // std::make_unique<UniformBufferProperty>( m_Buffer );
        }

        void UpdateFields()
        {
            [[maybe_unused]] const auto mapPtr = m_Buffer->MapMemory();
            for ( auto& field : m_FieldProperties )
            {
                if ( field.IsDirty() )
                {
                    m_Buffer->SetData( field.GetLocalData().Data, field.GetFieldInfo().Size,
                                       field.GetFieldInfo().Offset );
                    field.MarkClean();
                }
            }
            m_Buffer->UnmapMemory();
            MarkDirty(); // every slot owes itself this write
        }

        // A field stays dirty for frames-in-flight frames, so this returns true until every per-frame
        // buffer copy has received the new data. Used to keep flushing this UB across the whole window.
        bool HasDirtyFields() const
        {
            for ( const auto& field : m_FieldProperties )
            {
                if ( field.IsDirty() )
                    return true;
            }
            return false;
        }

        void SetRawData( const std::byte* data, size_t size )
        {
            DESERT_VERIFY( data != nullptr, "UniformBufferProperty::SetRawData: data is null" );

            const size_t bufferSize = m_Buffer->GetSize();
            if ( size > bufferSize ) // the sizes identify WHICH UB overflowed (VERIFY drops its args)
                LOG_ERROR( "[UB] SetRawData overflow: writing {} bytes into a {}-byte buffer ({} field(s), "
                           "first '{}')",
                           size, bufferSize, m_FieldProperties.size(),
                           m_FieldProperties.empty() ? "?" : m_FieldProperties[0].GetFieldInfo().Name );
            DESERT_VERIFY( size <= bufferSize,
                           "UniformBufferProperty::SetRawData: data size ({}) exceeds buffer size ({})", size,
                           bufferSize );

            [[maybe_unused]] const auto mapPtr = m_Buffer->MapMemory();

            m_Buffer->SetData( reinterpret_cast<const void*>( data ), size, 0 );

            m_Buffer->UnmapMemory();

            MarkDirty(); // every slot owes itself this write
        }

        const auto& GetUniform() const
        {
            return m_Buffer;
        }

        FieldProperty* GetField( const std::string& name )
        {
            auto it = m_FieldIndexMap.find( name );
            if ( it != m_FieldIndexMap.end() )
            {
                return &m_FieldProperties[it->second];
            }
            return nullptr;
        }

        const FieldProperty* GetField( const std::string& name ) const
        {
            auto it = m_FieldIndexMap.find( name );
            if ( it != m_FieldIndexMap.end() )
            {
                return &m_FieldProperties[it->second];
            }
            return nullptr;
        }

    private:
        std::vector<FieldProperty>                      m_FieldProperties;
        std::unordered_map<std::string, size_t>         m_FieldIndexMap;
        std::shared_ptr<ShaderResources::UniformBuffer> m_Buffer;
    };
} // namespace Desert::Graphic