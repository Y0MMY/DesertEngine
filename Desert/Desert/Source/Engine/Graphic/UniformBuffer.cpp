#include <Engine/Graphic/UniformBuffer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <numeric>

namespace Desert::Graphic
{

    std::shared_ptr<UniformBuffer> UniformBuffer::Create( uint32_t size, uint32_t binding )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanUniformBuffer>( size, binding );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

    void UniformBufferManager::AddBuffer( std::shared_ptr<UniformBuffer>&& buffer, const std::string& name )
    {
        const auto index = m_UniformBuffers.size();
        m_UniformBuffers.push_back( std::move( buffer ) );
        m_NameMap[name] = index;
    }

    Common::Result<std::shared_ptr<UniformBuffer>>
    UniformBufferManager::GetUniformBuffer( const std::string& name ) const
    {
        auto it = m_NameMap.find( name );
        if ( it == m_NameMap.end() )
        {

            return Common::MakeFormattedError<std::shared_ptr<UniformBuffer>>(
                 "Uniform '{}' not found in material", name );
        }

        return Common::MakeSuccess( m_UniformBuffers[it->second] );
    }

    std::shared_ptr<UniformBufferManager> UniformBufferManager::Create( const std::string_view         debugName,
                                                                        const std::shared_ptr<Shader>& shader )
    {
        return std::make_shared<UniformBufferManager>( debugName, shader );
    }

    UniformBufferManager::UniformBufferManager( const std::string_view         debugName,
                                                const std::shared_ptr<Shader>& shader )
         : m_DebugName( debugName )
    {
        const auto& models = shader->GetUniformModels();
        m_UniformBuffers.reserve( models.size() );
        m_NameMap.reserve( models.size() );

        for ( const auto& model : models )
        {
            AddBuffer( UniformBuffer::Create( model.Size, model.BindingPoint ), model.Name );
        }
    }

} // namespace Desert::Graphic
