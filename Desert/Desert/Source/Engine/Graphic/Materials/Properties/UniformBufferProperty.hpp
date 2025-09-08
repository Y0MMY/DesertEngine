#pragma once

#include <Engine/Graphic/Materials/Properties/MaterialProperty.hpp>

#include <Engine/Uniforms/UniformBuffer.hpp>

namespace Desert::Graphic
{
    class UniformBufferProperty : public MaterialProperty
    {
    public:
        UniformBufferProperty( const std::shared_ptr<Uniforms::UniformBuffer>& buffer ) : m_Buffer( buffer )
        {
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

        void SetData( const void* data, uint32_t size )
        {
            m_Buffer->SetData( data, size );
            m_Dirty = true;
        }

        const auto& GetUniform() const
        {
            return m_Buffer;
        }

    private:
        std::shared_ptr<Uniforms::UniformBuffer> m_Buffer;
    };
} // namespace Desert::Graphic