#include "MeshRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/PBRPush.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>

#include <variant>

namespace Desert::Graphic::System
{
    namespace
    {
        // Per-instance material override (MaterialPropertyBlock-style): start from the material's
        // reflected data and apply any overridden instance properties on top — generically, by name,
        // through reflection. Each drawn object thus gets its own effective material in the SSBO.
        PBRPushMaterial BuildEffectiveMaterial( StaticMaterialPBR* material, MaterialInstance* instance )
        {
            Assets::PBRMaterialData data = material->Data();

            const auto* type = Reflection::ReflectionRegistry::Get().Find( "PBRMaterialData" );
            if ( instance && type )
            {
                for ( const auto& [name, prop] : instance->GetPropertySet().GetProperties() )
                {
                    if ( !prop.bIsOverridden )
                        continue;

                    const Reflection::FieldInfo* field = nullptr;
                    for ( const auto& f : type->Fields )
                        if ( f.Name == name ) { field = &f; break; }
                    if ( !field )
                        continue;

                    void*       dst = static_cast<char*>( static_cast<void*>( &data ) ) + field->Offset;
                    const auto& v   = prop.Value;
                    using Reflection::FieldType;

                    switch ( field->Type )
                    {
                        case FieldType::Float:
                            if ( auto* f = std::get_if<float>( &v ) ) *static_cast<float*>( dst ) = *f;
                            break;
                        case FieldType::Int:
                            if ( auto* i = std::get_if<int>( &v ) ) *static_cast<int*>( dst ) = *i;
                            break;
                        case FieldType::Bool:
                            if ( auto* b = std::get_if<bool>( &v ) ) *static_cast<bool*>( dst ) = *b;
                            break;
                        case FieldType::Vec3:
                            if ( auto* v3 = std::get_if<glm::vec3>( &v ) ) *static_cast<glm::vec3*>( dst ) = *v3;
                            else if ( auto* v4 = std::get_if<glm::vec4>( &v ) )
                                *static_cast<glm::vec3*>( dst ) = glm::vec3( *v4 );
                            break;
                        case FieldType::Vec4:
                            if ( auto* v4 = std::get_if<glm::vec4>( &v ) ) *static_cast<glm::vec4*>( dst ) = *v4;
                            else if ( auto* v3 = std::get_if<glm::vec3>( &v ) )
                            {
                                auto* d = static_cast<glm::vec4*>( dst );
                                *d      = glm::vec4( *v3, d->w );
                            }
                            break;
                        default:
                            break; // AssetHandle/textures are per-material descriptors, not SSBO data.
                    }
                }
            }

            return BuildPBRPushMaterial( data );
        }
    } // namespace

    Common::BoolResultStr MeshRenderer::Initialize()
    {
        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return Common::MakeError( "Target framebuffer is not available" );

        if ( !SetupGeometryPass() )
            return Common::MakeError( "Failed to setup static geometry pass" );

        if ( !SetupSkinnedGeometryPass() )
            return Common::MakeError( "Failed to setup skinned geometry pass" );

        if ( !SetupSilhouettePass() )
            return Common::MakeError( "Failed to setup silhouette pass" );

        m_StaticMaterialFallback =
             std::make_unique<Graphic::StaticMaterialPBR>();

        return BOOLSUCCESS;
    }

    void MeshRenderer::Shutdown()
    {
        m_StaticPipeline.reset();
        m_SkinnedPipeline.reset();
        m_SilhouettePipeline.reset();
        m_SilhouetteMaterial.reset();
        m_SilhouetteMaskFramebuffer.reset();
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

        // The silhouette mask is always produced (and cleared) so the Jump Flood outline has a
        // fresh input every frame. Outline visibility is controlled by JumpFloodOutlineRenderer.
        RegisterSilhouettePass( builder );
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

        // Group draws by material so each material's per-object data fills ONE storage buffer, indexed
        // per draw (GPU-scene style). Objects of the same material that wrote a shared buffer per-draw
        // would otherwise collapse to the last writer.
        std::vector<std::pair<StaticMaterialPBR*, std::vector<const StaticMeshRenderData*>>> groups;
        const auto groupFor = [&]( StaticMaterialPBR* mat ) -> std::vector<const StaticMeshRenderData*>&
        {
            for ( auto& [m, v] : groups )
                if ( m == mat )
                    return v;
            groups.emplace_back( mat, std::vector<const StaticMeshRenderData*>{} );
            return groups.back().second;
        };

        for ( const auto& data : m_StaticQueue )
        {
            if ( !data.Mesh || data.MaterialSlots.empty() || !data.MaterialSlots[0] )
                continue;
            if ( auto* mat = static_cast<StaticMaterialPBR*>( data.MaterialSlots[0]->GetParentMaterial() ) )
                groupFor( mat ).push_back( &data );
        }

        for ( auto& [mat, objects] : groups )
        {
            // Fill this material's per-object storage buffer (one GpuMaterial per drawn object).
            std::vector<PBRPushMaterial> gpuMaterials;
            gpuMaterials.reserve( objects.size() );
            for ( const auto* obj : objects )
                gpuMaterials.push_back( BuildEffectiveMaterial( mat, obj->MaterialSlots[0] ) );

            if ( auto* sb = mat->Get<StorageBufferProperty>( "Materials" ) )
                sb->SetRawData( gpuMaterials.data(),
                                static_cast<uint32_t>( gpuMaterials.size() * sizeof( PBRPushMaterial ) ) );

            for ( uint32_t i = 0; i < static_cast<uint32_t>( objects.size() ); ++i )
            {
                const auto*       obj  = objects[i];
                MaterialInstance* inst = obj->MaterialSlots[0];

                StaticMaterialPBR::UpdateCamera( inst, camera );
                StaticMaterialPBR::UpdateLights( inst, pointLights, dirLights );
                StaticMaterialPBR::UpdateTransform( inst, obj->Transform );

                mat->SetMaterialIndex( i );
                mat->Bind( inst );

                renderer.RenderMesh( m_StaticPipeline.get(), obj->Mesh, obj->Transform,
                                     mat->GetMaterialExecutor() );
            }
        }
    }

