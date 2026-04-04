#include "MeshRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{

    Common::BoolResultStr MeshRenderer::Initialize()
    {
        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return Common::MakeError( "Target framebuffer is not available" );

        if ( !SetupGeometryPass() )
            return Common::MakeError( "Failed to setup static geometry pass" );

        if ( !SetupSkinnedGeometryPass() )
            return Common::MakeError( "Failed to setup skinned geometry pass" );

        if ( !SetupOutlinePass() )
            return Common::MakeError( "Failed to setup outline pass" );

        return BOOLSUCCESS;
    }

    void MeshRenderer::Shutdown()
    {
        m_StaticPipeline.reset();
        m_SkinnedPipeline.reset();
        m_OutlinePipeline.reset();
        m_OutlineMaterial.reset();
    }

    void MeshRenderer::AddStaticMesh( const std::shared_ptr<Desert::Mesh>&      mesh,
                                      const std::shared_ptr<StaticMaterialPBR>& material,
                                      const glm::mat4&                          transform )
    {
        m_StaticQueue.emplace_back( mesh, transform, material, false );
    }

    void MeshRenderer::AddSkinnedMesh( const std::shared_ptr<Desert::SkinnedMesh>& mesh,
                                       const std::shared_ptr<SkinnedMaterialPBR>&  material,
                                       const glm::mat4& transform, const std::vector<glm::mat4>& boneMatrices )
    {
        m_SkinnedQueue.emplace_back( mesh, transform, material, boneMatrices );
    }

    void MeshRenderer::ClearQueues()
    {
        m_StaticQueue.clear();
        m_SkinnedQueue.clear();
    }

    void MeshRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return;

        builder.AddPass( "MeshGeometryPass", RenderPhase::Geometry,
                         [this]()
                         {
                             const auto camera = m_SceneRenderer->GetMainCamera();
                             if ( !camera )
                                 return;

                             DrawStaticMeshes();
                             DrawSkinnedMeshes();
                         },
                         m_StaticPipeline->GetSpecification(), targetFb,
                         { RenderPassDependency( RenderPhase::DepthPrePass ) } );

        if ( m_OutlineDraw )
        {
            //RegisterOutlinePass( builder );
        }
    }

    void MeshRenderer::DrawStaticMeshes()
    {
        if ( m_StaticQueue.empty() )
            return;

        auto&       renderer    = Renderer::GetInstance();
        const auto  camera      = m_SceneRenderer->GetMainCamera();
        const auto  textures    = PreparePBRTextures();
        const auto& pointLights = m_SceneRenderer->GetPointLights();

        for ( const auto& data : m_StaticQueue )
        {
            data.Material->Bind(
                 { camera, data.Transform, m_SceneRenderer->GetDirectionLights(), pointLights, textures } );

            renderer.RenderMesh( m_StaticPipeline, data.Mesh, data.Transform, data.Material->GetMaterialExecutor() );
        }
    }

    void MeshRenderer::DrawSkinnedMeshes()
    {
        if ( m_SkinnedQueue.empty() )
            return;

        auto&       renderer    = Renderer::GetInstance();
        const auto  camera      = m_SceneRenderer->GetMainCamera();
        const auto  textures    = PreparePBRTextures();
        const auto& pointLights = m_SceneRenderer->GetPointLights();

        for ( const auto& data : m_SkinnedQueue )
        {
            data.Material->Bind( { camera, data.Transform, m_SceneRenderer->GetDirectionLights(), pointLights,
                                   textures, data.BoneMatrices } );

            renderer.RenderMesh( m_SkinnedPipeline, data.Mesh, data.Transform, data.Material->GetMaterialExecutor() );
        }
    }

    bool MeshRenderer::SetupGeometryPass()
    {
        m_GeometryShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "StaticMeshPBR" );

        if ( !m_GeometryShader )
            return false;

        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return false;

        PipelineSpecification spec;
        spec.DebugName = "StaticMeshGeometry";

        spec.Layout = { { Graphic::ShaderDataType::Float3, "a_Position" },
                        { Graphic::ShaderDataType::Float3, "a_Normal" },
                        { Graphic::ShaderDataType::Float3, "a_Tangent" },
                        { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                        { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };

        spec.DepthCompareOp = CompareOp::LessOrEqual;
        spec.CullMode       = CullMode::Back;
        spec.Shader         = m_GeometryShader;
        spec.Framebuffer    = targetFb;

        m_StaticPipeline = Pipeline::Create( spec );
        m_StaticPipeline->Invalidate();

        return true;
    }

    bool MeshRenderer::SetupSkinnedGeometryPass()
    {
        m_SkinnedShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "SkinnedMeshPBR" );

        if ( !m_SkinnedShader )
            return false;

        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return false;

        PipelineSpecification spec;
        spec.DebugName = "SkinnedMeshGeometry";

        spec.Layout = { { Graphic::ShaderDataType::Float3, "a_Position" },
                        { Graphic::ShaderDataType::Float3, "a_Normal" },
                        { Graphic::ShaderDataType::Float3, "a_Tangent" },
                        { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                        { Graphic::ShaderDataType::Float2, "a_TextureCoord" },
                        { Graphic::ShaderDataType::Int4, "a_BoneIndices" },
                        { Graphic::ShaderDataType::Float4, "a_BoneWeights" } };

        spec.DepthCompareOp = CompareOp::LessOrEqual;
        spec.CullMode       = CullMode::Back;
        spec.Shader         = m_SkinnedShader;
        spec.Framebuffer    = targetFb;

        m_SkinnedPipeline = Pipeline::Create( spec );
        m_SkinnedPipeline->Invalidate();

        return true;
    }

    bool MeshRenderer::SetupOutlinePass()
    {
        m_OutlineShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Outline" );
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

    ShaderProtocols::PBRTexturesUB MeshRenderer::PreparePBRTextures() const
    {
        return {};
    }

    void MeshRenderer::RegisterOutlinePass( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return;

        builder.AddPass( "MeshOutlinePass", RenderPhase::Outline,
                         [this]()
                         {
                             auto&      renderer = Renderer::GetInstance();
                             const auto camera   = m_SceneRenderer->GetMainCamera();
                             if ( !camera )
                                 return;

                             // ===== Static =====
                             /* for ( const auto& renderData : m_StaticQueue )
                              {
                                  if ( !renderData.Outlined )
                                      continue;

                                  m_OutlineMaterial->Bind( { camera, renderData.Transform, m_OutlineWidth,
                              m_OutlineColor } );

                                  renderer.RenderMesh( m_OutlinePipeline, renderData.Mesh,
                                                       m_OutlineMaterial->GetMaterialExecutor() );
                              }*/

                             // ===== Skinned =====
                             /*for ( const auto& renderData : m_SkinnedQueue )
                             {
                                 m_OutlineMaterial->Bind( { camera, renderData.Transform, m_OutlineWidth,
                             m_OutlineColor, renderData.BoneMatrices } );

                                 renderer.RenderMesh( m_OutlinePipeline, renderData.Mesh,
                                                      m_OutlineMaterial->GetMaterialExecutor() );
                             }*/
                         },
                         m_OutlinePipeline->GetSpecification(), targetFb,
                         { RenderPassDependency( RenderPhase::Geometry ) } );
    }

} // namespace Desert::Graphic::System
