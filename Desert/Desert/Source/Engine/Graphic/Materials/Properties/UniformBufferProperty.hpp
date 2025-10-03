#pragma once

#include <Engine/Graphic/Materials/Properties/MaterialProperty.hpp>

#include "FieldProperty.hpp"
#include <Engine/Uniforms/UniformBuffer.hpp>

namespace Desert::Graphic
{
    class UniformBufferProperty : public MaterialProperty
    {
    public:
        UniformBufferProperty( const std::shared_ptr<Uniforms::UniformBuffer>& buffer ) : m_Buffer( buffer )
        {
            m_FieldProperties.assign( buffer->GetFields().begin(), buffer->GetFields().end() );

            for ( size_t i = 0; i < m_FieldProperties.size(); ++i )
            {
                m_FieldIndexMap[m_FieldProperties[i].GetFieldInfo().Name] = i;
            }
        }

        void Apply( MaterialBackend* backend ) override
        {
            if ( true || m_Dirty )
            {
                backend->ApplyUniformBuffer( this );
                m_Dirty = true;
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
                }
            }
            m_Buffer->UnmapMemory();
            m_Dirty = true;
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
        std::vector<FieldProperty>               m_FieldProperties;
        std::unordered_map<std::string, size_t>  m_FieldIndexMap;
        std::shared_ptr<Uniforms::UniformBuffer> m_Buffer;
    };
} // namespace Desert::Graphic