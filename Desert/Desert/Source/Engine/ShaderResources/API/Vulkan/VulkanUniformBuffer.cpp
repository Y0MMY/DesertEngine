#include "VulkanUniformBuffer.hpp"

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Core/EngineContext.hpp>
#include <Engine/ShaderResources/BufferCopyLayout.hpp>

namespace Desert::ShaderResources::API::Vulkan
{

    VulkanUniformBuffer::VulkanUniformBuffer( const ShaderLayout::UniformBuffer& uniform )
         : UniformBuffer( uniform )
    {
        // A constructor has no channel, so the answer is KEPT rather than logged and forgotten: every
        // SetData and EnsureMapped afterwards hands the caller this refusal, with its original reason.
        // Logged once as well, here, because this is where the numbers are and because a buffer that
        // never built is worth a line even if nothing writes to it this session.
        m_Built = RT_Invalidate();
        if ( !m_Built.IsSuccess() )
            LOG_ERROR( "[UniformBuffer] '{}' was not built: {}", m_UniformModel.Name, m_Built.GetError() );
    }

    VulkanUniformBuffer::~VulkanUniformBuffer()
    {
        Release();
    }

    uint32_t VulkanUniformBuffer::CopyIndex( uint32_t frameIndex )
    {
        return BufferCopyIndex( frameIndex, EngineContext::GetInstance().GetActiveRendererSlot(),
                                EngineContext::kMaxRendererSlots );
    }

    void VulkanUniformBuffer::Release()
    {
        if ( m_Buffers.empty() )
            return;

        auto allocator = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                  EngineContext::GetInstance().GetRendererContext() )
                              ->GetVulkanAllocator()
                              .get();

        for ( uint32_t i = 0; i < static_cast<uint32_t>( m_Buffers.size() ); ++i )
        {
            // Unmap BEFORE the buffer is queued for destruction, exactly as before — the mapping's own
            // destructor does it now, so an early return cannot skip it.
            if ( i < m_Mappings.size() )
                m_Mappings[i].Unmap();
            if ( m_MemoryAllocs[i] )
            {
                allocator->RT_DestroyBuffer( m_Buffers[i], m_MemoryAllocs[i] );
                m_Buffers[i]      = VK_NULL_HANDLE;
                m_MemoryAllocs[i] = nullptr;
            }
        }

