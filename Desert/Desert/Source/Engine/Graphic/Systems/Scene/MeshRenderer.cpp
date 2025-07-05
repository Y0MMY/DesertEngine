#include "MeshRenderer.hpp"

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/Models/ToneMap.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResult MeshRenderer::Init( const uint32_t width, const uint32_t height,
                                           const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal )
    {
        // Setup geometry pass
        if ( !SetupGeometryPass( width, height, skyboxFramebufferExternal ) )
            return Common::MakeError( "Failed to setup geometry pass" );

        return BOOLSUCCESS;
    }

    void MeshRenderer::Shutdown()
    {
        if ( m_Framebuffer )
        {
            m_Framebuffer->Release();
            m_Framebuffer.reset();
        }

        m_RenderQueue.clear();
        m_ActiveCamera = nullptr;
    }

    void MeshRenderer::BeginScene( const Core::Camera& camera )
    {
        m_ActiveCamera = const_cast<Core::Camera*>( &camera );
        m_RenderQueue.clear();
    }

    void MeshRenderer::Submit( const DTO::MeshRenderData& renderData )
    {
        m_RenderQueue.push_back( renderData );
    }

    void MeshRenderer::EndScene()
    {
        auto& renderer = Renderer::GetInstance();
        renderer.BeginRenderPass( m_RenderPass );

        // Render all meshes in queue
        for ( const auto& renderData : m_RenderQueue )
        {
            renderData.Material->UpdateRenderParameters( *m_ActiveCamera );
            renderer.RenderMesh( m_Pipeline, renderData.Mesh, renderData.Material->GetMaterial() );
        }

        renderer.EndRenderPass();
    }

    bool MeshRenderer::SetupGeometryPass( const uint32_t width, const uint32_t height,
                                          const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal )
    {
        constexpr std::string_view debugName = "SceneGeometry";

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName                = debugName;
        fbSpec.Attachments.Attachments  = { Core::Formats::ImageFormat::DEPTH24STENCIL8 };
        fbSpec.ExternalAttachments.Load = AttachmentLoad::Load;
        fbSpec.ExternalAttachments.ExternalAttachments.push_back( skyboxFramebufferExternal );

        m_Framebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_Framebuffer->Resize( width, height );

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = m_Framebuffer;
        m_RenderPass             = RenderPass::Create( rpSpec );

        // Pipeline
        PipelineSpecification pipeSpec;
        pipeSpec.DebugName = debugName;
        pipeSpec.Layout    = { { Graphic::ShaderDataType::Float3, "a_Position" },
                               { Graphic::ShaderDataType::Float3, "a_Normal" },
                               { Graphic::ShaderDataType::Float3, "a_Tangent" },
                               { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                               { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };

        m_Shader             = Graphic::Shader::Create( "StaticPBR.glsl" );
        pipeSpec.Shader      = m_Shader;
        pipeSpec.Framebuffer = m_Framebuffer;
        pipeSpec.Renderpass  = m_RenderPass;

        m_Pipeline = Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        return true;
    }

} // namespace Desert::Graphic::System