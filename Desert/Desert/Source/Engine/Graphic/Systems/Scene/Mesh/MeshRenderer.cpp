#include "MeshRenderer.hpp"

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/Materials/Models/ToneMap.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResult MeshRenderer::Initialize()
    {
        const auto& compositeFramebuffer = m_TargetFramebuffer.lock();
        const auto& renderGraph          = m_RenderGraph.lock();
        if ( !compositeFramebuffer || !renderGraph )
        {
            DESERT_VERIFY( false );
        }

        // Setup geometry pass
        if ( !SetupGeometryPass( compositeFramebuffer, renderGraph ) )
            return Common::MakeError( "Failed to setup geometry pass" );

        if ( !SetupOutlinePass( compositeFramebuffer, renderGraph ) )
            return Common::MakeError( "Failed to setup outline pass" );

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = "SceneGeometry";
        rpSpec.TargetFramebuffer = compositeFramebuffer;

        renderGraph->AddPass(
             "SceneGeometry",
             [this]()
             {
                 const auto& camera = m_SceneRenderer->GetMainCamera().lock();
                 if ( camera )
                 {
                     auto&      renderer = Renderer::GetInstance();
                     const auto textures = PreparePBRTextures();

                     const auto& renderQueue = m_SceneRenderer->GetMeshRenderList();

                     const auto& pointLights = m_SceneRenderer->GetPointLights();

                     for ( const auto& renderData : renderQueue )
                     {
                         renderData.Material->Bind( { camera, renderData.Transform,
                                                      m_SceneRenderer->GetDirectionLights(), textures,
                                                      pointLights } );
                         renderer.RenderMesh( m_Pipeline, renderData.Mesh,
                                              renderData.Material->GetMaterialExecutor() );
                     }

                     if ( m_OutlineDraw )
                     {

                         for ( const auto& renderData : renderQueue )
                         {
                             if ( !renderData.Outlined )
                             {
                                 continue;
                             }

                             m_OutlineMaterial->Bind(
                                  { camera, renderData.Transform, m_OutlineWidth, m_OutlineColor } );

                             renderer.RenderMesh( m_OutlinePipeline, renderData.Mesh,
                                                  m_OutlineMaterial->GetMaterialExecutor() );
                         }
                     }
                 }
             },
             RenderPass::Create( rpSpec ) );

        return BOOLSUCCESS;
    }

    void MeshRenderer::Shutdown()
    {
        if ( m_Framebuffer )
        {
            m_Framebuffer->Release();
            m_Framebuffer.reset();
        }
    }

    bool MeshRenderer::SetupGeometryPass( const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal,
                                          const std::shared_ptr<RenderGraph>& renderGraph )
    {
        constexpr std::string_view debugName = "SceneGeometry";

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
        pipeSpec.Shader         = Runtime::ResourceRegistry::GetShaderService()->GetByName( "StaticPBR.glsl" );
        pipeSpec.Framebuffer    = skyboxFramebufferExternal;

        m_Pipeline = Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        return true;
    }

    bool MeshRenderer::SetupOutlinePass( const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal,
                                         const std::shared_ptr<RenderGraph>& renderGraph )
    {

        // Shader
        m_OutlineShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Outline.glsl" );

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
        outlinePipeSpec.Framebuffer    = skyboxFramebufferExternal;
        outlinePipeSpec.PolygonMode    = PrimitivePolygonMode::Wireframe;
        outlinePipeSpec.Topology       = PrimitiveTopology::LineStrip;
        outlinePipeSpec.LineWidth      = 5.0f;

        m_OutlinePipeline = Pipeline::Create( outlinePipeSpec );
        m_OutlinePipeline->Invalidate();

        m_OutlineMaterial = std::make_unique<MaterialOutline>();

        return true;
    }

    std::optional<Models::PBR::PBRTextures> MeshRenderer::PreparePBRTextures() const
    {
        const auto& environment = m_SceneRenderer->GetEnvironment();

        if ( !environment || !environment->IrradianceMap || !environment->PreFilteredMap )
            return std::nullopt;

        return Models::PBR::PBRTextures{ .IrradianceMap  = environment->IrradianceMap,
                                         .PreFilteredMap = environment->PreFilteredMap };
    }

} // namespace Desert::Graphic::System