#pragma once

#include <Engine/Graphic/Materials/Properties/MaterialProperty.hpp>

#include <Engine/ShaderResources/StorageBuffer.hpp>

namespace Desert::Graphic
{
    class StorageBufferProperty : public MaterialProperty
    {
    public:
        StorageBufferProperty( const std::shared_ptr<ShaderResources::StorageBuffer>& buffer ) : m_Buffer( buffer )
        {
        }

        void Apply( MaterialBackend* backend ) override
        {
            if ( IsDirty() )
            {
                backend->ApplyStorageBuffer( this );
                MarkClean();
            }
        }

        std::unique_ptr<MaterialProperty> Clone() const override
        {
            return nullptr; // std::make_unique<UniformBufferProperty>( m_Buffer );
        }

        void SetRawData( const void* data, uint32_t size )
        {
            m_Buffer->SetData( data, size );
            MarkDirty(); // every slot owes itself this write
        }

        // Replace the reflection-created buffer with an externally-owned one (e.g. a correctly-sized,
        // compute-written SSBO for GPU-culled grass). Marks dirty so the next Apply rebinds the new VkBuffer.
        void SetBuffer( const std::shared_ptr<ShaderResources::StorageBuffer>& buffer )
        {
            m_Buffer     = buffer;
            MarkDirty(); // every slot owes itself this write
        }

        const auto& GetStorageBuffer() const
        {
            return m_Buffer;
        }

    private:
        std::shared_ptr<ShaderResources::StorageBuffer> m_Buffer;
    };
} // namespace Desert::Graphic