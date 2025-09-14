#include "MeshRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResult MeshRenderer::Initialize()
    {
        const auto& compositeFramebuffer = m_TargetFramebuffer.lock();
        if ( !compositeFramebuffer )
        {
            return Common::MakeError( "Target framebuffer is not available" );
        }

        // Setup geometry pass
        if ( !SetupGeometryPass() )
            return Common::MakeError( "Failed to setup geometry pass" );

        // Setup outline pass
        if ( !SetupOutlinePass() )
            return Common::MakeError( "Failed to setup outline pass" );

        return BOOLSUCCESS;
    }

    void MeshRenderer::Shutdown()
    {
        m_GeometryPipeline.reset();
        m_OutlinePipeline.reset();
        m_OutlineMaterial.reset();
    }

    void MeshRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return;

        // Geometry pass
        builder.AddPass( "MeshGeometryPass", RenderPhase::Geometry,
                         [this]()
                         {
                             const auto camera      = m_SceneRenderer->GetMainCamera().lock();
                             if ( !camera  )
                                 return;


                             auto&      renderer    = Renderer::GetInstance();
                             const auto  textures    = PreparePBRTextures();
                             const auto& renderQueue = m_SceneRenderer->GetMeshRenderList();
                             const auto& pointLights = m_SceneRenderer->GetPointLights();

                             for ( const auto& renderData : renderQueue )
                             {
                                 renderData.Material->Bind( { camera, renderData.Transform,
                                                              m_SceneRenderer->GetDirectionLights(), textures,
                                                              pointLights } );
                                 renderer.RenderMesh(m_GeometryPipeline, renderData.Mesh,
                                                      renderData.Material->GetMaterialExecutor() );
                             }
                         },
                         m_GeometryPipeline->GetSpecification(), targetFb,
                         { RenderPassDependency( RenderPhase::DepthPrePass ) } );

        //// Outline pass (optional)
        //if ( m_OutlineDraw )
        //{
        //    builder.AddPass( "MeshOutlinePass", RenderPhase::Outline,
        //                     [this]()
        //                     {
        //                         auto&      renderer = Renderer::GetInstance();
        //                         const auto camera   = m_ActiveCamera.lock();

        //                         if ( !camera )
        //                             return;

        //                         const auto& renderQueue = m_SceneRenderer->GetMeshRenderList();

        //                         for ( const auto& renderData : renderQueue )
        //                         {
        //                             if ( !renderData.Outlined )
        //                                 continue;

        //                             m_OutlineMaterial->Bind(
        //                                  { camera, renderData.Transform, m_OutlineWidth, m_OutlineColor } );

        //                             renderer.RenderMesh( m_OutlinePipeline, renderData.Mesh,
        //                                                  m_OutlineMaterial->GetMaterialExecutor() );
        //                         }
        //                     },
        //                     m_OutlinePipeline->GetSpecification(), targetFb,
        //                     { RenderPassDependency( RenderPhase::Geometry ) } );
        //}
    }

    bool MeshRenderer::SetupGeometryPass()
    {
        constexpr std::string_view debugName = "MeshGeometry";

        m_GeometryShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "StaticPBR.glsl" );
        if ( !m_GeometryShader )
        {
            LOG_ERROR( "Failed to load geometry shader" );
            return false;
        }

        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return false;

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
        pipeSpec.CullMode       = CullMode::Back;
        pipeSpec.Shader         = m_GeometryShader;
        pipeSpec.Framebuffer    = targetFb;

        m_GeometryPipeline = Pipeline::Create( pipeSpec );
        m_GeometryPipeline->Invalidate();

        return true;
    }

    bool MeshRenderer::SetupOutlinePass()
    {
        m_OutlineShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Outline.glsl" );
        if ( !m_OutlineShader )
        {
            LOG_ERROR( "Failed to load outline shader" );
            return false;
        }

        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return false;

        PipelineSpecification outlinePipeSpec;
        outlinePipeSpec.DebugName = "OutlinePipeline";
        outlinePipeSpec.Layout    = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                      { Graphic::ShaderDataType::Float3, "a_Normal" },
                                      { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                      { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                      { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };

        outlinePipeSpec.DepthWriteEnabled  = false;
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
        outlinePipeSpec.CullMode           = CullMode::None;
        outlinePipeSpec.Shader             = m_OutlineShader;
        outlinePipeSpec.Framebuffer        = targetFb;
        outlinePipeSpec.PolygonMode        = PrimitivePolygonMode::Wireframe;
        outlinePipeSpec.LineWidth          = 5.0F; 

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