        m_Buffers.clear();
        m_MemoryAllocs.clear();
        m_DescriptorInfos.clear();
        m_Mappings.clear();
    }

    Common::BoolResultStr VulkanUniformBuffer::RT_Invalidate()
    {
        Release();

        // One copy per (frame in flight x renderer slot). The frame dimension keeps a buffer the GPU is
        // still reading from being overwritten; the SLOT dimension keeps a second view from overwriting
        // the first one's camera, lights and shadow state inside the same frame — that is the whole point
        // of this change. A uniform block is a few hundred bytes to a few KB, so the extra copies cost
        // kilobytes per material.
        const uint32_t framesInFlight = EngineContext::GetInstance().GetMaxFramesInFlight();
        const uint32_t copies         = BufferCopyCount( framesInFlight, EngineContext::kMaxRendererSlots );

        m_Buffers.resize( copies, VK_NULL_HANDLE );
        m_MemoryAllocs.resize( copies, nullptr );
        m_DescriptorInfos.resize( copies );
        m_Mappings.resize( copies );

        auto vulkanContext = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                      EngineContext::GetInstance().GetRendererContext() );

        for ( uint32_t i = 0; i < copies; ++i )
        {
            VkBufferCreateInfo bufferInfo = {};
            bufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.usage              = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.size               = m_UniformModel.Size;

            const auto allocatedBuffer = vulkanContext->GetVulkanAllocator()->RT_AllocateBuffer(
                 std::format( "{}-UniformBuffer-Frame{}-Slot{}", m_UniformModel.Name,
                              i / EngineContext::kMaxRendererSlots, i % EngineContext::kMaxRendererSlots ),
                 bufferInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, m_Buffers[i] );

            // REFUSED, NOT SKIPPED. This was `continue`, which left m_Buffers[i] as VK_NULL_HANDLE and
            // m_DescriptorInfos[i] zeroed while the function went on to report the buffer built. The
            // descriptor is then written into a set and handed to the driver, and the only symptom is
            // that one frame in flight — one in two, or one in six with the slot dimension — renders
            // wrong. Release() below puts the object back to empty so that a half-built buffer is not a
            // state anything else can observe.
            if ( !allocatedBuffer.IsSuccess() )
            {
                Release();
                return Common::MakeFormattedError<bool>( "uniform buffer '{}' copy {} of {}: {}",
                                                         m_UniformModel.Name, i, copies,
                                                         allocatedBuffer.GetError() );
            }
            m_MemoryAllocs[i] = allocatedBuffer.GetValue();

            m_DescriptorInfos[i].buffer = m_Buffers[i];
            m_DescriptorInfos[i].offset = 0;
            m_DescriptorInfos[i].range  = m_UniformModel.Size;

            // Persistently mapped for the lifetime of this buffer (CPU_TO_GPU stays mappable); the
            // actual unmap happens in Release(), through the mapping's own destructor.
            m_Mappings[i] = vulkanContext->GetVulkanAllocator()->MapMemory( m_MemoryAllocs[i] );

            // A COPY THAT DID NOT MAP IS THE SAME FAILURE AS ONE THAT DID NOT ALLOCATE, and it used to
            // be a log line while the object still claimed to be built. Every write to this copy would
            // refuse for the lifetime of the buffer — MappedMemory sees to that — so "built" was simply
            // untrue for one frame in flight.
            const auto cleared = m_Mappings[i].Fill( 0, m_UniformModel.Size );
            if ( !cleared.IsSuccess() )
            {
                const std::string reason = cleared.GetError();
                Release();
                return Common::MakeFormattedError<bool>( "uniform buffer '{}' copy {} of {}: {}",
                                                         m_UniformModel.Name, i, copies, reason );
            }
        }

        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanUniformBuffer::SetData( const void* data, uint32_t size, uint32_t offset )
    {
        // THE CAUSE, NOT THE CONSEQUENCE. A buffer that failed to build has no copies at all, so the
        // index check below would answer "there is no copy 0", which is true and tells the reader
        // nothing. The constructor's own reason is what they need.
        if ( !m_Built.IsSuccess() )
            return Common::MakeFormattedError<bool>( "uniform buffer '{}' was never built: {}",
                                                     m_UniformModel.Name, m_Built.GetError() );

        const uint32_t index = CopyIndex( EngineContext::GetInstance().GetCurrentFrameIndex() );

        if ( index >= m_Mappings.size() )
            return Common::MakeFormattedError<bool>( "uniform buffer '{}' has no copy {} to write ({} exist)",
                                                     m_UniformModel.Name, index, m_Mappings.size() );

        const auto wrote = m_Mappings[index].Write( data, size, offset );
        if ( !wrote.IsSuccess() )
            return Common::MakeFormattedError<bool>( "uniform buffer '{}' copy {}: {}", m_UniformModel.Name, index,
                                                     wrote.GetError() );

        // NO LOG ON THIS PATH ANY MORE, and that is the change rather than an omission. The log line
        // that stood here ran per frame per material and had to guard itself against burying itself; the
        // caller now receives the refusal and decides once, where it knows what the write was for.
        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanUniformBuffer::EnsureMapped()
    {
        if ( !m_Built.IsSuccess() )
            return Common::MakeFormattedError<bool>( "uniform buffer '{}' was never built: {}",
                                                     m_UniformModel.Name, m_Built.GetError() );

        const uint32_t index = CopyIndex( EngineContext::GetInstance().GetCurrentFrameIndex() );

        if ( index >= m_Mappings.size() )
            return Common::MakeFormattedError<bool>( "uniform buffer '{}' has no copy {} ({} exist)",
                                                     m_UniformModel.Name, index, m_Mappings.size() );

        // THE RETRY THAT STOOD HERE IS GONE, AND THE REASON IS THE FIX ABOVE. It re-mapped a copy whose
        // map had failed, because RT_Invalidate used to tolerate exactly that state — a buffer that was
        // "built" with one copy unmapped. It cannot be now: RT_Invalidate is all or nothing, so a built
        // buffer has every copy mapped and this question has one answer. Keeping the retry would be a
        // second path to a state that no longer exists, which is the "two paths, one untested" shape §4
        // of the contract is about.
        if ( !m_Mappings[index].IsMapped() )
            return Common::MakeFormattedError<bool>(
                 "uniform buffer '{}' copy {} reports itself unmapped although the buffer was built: {}",
                 m_UniformModel.Name, index, m_Mappings[index].GetRefusal() );

        return BOOLSUCCESS;
    }

} // namespace Desert::ShaderResources::API::Vulkan