#include "EditorLayer.hpp"

namespace Desert
{
    EditorLayer::EditorLayer( const std::string& layerName ) : Common::Layer( layerName )
    {
    }

    void EditorLayer::OnAttach()
    {

        Graphic::RenderPassSpecification renderPassSpec;
        renderPassSpec.DebugName         = "tes6t";
        renderPassSpec.TargetFramebuffer = m_Framebuffer;

        m_Framebuffer = Graphic::Framebuffer::Create( {} );
        m_Framebuffer->Resize( 1, 1 );

        m_Shader = Graphic::Shader::Create( "test.glsl" );
        Graphic::PipelineSpecification spec;

        m_RenderPass = Graphic::RenderPass::Create( renderPassSpec );

        float* vertices = new float[9]{
             0.0f,  -0.5f, 0.0f, // Нижний угол (по оси Y)
             0.5f,  0.5f,  0.0f, // Верхний правый угол
             -0.5f, 0.5f,  0.0f  // Верхний левый угол
        };

        m_Vertexbuffer = Graphic::VertexBuffer::Create( vertices, 9 * 4 );
        m_Vertexbuffer->Invalidate();
        spec.Layout = { { Graphic::ShaderDataType::Float3, "a_Position" } };

        spec.DebugName   = "test temp";
        spec.Framebuffer = m_Framebuffer;
        spec.Shader      = m_Shader;
        m_Pipeline       = Graphic::Pipeline::Create( spec );
        m_Pipeline->Invalidate();
    }

    void EditorLayer::OnUpdate( Common::Timestep ts )
    {
        Graphic::Renderer::GetInstance().BeginFrame();
        Graphic::Renderer::GetInstance().BeginRenderPass( m_RenderPass );
        Graphic::Renderer::GetInstance().TEST_DrawTriangle( m_Vertexbuffer, m_Pipeline );
        Graphic::Renderer::GetInstance().EndRenderPass();
        Graphic::Renderer::GetInstance().EndFrame();
    }

} // namespace Desert