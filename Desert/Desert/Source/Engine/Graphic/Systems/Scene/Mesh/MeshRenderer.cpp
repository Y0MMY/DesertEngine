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

        m_StaticMaterialFallback =
             std::make_unique<Graphic::StaticMaterialPBR>();

        return BOOLSUCCESS;
    }

    void MeshRenderer::Shutdown()
    {
        m_StaticPipeline.reset();
        m_SkinnedPipeline.reset();
        m_OutlinePipeline.reset();
        m_OutlineMaterial.reset();
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

                             UpdateGlobalUniforms( camera, m_SceneRenderer->GetPointLights(),
                                                   m_SceneRenderer->GetDirectionLights() );

                             DrawStaticMeshes();
                             DrawSkinnedMeshes();
                         },
                         m_StaticPipeline->GetSpecification(), targetFb,
                         { RenderPassDependency( RenderPhase::DepthPrePass ) } );

        if ( m_OutlineDraw )
        {
            // RegisterOutlinePass( builder );
        }
    }

    void MeshRenderer::UpdateGlobalUniforms( const Core::Camera*                    camera,
                                             const ShaderProtocols::PointLight&     pointLights,
                                             const ShaderProtocols::DirectionLight& dirLights )
    {
        if ( !camera )
            return;
    }

    void MeshRenderer::DrawStaticMeshes()
    {
        if ( m_StaticQueue.empty() )
            return;

        auto&      renderer = Renderer::GetInstance();
        const auto camera   = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return;

        const auto& pointLights = m_SceneRenderer->GetPointLights();
        const auto& dirLights   = m_SceneRenderer->GetDirectionLights();

        for ( const auto& data : m_StaticQueue )
        {
            if ( !data.Mesh || data.MaterialSlots.empty() )
                continue;

            // We use the first material slot for now, as the current renderer doesn't support submeshes with different materials yet
            MaterialInstance* inst = data.MaterialSlots[0];
            if ( !inst )
                continue;

            Material* parentMaterial = inst->GetParentMaterial();

            // Update scene data on the specific instance using static PBR helpers
            StaticMaterialPBR::UpdateCamera( inst, camera );
            StaticMaterialPBR::UpdateLights( inst, pointLights, dirLights );

            // Update object data on the specific instance
            StaticMaterialPBR::UpdateTransform( inst, data.Transform );

            // Bind the material instance.
            parentMaterial->Bind( inst );

            renderer.RenderMesh( m_StaticPipeline.get(), data.Mesh, data.Transform,
                                 parentMaterial->GetMaterialExecutor() );
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
            // For now, we assume the first material instance is the PBR one
            MaterialInstance* inst = (MaterialInstance*)data.Material;
            
            data.Material->Bind( { .instance        = inst,
                                   .MainCamera      = camera,
                                   .MeshTransform   = data.Transform,
                                   .DirectionLights = m_SceneRenderer->GetDirectionLights(),
                                   .PointLights     = pointLights,
                                   .PBREnvTextures  = textures,
                                   .SkinnedUB       = { .BoneMatrices = data.BoneMatrices } } );

            renderer.RenderMesh( m_SkinnedPipeline.get(), data.Mesh, data.Transform,
                                 data.Material->GetMaterialExecutor() );
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

    void MeshRenderer::SubmitMesh( const MeshRenderData& data )
    {
        if ( !data.Mesh )
        {
            return;
        }

        switch ( data.Mesh->GetType() )
        {
            case MeshType::Static:
            {
                StaticMeshRenderData staticData;
                staticData.Mesh          = static_cast<StaticMesh*>( data.Mesh );
                staticData.Transform     = data.Transform;
                staticData.MaterialSlots = data.MaterialSlots;
                staticData.Outlined      = data.Outlined;

                m_StaticQueue.push_back( staticData );
                break;
            }

            case MeshType::Skinned:
            {
                SkinnedMeshRenderData skinnedData;
                skinnedData.Mesh         = static_cast<SkinnedMesh*>( data.Mesh );
                skinnedData.Transform    = data.Transform;
             //   skinnedData.Material     = static_cast<SkinnedMaterialPBR*>( data.MaterialSlots[0] );
                skinnedData.BoneMatrices = data.BoneMatrices;

                m_SkinnedQueue.push_back( skinnedData );
                break;
            }
        }
    }

} // namespace Desert::Graphic::System
