#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>

#include <Engine/Uniforms/UniformManager.hpp>

static constexpr uint32_t kMaxPushConstantsSize = 128U;

namespace Desert::Graphic
{
    Material::Material( std::string&& debugName, const std::shared_ptr<Shader>& shader,
                        std::unique_ptr<MaterialBackend>&& materialBackend )
         : m_DebugName( std::move( debugName ) ), m_MaterialBackend( std::move( materialBackend ) ),
           m_Shader( shader )
    {
        m_PushConstantBuffer.Allocate(kMaxPushConstantsSize);
        InitializeProperties();
    }

    void Material::InitializeProperties()
    {
        auto uniformManager = Uniforms::UniformManager::Create( "Material_" + m_Shader->GetName(), m_Shader );

        for ( auto [name, index] : uniformManager->GetUniformBufferTotal().Names )
        {
            LOG_INFO( "Found {} as uniform", name );
            m_UniformBufferProperties[name] =
                 std::make_shared<UniformBufferProperty>( uniformManager->GetUniformBuffer( name ).GetValue() );
        }

        for ( auto [name, index] : uniformManager->GetUniformImageCubeTotal().Names )
        {
            LOG_INFO( "Found {} as image cube", name );
            m_TextureCubeProperties[name] =
                 std::make_shared<TextureCubeProperty>( uniformManager->GetUniformImageCube( name ).GetValue() );
        }

        for ( auto [name, index] : uniformManager->GetUniformImage2DTotal().Names )
        {
            LOG_INFO( "Found {} as image 2d", name );
            m_Texture2DProperties[name] =
                 std::make_shared<Texture2DProperty>( uniformManager->GetUniformImage2D( name ).GetValue() );
        }
    }

    void Material::Apply()
    {
        m_MaterialBackend->ApplyProperties( this );
        if ( m_PushConstantBuffer.Size )
        {
            // m_MaterialBackend->ApplyPushConstants( this, m_Shader-> );
        }
    }

    std::shared_ptr<Material> Material::Create( std::string&& debugName, const std::shared_ptr<Shader>& shader )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {
                return std::make_shared<Material>(
                     std::move( debugName ), shader,
                     std::move( std::make_unique<API::Vulkan::VulkanMaterialBackend>() ) );
            }
        }
        DESERT_VERIFY( false, "Unknown RendererAPI" );
        return nullptr;
    }

    std::shared_ptr<Desert::Graphic::UniformBufferProperty>
    Material::GetUniformBufferProperty( const std::string& name ) const
    {
        return m_UniformBufferProperties.at( name );
    }

    std::shared_ptr<Desert::Graphic::Texture2DProperty>
    Material::GetTexture2DProperty( const std::string& name ) const
    {
        return m_Texture2DProperties.at( name );
    }

    std::shared_ptr<Desert::Graphic::TextureCubeProperty>
    Material::GetTextureCubeProperty( const std::string& name ) const
    {
        return m_TextureCubeProperties.at( name );
    }

} // namespace Desert::Graphic