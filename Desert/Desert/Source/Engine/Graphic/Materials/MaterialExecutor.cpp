#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>

#include <Engine/Uniforms/UniformManager.hpp>

static constexpr uint32_t kMaxPushConstantsSize = 128U;

namespace Desert::Graphic
{
    MaterialExecutor::MaterialExecutor( std::string&& debugName, const std::shared_ptr<Shader>& shader,
                                        std::unique_ptr<MaterialBackend>&& materialBackend )
         : m_DebugName( std::move( debugName ) ), m_MaterialBackend( std::move( materialBackend ) ),
           m_Shader( shader )
    {
        m_PushConstantBuffer.Allocate( kMaxPushConstantsSize );
        InitializeProperties();
    }

    void MaterialExecutor::InitializeProperties()
    {
        auto uniformManager = Uniforms::UniformManager::Create( "Material_" + m_Shader->GetName(), m_Shader );

        for ( auto [name, index] : uniformManager->GetUniformBufferTotal().Names )
        {
            LOG_INFO( "Found {} as uniform", name );
            auto prop =
                 std::make_shared<UniformBufferProperty>( uniformManager->GetUniformBuffer( name ).GetValue() );
            m_UniformBufferPropertiesStorage.push_back( prop );
            m_UniformBufferPropertiesLookup[name] = m_UniformBufferPropertiesStorage.size() - 1;
        }

        for ( auto [name, index] : uniformManager->GetStorageBufferTotal().Names )
        {
            LOG_INFO( "Found {} as storage buffer", name );
            auto prop =
                 std::make_shared<StorageBufferProperty>( uniformManager->GetStorageBuffer( name ).GetValue() );
            m_StorageBufferPropertiesStorage.push_back( prop );
            m_StorageBufferPropertiesLookup[name] = m_StorageBufferPropertiesStorage.size() - 1;
        }

        for ( auto [name, index] : uniformManager->GetUniformImageCubeTotal().Names )
        {
            LOG_INFO( "Found {} as image cube", name );
            auto prop =
                 std::make_shared<TextureCubeProperty>( uniformManager->GetUniformImageCube( name ).GetValue() );
            m_TextureCubePropertiesStorage.push_back( prop );
            m_TextureCubePropertiesLookup[name] = m_TextureCubePropertiesStorage.size() - 1;
        }

        for ( auto [name, index] : uniformManager->GetUniformImage2DTotal().Names )
        {
            LOG_INFO( "Found {} as image 2d", name );
            auto prop =
                 std::make_shared<Texture2DProperty>( uniformManager->GetUniformImage2D( name ).GetValue() );
            m_Texture2DPropertiesStorage.push_back( prop );
            m_Texture2DPropertiesLookup[name] = m_Texture2DPropertiesStorage.size() - 1;
        }
    }

    void MaterialExecutor::Apply()
    {
        const auto& backend = m_MaterialBackend.get();

        for ( auto& prop : m_UniformBufferPropertiesStorage )
            prop->Apply( backend );

        for ( auto& prop : m_Texture2DPropertiesStorage )
            prop->Apply( backend );

        for ( auto& prop : m_TextureCubePropertiesStorage )
            prop->Apply( backend );

        for ( auto& prop : m_StorageBufferPropertiesStorage )
            prop->Apply( backend );

        backend->FlushUpdates();

        if ( m_PushConstantBuffer.Size )
        {
            // m_MaterialBackend->ApplyPushConstants( this, m_Shader-> );
        }
    }

    std::shared_ptr<MaterialExecutor> MaterialExecutor::Create( std::string&& debugName, std::string&& shaderName )
    {
        const auto& shaderLibrary = Graphic::ShaderLibrary::Get( shaderName, {} );
        if ( !shaderLibrary )
        {
            LOG_ERROR( "Could not find the shader" );
            DESERT_VERIFY( false )
        }

        const auto& shader = shaderLibrary.GetValue();

        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {
                return std::make_shared<MaterialExecutor>(
                     std::move( debugName ), shader,
                     std::move( std::make_unique<API::Vulkan::VulkanMaterialBackend>( shader ) ) );
            }
        }
        DESERT_VERIFY( false, "Unknown RendererAPI" );
        return nullptr;
    }

    std::shared_ptr<MaterialExecutor> MaterialExecutor::Create( std::string&&                  debugName,
                                                                const std::shared_ptr<Shader>& shader )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {
                return std::make_shared<MaterialExecutor>(
                     std::move( debugName ), shader,
                     std::move( std::make_unique<API::Vulkan::VulkanMaterialBackend>( shader ) ) );
            }
        }
        DESERT_VERIFY( false, "Unknown RendererAPI" );
        return nullptr;
    }

    std::shared_ptr<UniformBufferProperty>
    MaterialExecutor::GetUniformBufferProperty( const std::string& name ) const
    {
        auto it = m_UniformBufferPropertiesLookup.find( name );
        if ( it != m_UniformBufferPropertiesLookup.end() )
        {
            return m_UniformBufferPropertiesStorage[it->second];
        }
        return nullptr;
    }

    std::shared_ptr<Desert::Graphic::StorageBufferProperty>
    MaterialExecutor::GetStorageBufferProperty( const std::string& name ) const
    {
        auto it = m_StorageBufferPropertiesLookup.find( name );
        if ( it != m_StorageBufferPropertiesLookup.end() )
        {
            return m_StorageBufferPropertiesStorage[it->second];
        }
        return nullptr;
    }

    std::shared_ptr<Texture2DProperty> MaterialExecutor::GetTexture2DProperty( const std::string& name ) const
    {
        auto it = m_Texture2DPropertiesLookup.find( name );
        if ( it != m_Texture2DPropertiesLookup.end() )
        {
            return m_Texture2DPropertiesStorage[it->second];
        }
        return nullptr;
    }

    std::shared_ptr<TextureCubeProperty> MaterialExecutor::GetTextureCubeProperty( const std::string& name ) const
    {
        auto it = m_TextureCubePropertiesLookup.find( name );
        if ( it != m_TextureCubePropertiesLookup.end() )
        {
            return m_TextureCubePropertiesStorage[it->second];
        }
        return nullptr;
    }
} // namespace Desert::Graphic