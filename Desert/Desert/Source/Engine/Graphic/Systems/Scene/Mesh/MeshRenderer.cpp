#include "MeshRenderer.hpp"

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/Models/ToneMap.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResult MeshRenderer::Initialize( const uint32_t width, const uint32_t height )
    {
        const auto& compositeFramebuffer = m_CompositeFramebuffer.lock();
        if ( !compositeFramebuffer )
        {
            DESERT_VERIFY( false );
        }

        // Setup geometry pass
        if ( !SetupGeometryPass( width, height, compositeFramebuffer) )
            return Common::MakeError( "Failed to setup geometry pass" );

        if ( !SetupOutlinePass( width, height, compositeFramebuffer) )
            return Common::MakeError( "Failed to setup outline pass" );

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

    bool MeshRenderer::SetupOutlinePass( const uint32_t width, const uint32_t height,
                                         const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal )
    {

        // Shader
        m_OutlineShader = Graphic::Shader::Create( "Outline.glsl" );

        PipelineSpecification outlinePipeSpec;
        outlinePipeSpec.DebugName = "OutlinePipeline";
        outlinePipeSpec.Layout    = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                      { Graphic::ShaderDataType::Float3, "a_Normal" },
                                      { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                      { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                      { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };

        outlinePipeSpec.DepthWriteEnabled  = true;
        outlinePipeSpec.DepthTestEnabled   = false;
        outlinePipeSpec.StencilTestEnabled = true;
        outlinePipeSpec.StencilFront       = { .FailOp      = StencilOp::Keep,
                                               .PassOp      = StencilOp::Replace,
                                               .DepthFailOp = StencilOp::Keep,
                                               .CompareOp   = CompareOp::NotEqual,
                                               .CompareMask = 0xFF,
                                               .WriteMask   = 0xFF,
                                               .Reference   = 1 };
        outlinePipeSpec.StencilBack        = outlinePipeSpec.StencilFront;
        outlinePipeSpec.DepthTestEnabled   = false;

        outlinePipeSpec.StencilBack = outlinePipeSpec.StencilFront;

        outlinePipeSpec.DepthCompareOp = CompareOp::LessOrEqual;
        outlinePipeSpec.CullMode       = CullMode::None;
        outlinePipeSpec.Shader         = m_OutlineShader;
        outlinePipeSpec.Framebuffer    = m_Framebuffer;
        outlinePipeSpec.PolygonMode    = PrimitivePolygonMode::Wireframe;
        outlinePipeSpec.Topology       = PrimitiveTopology::LineStrip;
        outlinePipeSpec.LineWidth      = 5.0f;

        m_OutlinePipeline = Pipeline::Create( outlinePipeSpec );
        m_OutlinePipeline->Invalidate();

        m_OutlineMaterial = std::make_unique<MaterialOutline>();

        return true;
    }

    void MeshRenderer::PrepareFrame( const Core::Camera& camera, const std::optional<Environment>& environment )
    {
        m_ActiveCamera = const_cast<Core::Camera*>( &camera );
        m_RenderQueue.clear();
        m_Environment = environment;
    }

    void MeshRenderer::AddMesh( const MeshRenderData& renderData )
    {
        m_RenderQueue.push_back( renderData );
    }

    void MeshRenderer::AddLight( const glm::vec3& directionLight )
    {
        m_DirectionLight = directionLight;
    }

    std::optional<Models::PBR::PBRTextures> MeshRenderer::PreparePBRTextures() const
    {
        if ( !m_Environment || !m_Environment->IrradianceMap || !m_Environment->PreFilteredMap )
            return std::nullopt;

        return Models::PBR::PBRTextures{ .IrradianceMap  = m_Environment->IrradianceMap,
                                         .PreFilteredMap = m_Environment->PreFilteredMap };
    }

    void MeshRenderer::ProcessSystem()
    {
        auto&      renderer = Renderer::GetInstance();
        const auto textures = PreparePBRTextures();
        renderer.BeginRenderPass( m_RenderPass );

        for ( const auto& renderData : m_RenderQueue )
        {
            renderData.Material->UpdateRenderParameters( *m_ActiveCamera, renderData.Transform, m_DirectionLight,
                                                         textures );
            renderer.RenderMesh( m_Pipeline, renderData.Mesh, renderData.Material->GetMaterial() );
        }

        if ( !m_OutlineDraw )
        {
            return;
        }

        for ( const auto& renderData : m_RenderQueue )
        {

            m_OutlineMaterial->UpdateRenderParameters( *m_ActiveCamera, renderData.Transform, m_OutlineWidth,
                                                       m_OutlineColor );

            renderer.RenderMesh( m_OutlinePipeline, m_RenderQueue[0].Mesh,
                                 m_OutlineMaterial->GetMaterialExecutor() );
        }

        renderer.EndRenderPass();
    }

    bool MeshRenderer::SetupGeometryPass( const uint32_t width, const uint32_t height,
                                          const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal )
    {
        constexpr std::string_view debugName = "SceneGeometry";

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName               = debugName;
        fbSpec.Attachments.Attachments = { Core::Formats::ImageFormat::DEPTH24STENCIL8 };

        fbSpec.ExternalAttachments.ColorAttachments.push_back( { .SourceFramebuffer = skyboxFramebufferExternal,
                                                                 .AttachmentIndex   = 0,
                                                                 .Load              = AttachmentLoad::Load } );

        m_Framebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_Framebuffer->Resize( width, height );

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = m_Framebuffer;

        // Pipeline
        PipelineSpecification pipeSpec;
        pipeSpec.DebugName = debugName;
        pipeSpec.Layout    = { { Graphic::ShaderDataType::Float3, "a_Position" },
                               { Graphic::ShaderDataType::Float3, "a_Normal" },
                               { Graphic::ShaderDataType::Float3, "a_Tangent" },
                               { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                               { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };

        pipeSpec.StencilTestEnabled = true;
        pipeSpec.StencilFront       = { .FailOp      = StencilOp::Replace,
                                        .PassOp      = StencilOp::Replace,
                                        .DepthFailOp = StencilOp::Replace,
                                        .CompareOp   = CompareOp::Always,
                                        .CompareMask = 0xFF,
                                        .WriteMask   = 0xFF,
                                        .Reference   = 1 };
        pipeSpec.StencilBack        = pipeSpec.StencilFront;

        pipeSpec.DepthCompareOp = CompareOp::LessOrEqual;
        pipeSpec.CullMode       = CullMode::None;
        pipeSpec.Shader         = Graphic::Shader::Create( "StaticPBR.glsl" );
        pipeSpec.Framebuffer    = m_Framebuffer;

        m_RenderPass = RenderPass::Create( rpSpec );
        m_Pipeline   = Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        return true;
    }
} // namespace Desert::Graphic::System