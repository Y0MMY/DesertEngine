#pragma once

#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/Graphic/DynamicResources.hpp>
#include <Engine/Graphic/ResourceLedger.hpp>

namespace Desert::Graphic
{
    class IndexBuffer : public DynamicResources
    {
    public:
        // The ledger row — see Engine/Graphic/ResourceLedger.hpp and the note on VertexBuffer.
        IndexBuffer() : m_Accounting( ResourceOwnership::Take( ResourceKind::IndexBuffer ) )
        {
        }

        virtual ~IndexBuffer()                                                 = default;
        virtual void SetData( void* data, uint32_t size, uint32_t offset = 0 ) = 0;

        void ClaimOwnership( const ResourceOwner owner, const Common::AssetHandle asset = Common::AssetHandle{} )
        {
            m_Accounting.Claim( owner, asset );
        }

        /// What this buffer costs on the device. Recorded by whoever knows — the base cannot ask
        /// GetSize() from its own constructor, the backend has not allocated yet at that point.
        void RecordDeviceBytes( const std::size_t bytes )
        {
            m_Accounting.RecordBytes( bytes );
        }
        virtual void Use( BindUsage use = BindUsage::Bind ) const              = 0;
        virtual void RT_Use( BindUsage use = BindUsage::Bind ) const           = 0;

        virtual unsigned int GetSize() const  = 0;
        virtual unsigned int GetCount() const = 0;

        [[nodiscard]] virtual Common::BoolResultStr RT_Invalidate() = 0;

        static std::shared_ptr<IndexBuffer> Create( const void* data, uint32_t size,
                                                    BufferUsage usage = BufferUsage::Static );
        static std::shared_ptr<IndexBuffer> Create( uint32_t size, BufferUsage usage = BufferUsage::Dynamic );

    private:
        ResourceOwnership m_Accounting;
    };
} // namespace Desert::Graphic