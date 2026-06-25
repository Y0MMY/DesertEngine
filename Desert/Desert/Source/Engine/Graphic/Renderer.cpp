#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>

namespace Desert::Graphic
{
    static RendererAPI* s_RendererAPI = nullptr;

    [[nodiscard]] Common::BoolResultStr Renderer::InitGraphicAPI()
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {
                s_RendererAPI = new Graphic::API::Vulkan::VulkanRendererAPI(
                     EngineContext::GetInstance().GetWindow() );

                break;
            }
        }
        s_RendererAPI->Init();

        return BOOLSUCCESS;
    }

    Common::BoolResultStr Renderer::Init()
    {
        // Pull in the generated reflection TU (static lib would otherwise strip it) so all
        // REFLECT()-annotated types are registered before any editor / shader-upload use.
        Reflection::ForceLinkGeneratedReflection();
        LOG_INFO( "[Reflection] {} reflected type(s) registered",
                  Reflection::ReflectionRegistry::Get().All().size() );

        const auto& init = InitGraphicAPI();
        if ( !init )
        {
            return Common::MakeError( init.GetError() );
        }

        Graphic::TextureSpecification spec;
        spec.GenerateMips = false;
        // Path must be rooted at the resource-textures dir (CWD-relative), like every other texture load
        // (e.g. EnvironmentManager). The bare "PBR/BRDF_LUT.tga" resolved to <cwd>/PBR/... → not found →
        // null texture → the split-sum LUT bind fell back to the white dummy (IBL specular too bright).
        m_BRDFTexture =
             Texture2D::Create( spec, Common::Filepath( "Resources/Textures" ) / "PBR/BRDF_LUT.tga" ).ExtractValue();
        if ( !m_BRDFTexture )
            LOG_ERROR( "Failed to load BRDF LUT (Resources/Textures/PBR/BRDF_LUT.tga) — IBL specular will be wrong" );

        return Common::MakeSuccess( true );
    }

    [[nodiscard]] Common::BoolResultStr Renderer::EndFrame()
    {
        return s_RendererAPI->EndFrame();
    }

    [[nodiscard]] Common::BoolResultStr Renderer::BeginFrame()
    {
        return s_RendererAPI->BeginFrame();
    }

    uint32_t Renderer::GetCurrentFrameIndex()
    {
        return EngineContext::GetInstance().GetCurrentFrameIndex();
    }

    Common::Memory::CommandBuffer& Renderer::GetRenderCommandQueue()
    {
        static Common::Memory::CommandBuffer cmdBuffer;
        return cmdBuffer;
    }

    void Renderer::PresentFinalImage()
    {
        s_RendererAPI->PresentFinalImage();
    }

    void Renderer::PrepareNextFrame()
    {
        s_RendererAPI->PrepareNextFrame();
    }

    void Renderer::SubmitFullscreenQuad( const GraphicsPipeline* pipeline, const MaterialExecutor* materialExecutor )
    {
        s_RendererAPI->SubmitFullscreenQuad( pipeline, materialExecutor );
    }

    void Renderer::SubmitLines( const GraphicsPipeline* pipeline, uint32_t vertexCount, float lineWidth,
                                const MaterialExecutor* materialExecutor )
    {
        s_RendererAPI->SubmitLines( pipeline, vertexCount, lineWidth, materialExecutor );
    }

    void Renderer::DispatchComputeInFrame( const ComputePipeline* pipeline, uint32_t groupCountX,
                                           uint32_t groupCountY, uint32_t groupCountZ )
    {
        s_RendererAPI->DispatchComputeInFrame( pipeline, groupCountX, groupCountY, groupCountZ );
    }

    void Renderer::ComputeImageBeginWrite( Image2D* image )
    {
        s_RendererAPI->ComputeImageBeginWrite( image );
    }

    void Renderer::ComputeImageEndWrite( Image2D* image )
    {
        s_RendererAPI->ComputeImageEndWrite( image );
    }

    void Renderer::BeginRenderPass( const RenderPass* renderPass, bool clearFrame )
    {
        s_RendererAPI->BeginRenderPass( renderPass, clearFrame );
    }

    void Renderer::BeginSwapChainRenderPass()
    {
        s_RendererAPI->BeginSwapChainRenderPass();
    }

    void Renderer::EndRenderPass()
    {
        s_RendererAPI->EndRenderPass();
    }

    void Renderer::ResizeWindowEvent( uint32_t width, uint32_t height )
    {
        s_RendererAPI->ResizeWindowEvent( width, height );
    }

    void Renderer::WaitDeviceIdle()
    {
        s_RendererAPI->WaitDeviceIdle();
    }

    void Renderer::RecreateImageSamplers()
    {
        // Idle first: we destroy/recreate VkSamplers that in-flight frames may still reference.
        WaitDeviceIdle();
        auto* imageService = Runtime::ResourceRegistry::GetImageService();
        for ( const auto& image : imageService->All() )
        {
            if ( auto* vulkanImage = dynamic_cast<API::Vulkan::IVulkanImage*>( image.get() ) )
                vulkanImage->RecreateSampler();
        }
        // Next frame, MaterialExecutor::Apply rebinds the new samplers into descriptor sets.
    }

    std::shared_ptr<Framebuffer> Renderer::GetCompositeFramebuffer()
    {
        return s_RendererAPI->GetCompositeFramebuffer();
    }

    void Renderer::RenderMesh( const GraphicsPipeline* pipeline, const Mesh* mesh, const glm::mat4 transform,
                               const MaterialExecutor* materialExecutor )
    {
        s_RendererAPI->RenderMesh( pipeline, mesh, transform, materialExecutor );
    }

    const std::shared_ptr<Desert::Graphic::Texture2D>& Renderer::GetBRDFTexture() const
    {
        return m_BRDFTexture;
    }

    void Renderer::Shutdown()
    {
        s_RendererAPI->Shutdown();
        FallbackTextures::Get().Release();

        delete s_RendererAPI;
    }

    Desert::Graphic::RendererAPI* Renderer::GetRendererAPI() const
    {
        return s_RendererAPI;
    }
} // namespace Desert::Graphic
