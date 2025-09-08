#pragma once

#include <Engine/Graphic/Materials/Properties/MaterialProperty.hpp>

#include <Engine/Uniforms/StorageBuffer.hpp>

namespace Desert::Graphic
{
    class StorageBufferProperty : public MaterialProperty
    {
    public:
        StorageBufferProperty( const std::shared_ptr<Uniforms::StorageBuffer>& buffer ) : m_Buffer( buffer )
        {
        }

        void Apply( MaterialBackend* backend ) override
        {
            if ( m_Dirty )
            {
                backend->ApplyStorageBuffer( this );
                m_Dirty = false;
            }
        }

        std::unique_ptr<MaterialProperty> Clone() const override
        {
            return nullptr; // std::make_unique<UniformBufferProperty>( m_Buffer );
        }

        void SetData( const void* data, uint32_t size )
        {
            m_Buffer->SetData( data, size );
            m_Dirty = true;
        }

        const auto& GetStorageBuffer() const
        {
            return m_Buffer;
        }

    private:
        std::shared_ptr<Uniforms::StorageBuffer> m_Buffer;
    };
} // namespace Desert::Graphic