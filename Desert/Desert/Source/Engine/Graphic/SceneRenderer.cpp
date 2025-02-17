#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.h>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    void SceneRenderer::Init()
    {
        REGISTER_EVENT( this, OnEvent );

        /* RenderPassSpecification renderPassSpec;
         renderPassSpec.DebugName         = "tes6t";

         m_TESTFramebuffer = Graphic::Framebuffer::Create( {} );
         m_TESTFramebuffer->Resize( 1, 1 );

         m_TESTShader = Graphic::Shader::Create( "test.glsl" );
         Graphic::PipelineSpecification spec;

         renderPassSpec.TargetFramebuffer = m_TESTFramebuffer;
         m_TESTRenderPass = Graphic::RenderPass::Create( renderPassSpec );

         float* vertices = new float[8]{
              -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,
         };

         m_TESTVertexbuffer = Graphic::VertexBuffer::Create( vertices, 8 * 4 );
         m_TESTVertexbuffer->Invalidate();
         spec.Layout = { { Graphic::ShaderDataType::Float3, "a_Position" } };

         spec.DebugName   = "test temp";
         spec.Framebuffer = m_TESTFramebuffer;
         spec.Shader      = m_TESTShader;
         m_TESTPipeline   = Graphic::Pipeline::Create( spec );
         m_TESTPipeline->Invalidate();
 */

        //

        // m_TESTCompShader = Graphic::Shader::Create( "comute_test.glsl" );

        //

        RenderPassSpecification renderPassSpecSkybox;
        renderPassSpecSkybox.DebugName = "Skybox";

        m_TESTFramebufferSkybox = Graphic::Framebuffer::Create( {} );

        uint32_t width  = EngineContext::GetInstance().GetCurrentWindowWidth();
        uint32_t height = EngineContext::GetInstance().GetCurrentWindowHeight();

        m_TESTFramebufferSkybox->Resize(width, height);

        m_TESTShaderSkybox = Graphic::Shader::Create( "skybox.glsl" );
        Graphic::PipelineSpecification spec;

        renderPassSpecSkybox.TargetFramebuffer = m_TESTFramebufferSkybox;
        m_TESTRenderPassSkybox                 = Graphic::RenderPass::Create( renderPassSpecSkybox );

        struct QuadVertex
        {
            glm::vec3 Position;
            glm::vec2 TexCoord;
        };

        std::array<QuadVertex, 4> data;
        data[0].Position = { -1, 1, 0 };
        data[1].TexCoord = { 0, 1 };

        data[1].Position = { -1, -1, 0 };
        data[1].TexCoord = { 0, 0 };

        data[2].Position = { 1, 1, 0 };
        data[2].TexCoord = { 1, 1 };

        data[3].Position = { 1, -1, 0 };
        data[3].TexCoord = { 1, 0 };

        m_TESTVertexbufferSkybox = Graphic::VertexBuffer::Create( data.data(), 4 * sizeof( QuadVertex ) );
        m_TESTVertexbufferSkybox->Invalidate();
        uint32_t* indices = new uint32_t[6]{
             0, 1, 2, 1, 2, 3,
        };
        m_TESTIndexbufferSkybox = Graphic::IndexBuffer::Create( indices, 6 * sizeof( unsigned int ) );
        m_TESTIndexbufferSkybox->Invalidate();

        spec.Layout = { { Graphic::ShaderDataType::Float3, "a_Position" },
                        { Graphic::ShaderDataType::Float2, "a_TexCoord" } };

        spec.DebugName       = "skybox";
        spec.Framebuffer     = m_TESTFramebufferSkybox;
        spec.Shader          = m_TESTShaderSkybox;
        m_TESTPipelineSkybox = Graphic::Pipeline::Create( spec );
        m_TESTPipelineSkybox->Invalidate();

        // m_TESTTextureSkybox = TextureCube::Create("Arches_E_PineTree_Radiance.tga");
    }

    void SceneRenderer::BeginFrame()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.BeginFrame();
        renderer.BeginRenderPass( m_TESTRenderPassSkybox );
        renderer.TEST_DrawTriangle( m_TESTVertexbufferSkybox, m_TESTIndexbufferSkybox, m_TESTPipelineSkybox );
    }

    void SceneRenderer::EndFrame()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.EndRenderPass();
        renderer.EndFrame();
    }

    void SceneRenderer::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowResize>( [this]( Common::EventWindowResize& e )
                                                        { return this->OnWindowResize( e ); } );
    }

    bool SceneRenderer::OnWindowResize( Common::EventWindowResize& e )
    {
        auto&                                              renderer = Renderer::GetInstance();
        std::vector<std::shared_ptr<Graphic::Framebuffer>> framebuffers;
        framebuffers.push_back( m_TESTFramebufferSkybox );
        renderer.ResizeWindowEvent( e.width, e.height, framebuffers );
        return false;
    }

} // namespace Desert::Graphic