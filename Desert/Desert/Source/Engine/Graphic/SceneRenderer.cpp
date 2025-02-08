#include <Engine/Graphic/SceneRenderer.hpp>

namespace Desert::Graphic
{
    void SceneRenderer::Init()
    {
        RenderPassSpecification renderPassSpec;
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





        //



        m_TESTCompShader = Graphic::Shader::Create( "comute_test.glsl" );






    }

    void SceneRenderer::BeginFrame()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.BeginFrame();
        renderer.BeginRenderPass( m_TESTRenderPass );
        renderer.TEST_DrawTriangle( m_TESTVertexbuffer, m_TESTPipeline );
    }

    void SceneRenderer::EndFrame()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.EndRenderPass();
        renderer.EndFrame();
    }

} // namespace Desert::Graphic