    void MeshRenderer::DrawSkinnedMeshes()
    {
        if ( m_SkinnedQueue.empty() )
            return;

        auto&       renderer    = Renderer::GetInstance();
        const auto  camera      = m_SceneRenderer->GetMainCamera();
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

        GraphicsPipelineSpecification spec;
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

        m_StaticPipeline = GraphicsPipeline::Create( spec );
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

        GraphicsPipelineSpecification spec;
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

        m_SkinnedPipeline = GraphicsPipeline::Create( spec );
        m_SkinnedPipeline->Invalidate();

        return true;
    }

    bool MeshRenderer::SetupSilhouettePass()
    {
        m_SilhouetteShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Silhouette" );
        if ( !m_SilhouetteShader )
        {
            LOG_ERROR( "Failed to load silhouette shader" );
            return false;
        }

        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return false;

        // Dedicated single-channel-ish mask target. Selected meshes are drawn white; the rest stays
        // at the framebuffer clear color (~0.1), which JFA_Init separates with a 0.5 threshold.
        FramebufferSpecification maskSpec;
        maskSpec.DebugName = "SilhouetteMask";
        maskSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA8F );

        m_SilhouetteMaskFramebuffer = Graphic::Framebuffer::Create( maskSpec );
        m_SilhouetteMaskFramebuffer->Resize( targetFb->GetFramebufferWidth(), targetFb->GetFramebufferHeight() );

        GraphicsPipelineSpecification spec;
        spec.DebugName = "SilhouettePipeline";
        spec.Layout    = { { Graphic::ShaderDataType::Float3, "a_Position" },
                           { Graphic::ShaderDataType::Float3, "a_Normal" },
                           { Graphic::ShaderDataType::Float3, "a_Tangent" },
                           { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                           { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };

        spec.DepthTestEnabled   = false;
        spec.DepthWriteEnabled  = false;
        spec.StencilTestEnabled = false;
        spec.CullMode           = CullMode::None;
        spec.Shader             = m_SilhouetteShader;
        spec.Framebuffer        = m_SilhouetteMaskFramebuffer;

        m_SilhouettePipeline = GraphicsPipeline::Create( spec );
        m_SilhouettePipeline->Invalidate();

        m_SilhouetteMaterial = std::make_unique<MaterialSilhouette>();

        return true;
    }

    void MeshRenderer::RegisterSilhouettePass( RenderGraphBuilder& builder )
    {
        if ( !m_SilhouetteMaskFramebuffer )
            return;

        builder.AddPass( "MeshSilhouettePass", RenderPhase::Outline,
                         [this]()
                         {
                             const auto camera = m_SceneRenderer->GetMainCamera();
                             if ( !camera )
                                 return;

                             auto& renderer = Renderer::GetInstance();
                             m_SilhouetteMaterial->UpdateCamera( camera );

                             // ===== Static =====
                             for ( const auto& renderData : m_StaticQueue )
                             {
                                 if ( !renderData.Outlined || !renderData.Mesh )
                                     continue;

                                 renderer.RenderMesh( m_SilhouettePipeline.get(), renderData.Mesh,
                                                      renderData.Transform,
                                                      m_SilhouetteMaterial->GetMaterialExecutor() );
                             }

                             // NOTE: skinned mesh silhouettes require a skinned variant of the
                             // Silhouette shader (bone matrices). Deferred until the skinned mesh
                             // material path is re-enabled.
                         },
                         m_SilhouettePipeline->GetSpecification(), m_SilhouetteMaskFramebuffer,
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
