#include "MeshRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/PBRPush.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>

#include <variant>
#include <cmath>

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

        if ( !SetupShadowPass() )
            return Common::MakeError( "Failed to setup shadow pass" );

        if ( !SetupDebugLinePass() )
            return Common::MakeError( "Failed to setup debug line pass" );

        m_StaticMaterialFallback =
             std::make_unique<Graphic::StaticMaterialPBR>();

        return BOOLSUCCESS;
    }

    void MeshRenderer::Shutdown()
    {
        m_StaticPipeline.reset();
        m_StaticWireframePipeline.reset();
        m_SkinnedPipeline.reset();
        m_SilhouettePipeline.reset();
        m_SilhouetteMaterial.reset();
        m_SilhouetteMaskFramebuffer.reset();
        m_ShadowPipeline.reset();
        for ( uint32_t i = 0; i < kNumCascades; ++i )
        {
            m_ShadowMaterial[i].reset();
            m_CascadeFB[i].reset();
        }
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
        RegisterShadowPass( builder );
        RegisterDebugPass( builder );
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
        const auto& spotLights  = m_SceneRenderer->GetSpotLights();
        const auto& dirLights   = m_SceneRenderer->GetDirectionLights();

        // Resolve the active IBL environment cubemaps once (diffuse irradiance + prefiltered specular)
        // so each PBR object can sample real ambient/reflections instead of the fallback dummy cube.
        ImageCube*  iblIrradiance  = nullptr;
        ImageCube*  iblPrefiltered = nullptr;
        Image2D*    iblBrdfLut     = nullptr;
        {
            auto* imageService = Runtime::ResourceRegistry::GetImageService();
            if ( const auto& env = m_SceneRenderer->GetEnvironment(); env.has_value() )
            {
                if ( env->IrradianceMap.IsValid() )
                    iblIrradiance = static_cast<ImageCube*>( imageService->Resolve( env->IrradianceMap ) );
                if ( env->PreFilteredMap.IsValid() )
                    iblPrefiltered = static_cast<ImageCube*>( imageService->Resolve( env->PreFilteredMap ) );
            }
            // Split-sum BRDF LUT (precomputed .tga loaded by the Renderer) — needed for correct IBL specular.
            if ( const auto& brdf = Renderer::GetInstance().GetBRDFTexture();
                 brdf && brdf->GetImageHandle().IsValid() )
                iblBrdfLut = static_cast<Image2D*>( imageService->Resolve( brdf->GetImageHandle() ) );
        }

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
                StaticMaterialPBR::UpdateLights( inst, pointLights, spotLights, dirLights );
                StaticMaterialPBR::UpdateTransform( inst, obj->Transform );
                Image2D* cascadeMaps[kNumCascades];
                for ( uint32_t c = 0; c < kNumCascades; ++c )
                    cascadeMaps[c] =
                         m_CascadeFB[c] ? m_CascadeFB[c]->GetColorAttachmentImage().get() : nullptr;
                StaticMaterialPBR::UpdateShadow( inst, m_CascadeVP, cascadeMaps, kNumCascades, m_ShadowBias,
                                                 m_ShadowsEnabled, m_ShadowDebugMode, m_ShowNormals,
                                                 m_CascadeWorldPerTexel, m_LightingDebug );
                StaticMaterialPBR::UpdateEnvironment( inst, iblIrradiance, iblPrefiltered, iblBrdfLut );

                mat->SetMaterialIndex( i );
                mat->Bind( inst );

                auto* pipeline = ( m_Wireframe && m_StaticWireframePipeline ) ? m_StaticWireframePipeline.get()
                                                                              : m_StaticPipeline.get();
                renderer.RenderMesh( pipeline, obj->Mesh, obj->Transform, mat->GetMaterialExecutor() );
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
        const auto& spotLights  = m_SceneRenderer->GetSpotLights();

        for ( const auto& data : m_SkinnedQueue )
        {
            // For now, we assume the first material instance is the PBR one
            MaterialInstance* inst = (MaterialInstance*)data.Material;

            data.Material->Bind( { .instance        = inst,
                                   .MainCamera      = camera,
                                   .MeshTransform   = data.Transform,
                                   .DirectionLights = m_SceneRenderer->GetDirectionLights(),
                                   .PointLights     = pointLights,
                                   .SpotLights      = spotLights,
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

        // Wireframe variant — identical spec, line polygon mode (device feature fillModeNonSolid is on).
        // Selected per-frame by the SceneSettings debug toggle; shares the same framebuffer/render pass.
        spec.DebugName   = "StaticMeshWireframe";
        spec.PolygonMode = PrimitivePolygonMode::Wireframe;
        m_StaticWireframePipeline = GraphicsPipeline::Create( spec );
        m_StaticWireframePipeline->Invalidate();

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

    bool MeshRenderer::SetupShadowPass()
    {
        m_ShadowShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Shadow" );
        if ( !m_ShadowShader )
        {
            LOG_ERROR( "Failed to load shadow shader" );
            return false;
        }

        // One R32F (in RGBA32F) light-space depth map + depth attachment PER CASCADE. Each cascade also
        // gets its own MaterialShadow so the 4 shadow passes don't alias a single shared light-matrix UBO
        // (all draws recorded into one command buffer would otherwise see the last cascade's matrix).
        for ( uint32_t i = 0; i < kNumCascades; ++i )
        {
            FramebufferSpecification shadowSpec;
            shadowSpec.DebugName = "ShadowCascade" + std::to_string( i );
            shadowSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );
            shadowSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::DEPTH24STENCIL8 );
            m_CascadeFB[i] = Graphic::Framebuffer::Create( shadowSpec );
            m_CascadeFB[i]->Resize( kShadowMapSize, kShadowMapSize );
            m_ShadowMaterial[i] = std::make_unique<MaterialShadow>();
        }

        GraphicsPipelineSpecification spec;
        spec.DebugName = "ShadowPipeline";
        spec.Layout    = { { Graphic::ShaderDataType::Float3, "a_Position" },
                           { Graphic::ShaderDataType::Float3, "a_Normal" },
                           { Graphic::ShaderDataType::Float3, "a_Tangent" },
                           { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                           { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };
        spec.DepthTestEnabled  = true;
        spec.DepthWriteEnabled = true;
        spec.DepthCompareOp    = CompareOp::LessOrEqual;
        // No culling in the shadow pass: store ALL faces so the map can never come out empty (front-face
        // culling under the engine's negative-height viewport could cull the wrong set and black out the
        // scene). Self-shadow acne is handled by the normal-offset + slope bias in the PBR sampling.
        spec.CullMode          = CullMode::None;
        spec.Shader            = m_ShadowShader;
        // All cascade framebuffers share the same attachment formats, so one pipeline is render-pass
        // compatible with all of them.
        spec.Framebuffer       = m_CascadeFB[0];

        m_ShadowPipeline = GraphicsPipeline::Create( spec );
        m_ShadowPipeline->Invalidate();

        return true;
    }

    void MeshRenderer::UpdateCascades()
    {
        const auto camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return;

        const auto& dirLights = m_SceneRenderer->GetDirectionLights();
        if ( dirLights.DirectionLights.empty() )
            return;
        glm::vec3 lightDir = glm::vec3( dirLights.DirectionLights[0].Direction );
        if ( glm::length( lightDir ) < 1e-4f )
            return;
        lightDir = glm::normalize( lightDir );

        // Two ranges: the CAMERA's real near/far parametrize the unprojected frustum corners (they must
        // match invVP below), while the shadow coverage is capped so far cascades stay usefully sized.
        constexpr float kShadowMaxDistance = 150.0f;
        const float     camNear   = camera->GetNear();
        const float     camFar    = camera->GetFar();
        const float     shadowFar = glm::min( camFar, kShadowMaxDistance );
        const float     splitRange = shadowFar - camNear;
        const float     ratio      = shadowFar / glm::max( camNear, 1e-4f );

        // Practical split scheme: blend uniform and logarithmic distributions (lambda).
        float splitFar[kNumCascades];
        for ( uint32_t i = 0; i < kNumCascades; ++i )
        {
            const float p   = static_cast<float>( i + 1 ) / static_cast<float>( kNumCascades );
            const float log = camNear * std::pow( ratio, p );
            const float uni = camNear + splitRange * p;
            splitFar[i]     = glm::mix( uni, log, m_SplitLambda );
        }

        // Full camera-frustum world corners (GL NDC z in [-1,1]). Each near corner shares a ray from the
        // eye with its matching far corner, so a cascade slice = lerp(near,far) by the view-depth fraction.
        const glm::mat4 invVP = glm::inverse( camera->GetProjectionMatrix() * camera->GetViewMatrix() );
        glm::vec3       nearCorners[4];
        glm::vec3       farCorners[4];
        int             ci = 0;
        for ( int x = 0; x < 2; ++x )
            for ( int y = 0; y < 2; ++y )
            {
                const glm::vec4 nc =
                     invVP * glm::vec4( 2.0f * x - 1.0f, 2.0f * y - 1.0f, -1.0f, 1.0f ); // near plane
                const glm::vec4 fc =
                     invVP * glm::vec4( 2.0f * x - 1.0f, 2.0f * y - 1.0f, 1.0f, 1.0f ); // far plane
                nearCorners[ci] = glm::vec3( nc ) / nc.w;
                farCorners[ci]  = glm::vec3( fc ) / fc.w;
                ++ci;
            }

        // Corner lerp fractions are relative to the CAMERA's full range (the range invVP encodes), NOT
        // the capped shadow range — otherwise the slice corners scale to the wrong (1000u) frustum.
        const float frustumRange = camFar - camNear;
        float       lastFar      = camNear;
        for ( uint32_t c = 0; c < kNumCascades; ++c )
        {
            const float tNear = ( lastFar - camNear ) / frustumRange;
            const float tFar  = ( splitFar[c] - camNear ) / frustumRange;

            glm::vec3 corners[8];
            glm::vec3 center( 0.0f );
            for ( int i = 0; i < 4; ++i )
            {
                const glm::vec3 edge = farCorners[i] - nearCorners[i];
                corners[i]           = nearCorners[i] + edge * tNear;
                corners[i + 4]       = nearCorners[i] + edge * tFar;
                center += corners[i] + corners[i + 4];
            }
            center /= 8.0f;

            // Bounding-sphere fit → cascade extent is rotation-invariant (reduces shimmer).
            float radius = 0.0f;
            for ( const auto& corner : corners )
                radius = glm::max( radius, glm::length( corner - center ) );
            radius = std::ceil( radius * 16.0f ) / 16.0f; // quantize a bit for stability

            const glm::vec3 up =
                 glm::abs( lightDir.y ) > 0.99f ? glm::vec3( 0, 0, 1 ) : glm::vec3( 0, 1, 0 );
            // Push the light eye back by 2*radius so casters between the light and the slice still cast.
            const glm::mat4 view = glm::lookAt( center - lightDir * ( radius * 2.0f ), center, up );
            glm::mat4       proj = glm::orthoRH_ZO( -radius, radius, -radius, radius, 0.1f, radius * 4.0f );

            // Texel-snap stabilization: round the cascade's origin to whole shadow-map texels in light
            // space so the sampling grid doesn't crawl/shimmer as the camera moves. (Microsoft CSM trick.)
            glm::mat4       vp          = proj * view;
            const float     halfRes     = static_cast<float>( kShadowMapSize ) * 0.5f;
            glm::vec4       originShadow = vp * glm::vec4( 0.0f, 0.0f, 0.0f, 1.0f );
            originShadow *= halfRes;
            glm::vec2       rounded( std::round( originShadow.x ), std::round( originShadow.y ) );
            glm::vec2       offset = ( rounded - glm::vec2( originShadow ) ) / halfRes;
            proj[3][0] += offset.x;
            proj[3][1] += offset.y;
            m_CascadeVP[c] = proj * view;

            // World size of one texel for this cascade (drives the PBR normal-offset / bias).
            m_CascadeWorldPerTexel[c] = ( 2.0f * radius ) / static_cast<float>( kShadowMapSize );

            lastFar = splitFar[c];
        }
    }

    void MeshRenderer::RegisterShadowPass( RenderGraphBuilder& builder )
    {
        // One depth-only pass per cascade, all in DepthPrePass (before Geometry, which depends on it).
        // Cascade matrices are computed in UpdateCascades() before the graph records (intra-phase order is
        // nondeterministic, so per-pass matrix computation can't be relied on for ordering).
        for ( uint32_t c = 0; c < kNumCascades; ++c )
        {
            if ( !m_CascadeFB[c] )
                continue;

            builder.AddPass(
                 "MeshShadowCascade" + std::to_string( c ), RenderPhase::DepthPrePass,
                 [this, c]()
                 {
                     if ( !m_ShadowsEnabled )
                         return;

                     // Shadow vert computes Projection*View*Transform; feed the combined cascade matrix as
                     // Projection and identity as View, matching u_LightViewProj[c] on the PBR side.
                     m_ShadowMaterial[c]->SetLightMatrix( glm::mat4( 1.0f ), m_CascadeVP[c] );

                     auto& renderer = Renderer::GetInstance();
                     for ( const auto& renderData : m_StaticQueue )
                     {
                         if ( !renderData.Mesh )
                             continue;
                         renderer.RenderMesh( m_ShadowPipeline.get(), renderData.Mesh, renderData.Transform,
                                              m_ShadowMaterial[c]->GetMaterialExecutor() );
                     }
                 },
                 m_ShadowPipeline->GetSpecification(), m_CascadeFB[c], {},
                 // Clear the R32F depth target to 1.0 (far): background texels must read as "no occluder",
                 // else the default 0.1 grey clear falsely shadows receivers whose light-space depth > 0.1.
                 glm::vec4( 1.0f ) );
        }
    }

    bool MeshRenderer::SetupDebugLinePass()
    {
        m_DebugLineShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "DebugLine" );
        if ( !m_DebugLineShader )
        {
            LOG_ERROR( "Failed to load DebugLine shader" );
            return false;
        }

        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return false;

        GraphicsPipelineSpecification spec;
        spec.DebugName         = "DebugLinePipeline";
        spec.Shader            = m_DebugLineShader;
        spec.Framebuffer       = targetFb;
        spec.Topology          = PrimitiveTopology::Lines;
        spec.LineWidth         = 1.0f; // dynamic line width is set to 1.0 in SubmitLines (no wideLines feature)
        spec.DepthTestEnabled  = true;
        spec.DepthWriteEnabled = false;
        spec.DepthCompareOp    = CompareOp::LessOrEqual;
        spec.CullMode          = CullMode::None;
        // No vertex Layout: the DebugLine shader pulls endpoints from the Lines storage buffer by index.

        m_DebugLinePipeline = GraphicsPipeline::Create( spec );
        m_DebugLinePipeline->Invalidate();

        m_DebugLineMaterial = std::make_unique<MaterialDebugLine>();
        return m_DebugLinePipeline != nullptr;
    }

    void MeshRenderer::RegisterDebugPass( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb || !m_DebugLinePipeline )
            return;

        // Overlay debug lines (AABB wireframes) over the lit scene; runs after Geometry, depth-tested.
        builder.AddPass(
             "DebugLinesPass", RenderPhase::Debug,
             [this]()
             {
                 if ( !m_ShowBoundingBoxes )
                     return;
                 const auto camera = m_SceneRenderer->GetMainCamera();
                 if ( !camera )
                     return;

                 // 12 box edges as index pairs into the 8 AABB corners (index bits = x|y<<1|z<<2).
                 static const int kEdges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
                                                    { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
                                                    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
                 const glm::vec4 color( m_BoundingBoxColor, 1.0f );

                 std::vector<MaterialDebugLine::LineVertex> lines;
                 for ( const auto& rd : m_StaticQueue )
                 {
                     if ( !rd.Mesh )
                         continue;
                     for ( const auto& sm : rd.Mesh->GetSubmeshes() )
                     {
                         const glm::vec3 mn = sm.BoundingBox.Min;
                         const glm::vec3 mx = sm.BoundingBox.Max;
                         glm::vec3       c[8] = { { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z },
                                                  { mn.x, mx.y, mn.z }, { mx.x, mx.y, mn.z },
                                                  { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z },
                                                  { mn.x, mx.y, mx.z }, { mx.x, mx.y, mx.z } };
                         const glm::mat4 world = rd.Transform * sm.Transform;
                         for ( auto& corner : c )
                             corner = glm::vec3( world * glm::vec4( corner, 1.0f ) );
                         for ( const auto& e : kEdges )
                         {
                             lines.push_back( { glm::vec4( c[e[0]], 1.0f ), color } );
                             lines.push_back( { glm::vec4( c[e[1]], 1.0f ), color } );
                         }
                     }
                 }
                 if ( lines.empty() )
                     return;

                 m_DebugLineMaterial->Update( camera, lines );
                 Renderer::GetInstance().SubmitLines( m_DebugLinePipeline.get(),
                                                      static_cast<uint32_t>( lines.size() ),
                                                      m_BoundingBoxLineWidth,
                                                      m_DebugLineMaterial->GetMaterialExecutor() );
             },
             m_DebugLinePipeline->GetSpecification(), targetFb,
             { RenderPassDependency( RenderPhase::Geometry ) } );
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
