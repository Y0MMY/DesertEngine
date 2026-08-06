#include "MeshRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/ShadowCascades.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/PBRPush.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/MaterialGlass.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Geometry/LODSelection.hpp>
#include <Common/Core/Profiler.hpp>
#include <Common/Core/Units.hpp>

#include <variant>
#include <chrono>
#include <cmath>
#include <algorithm>

namespace Desert::Graphic::System
{
    namespace
    {
        // Per-instance material override (MaterialPropertyBlock-style): start from the material's
        // reflected data and apply any overridden instance properties on top — generically, by name,
        // through reflection. Each drawn object thus gets its own effective material in the SSBO.
        PBRGpuMaterial BuildEffectiveMaterial( StaticMaterialPBR* material, MaterialInstance* instance )
        {
            Assets::PBRSurfaceParams data = material->Data();

            if ( instance )
            {
                // Apply instance overrides by schema name onto the typed hot-path view. The names
                // are the StaticMeshPBR schema (single material protocol) — same ones the tint
                // path (MeshECSSystem) and the material canon use.
                const auto vec4Of = []( const auto& v, const glm::vec4& current ) -> glm::vec4
                {
                    if ( auto* v4 = std::get_if<glm::vec4>( &v ) )
                        return *v4;
                    if ( auto* v3 = std::get_if<glm::vec3>( &v ) )
                        return glm::vec4( *v3, current.w );
                    return current;
                };
                const auto floatOf = []( const auto& v, float current ) -> float
                {
                    if ( auto* f = std::get_if<float>( &v ) )
                        return *f;
                    // A bare-instance override (no pre-existing typed property) is stored as a vec4; a scalar
                    // param authored that way (e.g. RoughnessFactor from MaterialComponent) rides in .x.
                    if ( auto* v4 = std::get_if<glm::vec4>( &v ) )
                        return v4->x;
                    return current;
                };

                for ( const auto& [name, prop] : instance->GetPropertySet().GetProperties() )
                {
                    if ( !prop.bIsOverridden )
                        continue;
                    const auto& v = prop.Value;

                    if ( name == "AlbedoColor" )
                        data.AlbedoColor = vec4Of( v, data.AlbedoColor );
                    else if ( name == "MetallicFactor" )
                        data.MetallicFactor = floatOf( v, data.MetallicFactor );
                    else if ( name == "RoughnessFactor" )
                        data.RoughnessFactor = floatOf( v, data.RoughnessFactor );
                    else if ( name == "AOStrength" )
                        data.AOStrength = floatOf( v, data.AOStrength );
                    else if ( name == "EmissiveColor" )
                        data.EmissiveColor = vec4Of( v, data.EmissiveColor );
                    else if ( name == "EmissiveIntensity" )
                        data.EmissiveIntensity = floatOf( v, data.EmissiveIntensity );
                    else if ( name == "AlphaCutoff" )
                        data.AlphaCutoff = floatOf( v, data.AlphaCutoff );
                    else if ( name == "Transmission" )
                        data.Transmission = floatOf( v, data.Transmission );
                    else if ( name == "IOR" )
                        data.IOR = floatOf( v, data.IOR );
                    else if ( name == "GlassTint" )
                        data.GlassTint = vec4Of( v, data.GlassTint );
                    else if ( name == "UVTiling" )
                    {
                        const glm::vec4 t = vec4Of( v, glm::vec4( data.UVTiling.value_or( glm::vec2( 1.0f ) ), 0, 0 ) );
                        data.UVTiling     = glm::vec2( t );
                    }
                    // Textures are per-material descriptors, not SSBO data — not overridable here.
                }
            }

            return BuildPBRGpuMaterial( data );
        }

        // First slot instance whose parent is the batched PBR material. Slots holding a
        // custom-shader material (DataDrivenMaterial, v3 per-slot shaders) belong to the
        // generic path — they must never be fed into the PBR SSBO machinery. nullptr when
        // the object has no PBR slot at all.
        MaterialInstance* FirstPBRSlot( const std::vector<MaterialInstance*>& slots )
        {
            for ( auto* inst : slots )
                if ( inst && dynamic_cast<StaticMaterialPBR*>( inst->GetParentMaterial() ) )
                    return inst;
            return nullptr;
        }
    } // namespace

    Common::BoolResultStr MeshRenderer::Initialize()
    {
        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return Common::MakeError( "Target framebuffer is not available" );

        if ( !SetupGeometryPass() )
            return Common::MakeError( "Failed to setup static geometry pass" );

        // Deferred G-buffer pipeline (optional; forward path is unaffected if it fails to set up). Creating it
        // here compiles the deferred shader + validates the MRT pipeline at startup.
        if ( !SetupGBufferPass() )
            LOG_WARN( "[MeshRenderer] Deferred G-buffer pipeline not set up (deferred path unavailable)." );
        if ( !SetupGlassPass() )
            LOG_WARN( "[MeshRenderer] Glass pipeline not set up (transparent materials won't draw)." );

        if ( !SetupSkinnedGeometryPass() )
            return Common::MakeError( "Failed to setup skinned geometry pass" );

        if ( !SetupSilhouettePass() )
            return Common::MakeError( "Failed to setup silhouette pass" );

        if ( !SetupShadowPass() )
            return Common::MakeError( "Failed to setup shadow pass" );

        if ( !SetupDebugLinePass() )
            return Common::MakeError( "Failed to setup debug line pass" );

        // Overdraw is an optional debug view — never fatal if its shaders are missing.
        if ( !SetupOverdrawPass() )
            LOG_WARN( "[MeshRenderer] Overdraw debug view unavailable (shaders missing)." );

        m_StaticMaterialFallback =
             std::make_unique<Graphic::StaticMaterialPBR>();

        // Shared instanced material for auto-batching (only usable if the instanced pipeline/shader exist).
        // One instance is created up front; the per-frame scene data + the packed InstanceTransforms/Materials
        // SSBOs are written into it in DrawStaticMeshes before the instanced draws are recorded.
        if ( m_StaticInstancedPipeline )
        {
            m_StaticInstancedMaterial = std::make_unique<Graphic::StaticMaterialPBRInstanced>();
            m_StaticInstancedInstance = m_StaticInstancedMaterial->CreateInstance( "StaticInstancedBatch" );
        }

        return BOOLSUCCESS;
    }

    void MeshRenderer::Shutdown()
    {
        m_StaticInstancedInstance.reset();
        m_StaticInstancedMaterial.reset();
        m_StaticPipeline.reset();
        m_StaticWireframePipeline.reset();
        m_SkinnedPipeline.reset();
        m_SilhouettePipeline.reset();
        m_SilhouetteMaterial.reset();
        m_SilhouetteMaskFramebuffer.reset();
        m_ShadowPipeline.reset();
        m_ShadowInstancedPipeline.reset();
        for ( uint32_t i = 0; i < kNumCascades; ++i )
        {
            m_ShadowMaterial[i].reset();
            m_ShadowInstancedMaterial[i].reset();
            m_CascadeFB[i].reset();
        }
    }

    void MeshRenderer::ClearQueues()
    {
        m_StaticQueue.clear();
        m_SkinnedQueue.clear();
        m_GenericQueue.clear();
        m_InstancedQueue.clear();
    }

    uint32_t MeshRenderer::ComputeLOD( const glm::mat4& transform, const Mesh* mesh, int forcedLOD,
                                       int lodBias ) const
    {
        if ( forcedLOD >= 0 )
            return static_cast<uint32_t>( forcedLOD );
        if ( !m_LODEnabled || !mesh )
            return 0;
        const auto* camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return 0;

        // The policy itself lives in Geometry::SelectLOD so the editor can report the SAME level it
        // draws with (Details "Mesh" section); this only resolves the renderer's camera + LOD toggle.
        return Geometry::SelectLOD( transform, mesh->GetSubmeshes(), camera->GetPosition(), forcedLOD, lodBias );
    }

    void MeshRenderer::SubmitGenericMesh( const GenericMeshRenderData& data )
    {
        // Two valid shapes: an override draw (ShaderName set) or a per-slot draw (SlotMaterial
        // set — the shader name comes from the material at draw time).
        if ( data.Mesh && ( !data.ShaderName.empty() || data.SlotMaterial ) )
            m_GenericQueue.push_back( data );
    }

    void MeshRenderer::SubmitInstancedMesh( const InstancedMeshRenderData& data )
    {
        if ( data.Mesh && data.Material && data.Transforms && !data.Transforms->empty() )
            m_InstancedQueue.push_back( data );
    }

    void MeshRenderer::DrawGenericMeshes( bool useLoadPass )
    {
        if ( m_GenericQueue.empty() )
            return;

        const auto  targetFb = m_TargetFramebuffer.lock();
        const auto* camera   = m_SceneRenderer->GetMainCamera();
        if ( !targetFb || !camera )
            return;

        // Engine-filled CameraUB (matches Common/CameraUB.glslh: mat4 Projection, View; vec3 CameraPos).
        struct CameraUBData
        {
            glm::mat4 Projection;
            glm::mat4 View;
            glm::vec4 CameraPos;
        };
        CameraUBData cam{};
        cam.Projection = camera->GetProjectionMatrix();
        cam.View       = camera->GetViewMatrix();
        cam.CameraPos  = glm::vec4( camera->GetPosition(), 1.0f );

        const VertexBufferLayout meshLayout = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                                { Graphic::ShaderDataType::Float3, "a_Normal" },
                                                { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                                { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                                { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };

        for ( const auto& g : m_GenericQueue )
        {
            // Per-slot draws carry their own material (asset params already applied at build);
            // Shader Override draws use a shader-keyed shared material + per-frame overrides.
            DataDrivenMaterial* material   = nullptr;
            std::string         shaderName = g.ShaderName;
            if ( g.SlotMaterial )
            {
                material   = dynamic_cast<DataDrivenMaterial*>( g.SlotMaterial );
                if ( material )
                    shaderName = material->GetShaderName();
            }

            auto shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( shaderName );
            if ( !shader || !g.Mesh || ( g.SlotMaterial && !material ) )
                continue;

            if ( !material )
            {
                auto& shared = m_GenericMaterials[shaderName];
                if ( !shared )
                    shared = std::make_unique<DataDrivenMaterial>( shaderName );
                material = shared.get();
            }

            GraphicsPipelineSpecification spec;
            spec.DebugName   = "GenericMesh_" + shaderName;
            spec.Shader      = shader;
            spec.Framebuffer       = targetFb;
            spec.Layout            = meshLayout;
            spec.UseLoadRenderPass = useLoadPass; // deferred manual pass begins with LOAD
            ApplyShaderRenderState( spec, shader->GetProgramMeta().State );
            auto pipeline = m_SceneRenderer->GetPipelineCache().GetOrCreate( spec );
            if ( !pipeline )
                continue;

            if ( auto* camUB = material->Get<UniformBufferProperty>( "CameraUB" ) )
            {
                // CameraUB ends in a vec3 (CameraPos) -> reflected size (140) < sizeof(cam) (144 padded).
                // Clamp to the real buffer size; the first 140 bytes (Proj/View/CameraPos.xyz) are valid.
                const size_t sz =
                     std::min( sizeof( cam ), static_cast<size_t>( camUB->GetUniform()->GetSize() ) );
                camUB->SetRawData( reinterpret_cast<const std::byte*>( &cam ), sz );
            }

            // Engine-filled TimeUB (opt-in: any shader declaring `uniform TimeUB { vec4 TimeData; }`
            // gets it — the shader-graph Time node relies on this). x = seconds since engine start.
            if ( auto* timeUB = material->Get<UniformBufferProperty>( "TimeUB" ) )
            {
                static const auto s_TimeOrigin = std::chrono::steady_clock::now();
                const float       seconds      = std::chrono::duration<float>(
                                             std::chrono::steady_clock::now() - s_TimeOrigin )
                                             .count();
                const glm::vec4 timeData( seconds, 0.0f, 0.0f, 0.0f );
                const size_t    sz = std::min( sizeof( timeData ),
                                               static_cast<size_t>( timeUB->GetUniform()->GetSize() ) );
                timeUB->SetRawData( reinterpret_cast<const std::byte*>( &timeData ), sz );
            }

            // Engine-filled DirectionLightsUB (opt-in, same PBR payload layout): generic shaders that
            // want lighting (the shader graph's Lit mode) declare the UB and receive the scene's
            // directional light — previously only PBR materials got light data.
            if ( auto* lightsUB =
                      material->Get<UniformBufferProperty>( ShaderProtocols::DirectionLight::Name ) )
            {
                const auto& dirLights = m_SceneRenderer->GetDirectionLights().DirectionLights;
                if ( !dirLights.empty() )
                {
                    const size_t sz = std::min(
                         dirLights.size() * sizeof( ShaderProtocols::DirectionLightPayload ),
                         static_cast<size_t>( lightsUB->GetUniform()->GetSize() ) );
                    lightsUB->SetRawData( reinterpret_cast<const std::byte*>( dirLights.data() ), sz );
                }
            }

            if ( !g.SlotMaterial )
            {
                material->ApplyDefaults();
                for ( const auto& [name, value] : g.Overrides.Params )
                    material->SetParamRaw( name, value );

                // Texture overrides: resolve asset handle -> runtime Image2D and bind by sampler name.
                // Unset samplers keep the backend fallback texture, so this is purely additive.
                for ( const auto& [name, handle] : g.Overrides.Textures )
                {
                    if ( handle == 0 )
                        continue;
                    auto* tex = Runtime::ResourceRegistry::GetTextureService()->Get( Common::UUID( handle ) );
                    if ( !tex )
                        continue;
                    auto* img = static_cast<Image2D*>(
                         Runtime::ResourceRegistry::GetImageService()->Resolve( tex->GetImageHandle() ) );
                    if ( img )
                        material->SetTexture( name, img );
                }

                // Runtime-owned texture (no asset handle) bound straight to its sampler — the text
                // SDF atlas takes this path.
                if ( g.DirectTexture && !g.DirectTextureSampler.empty() )
                    material->SetTexture( g.DirectTextureSampler, g.DirectTexture );
            }

            Renderer::GetInstance().RenderMesh( pipeline.get(), g.Mesh, g.Transform,
                                                material->GetMaterialExecutor(), 1, 0, ~g.VisibleSubmeshMask,
                                                ComputeLOD( g.Transform, g.Mesh, /*forced*/ -1 ) );
        }
    }

    void MeshRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return;

        builder.AddPass( "MeshGeometryPass", RenderPhase::Geometry,
                         [this]()
                         {
                             // Forward path only. In Deferred, meshes are drawn into the G-buffer by
                             // MeshGBufferPass instead (this target keeps sky/grid/terrain for compositing).
                             if ( m_SceneRenderer->GetRenderPath() == Core::RenderPath::Deferred &&
                                  m_StaticGBufferPipeline )
                                 return;

                             const auto camera = m_SceneRenderer->GetMainCamera();
                             if ( !camera )
                                 return;

                             UpdateGlobalUniforms( camera, m_SceneRenderer->GetPointLights(),
                                                   m_SceneRenderer->GetDirectionLights() );

                             DrawStaticMeshes();
                             DrawSkinnedMeshes();
                             DrawGenericMeshes();
                         },
                         m_StaticPipeline->GetSpecification(), targetFb,
                         { RenderPassDependency( RenderPhase::DepthPrePass ) } );

        // NOTE: the deferred G-buffer geometry is NOT a graph pass — it's rendered MANUALLY via
        // RenderGBufferManual() (called from SceneRenderer when Deferred). A graph pass targeting the G-buffer
        // would sit between the forward-target passes and break the graph's "consecutive same-framebuffer =
        // clear once" grouping, causing a spurious re-clear that wipes the sky/meshes in the scene target.

        // The silhouette mask is always produced (and cleared) so the Jump Flood outline has a
        // fresh input every frame. Outline visibility is controlled by JumpFloodOutlineRenderer.
        RegisterSilhouettePass( builder );
        RegisterShadowPass( builder );
        RegisterDebugPass( builder );
    }

    void MeshRenderer::RenderGBufferManual()
    {
        if ( !m_StaticGBufferPipeline )
            return;
        const auto& gbuffer = m_SceneRenderer ? m_SceneRenderer->GetGBuffer() : nullptr;
        if ( !gbuffer || !m_SceneRenderer->GetMainCamera() )
            return;

        auto& renderer = Renderer::GetInstance();
        // Clear the G-buffer to ZERO (not the default 0.1 grey) so empty texels have a zero normal — the
        // lighting pass uses dot(normal,normal) to tell geometry from sky, and 0.1 would read as "geometry".
        RenderPassSpecification rpSpec;
        rpSpec.TargetFramebuffer = gbuffer;
        rpSpec.DebugName         = "DeferredGBufferPass";
        rpSpec.ClearColor.Color  = glm::vec4( 0.0f, 0.0f, 0.0f, 0.0f );
        auto rp                  = RenderPass::Create( rpSpec );

        renderer.BeginRenderPass( rp.get() );
        m_DeferredGeometry = true;
        DrawStaticMeshes();
        m_DeferredGeometry = false;
        renderer.EndRenderPass();
    }

    void MeshRenderer::RenderGenericManual()
    {
        if ( m_GenericQueue.empty() )
            return;
        const auto& target = m_SceneRenderer ? m_SceneRenderer->GetTargetFramebuffer() : nullptr;
        if ( !target || !m_SceneRenderer->GetMainCamera() )
            return;

        auto& renderer = Renderer::GetInstance();

        RenderPassSpecification rpSpec;
        rpSpec.TargetFramebuffer = target;
        rpSpec.DebugName         = "GenericForwardPass";
        auto rp                  = RenderPass::Create( rpSpec );

        renderer.BeginRenderPass( rp.get(), false ); // LOAD: over the deferred lighting composite
        DrawGenericMeshes( /*useLoadPass*/ true );
        renderer.EndRenderPass();
    }

    void MeshRenderer::RenderGlassManual( const std::shared_ptr<Image2D>& sceneColor )
    {
        if ( !m_StaticGlassPipeline || !m_GlassMaterial || !m_GlassInstance || m_StaticQueue.empty() )
            return;
        const auto& target = m_SceneRenderer ? m_SceneRenderer->GetTargetFramebuffer() : nullptr;
        const auto  camera = m_SceneRenderer ? m_SceneRenderer->GetMainCamera() : nullptr;
        if ( !target || !camera )
            return;

        // Collect the transparent (Transmission > 0) objects + their effective GPU material entries. Uses a
        // DEDICATED material so the opaque passes' per-frame UBs are untouched (the double-write-per-frame that
        // hung the GPU).
        std::vector<const StaticMeshRenderData*> glassObjs;
        std::vector<PBRGpuMaterial>              gpuMats;
        for ( const auto& data : m_StaticQueue )
        {
            if ( !data.Mesh || !data.MaterialSlots || data.MaterialSlots->empty() )
                continue;
            MaterialInstance* pbrInst = FirstPBRSlot( *data.MaterialSlots );
            if ( !pbrInst )
                continue;
            auto* mat = static_cast<StaticMaterialPBR*>( pbrInst->GetParentMaterial() );
            PBRGpuMaterial gm = BuildEffectiveMaterial( mat, pbrInst );
            if ( gm.GlassTint.a <= 0.001f )
                continue; // opaque -> drawn by the opaque pass, not here
            glassObjs.push_back( &data );
            gpuMats.push_back( gm );
        }
        if ( glassObjs.empty() )
            return;

        auto& renderer = Renderer::GetInstance();

        // --- One-time shared setup on the dedicated glass material (written ONCE per frame) ---
        if ( auto* sb = m_GlassMaterial->Get<StorageBufferProperty>( "Materials" ) )
            sb->SetRawData( gpuMats.data(),
                            static_cast<uint32_t>( gpuMats.size() * sizeof( PBRGpuMaterial ) ) );

        // The whole scene contribution in one snapshot (see MeshRenderer::FrameState) — the glass pass
        // needs every part of it, including the env cube + BRDF bindings it epsilon-touches.
        MaterialInstance* gi = m_GlassInstance.get();
        CaptureFrameState( camera ).ApplyTo( gi );

        // Bind the scene snapshot the glass samples for refraction (binding 19, glass-shader-only).
        if ( sceneColor )
            if ( auto tex = m_GlassMaterial->GetMaterialExecutor()->GetTexture2DProperty( "u_SceneColor" ) )
                tex->SetImage( sceneColor.get() );

        // --- Draw the glass over the composited scene (LOAD + blend) ---
        RenderPassSpecification rpSpec;
        rpSpec.TargetFramebuffer = target;
        rpSpec.DebugName         = "GlassPass";
        auto rp                  = RenderPass::Create( rpSpec );

        renderer.BeginRenderPass( rp.get(), false ); // LOAD: composite over the opaque scene
        for ( uint32_t i = 0; i < static_cast<uint32_t>( glassObjs.size() ); ++i )
        {
            const auto* obj = glassObjs[i];
            StaticMaterialPBR::UpdateTransform( gi, obj->Transform );
            m_GlassMaterial->SetMaterialIndex( i );
            m_GlassMaterial->Bind( gi );
            renderer.RenderMesh( m_StaticGlassPipeline.get(), obj->Mesh, obj->Transform,
                                 m_GlassMaterial->GetMaterialExecutor(), 1, 0, obj->HiddenSubmeshes,
                                 ComputeLOD( obj->Transform, obj->Mesh, obj->ForcedLOD, obj->LODBias ) );
        }
        renderer.EndRenderPass();
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

        // The scene's whole contribution to a lit draw, gathered ONCE (camera, lights, shadow cascades and
        // the resolved IBL cubes + BRDF LUT). Applied per material GROUP below, not per object: only the
        // transform is per-object, and it rides a push constant.
        const FrameState frameState = CaptureFrameState( camera );

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
            if ( !data.Mesh || !data.MaterialSlots || data.MaterialSlots->empty() ||
                 !( *data.MaterialSlots )[0] )
                continue;

            // First PBR slot drives the batch. Slots holding a custom-shader material
            // (DataDrivenMaterial) are not PBR — their submeshes were routed to the generic
            // path at submit and are masked out of this draw; an object with NO PBR slot at
            // all has nothing for this path to do.
            if ( MaterialInstance* pbrInst = FirstPBRSlot( *data.MaterialSlots ) )
                groupFor( static_cast<StaticMaterialPBR*>( pbrInst->GetParentMaterial() ) ).push_back( &data );
        }

        // Accumulators for the auto-instanced path (shared across ALL material groups). The shared instanced
        // material owns ONE InstanceTransforms / Materials SSBO per frame, so every batch's data is packed
        // contiguously and uploaded ONCE: a per-batch refill of the same buffer would be overwritten by the
        // next batch before the GPU executes the recorded draws (last-write-wins). Each instanced draw then
        // reads its own transform slice via firstInstance (gl_InstanceIndex) and its own material via the
        // MaterialIndex push constant (push constants ARE snapshotted per draw, so they stay correct).
        // Instancing is disabled in the deferred G-buffer pass (its instanced variant isn't built yet) — all
        // statics take the per-object path with m_StaticGBufferPipeline.
        const bool instancingOn = m_StaticInstancedPipeline && m_StaticInstancedMaterial &&
                                  m_StaticInstancedInstance && !m_Wireframe && !m_DeferredGeometry;
        auto& instTransforms = m_ScratchInstTransforms;
        auto& instMaterials  = m_ScratchInstMaterials;
        auto& instDraws      = m_ScratchInstDraws;
        instTransforms.clear();
        instMaterials.clear();
        instDraws.clear();

        for ( auto& [mat, objects] : groups )
        {
            if ( objects.empty() )
                continue;

            // Sub-group this material's objects by Mesh*; a sub-group of >= 2 identical meshes collapses into
            // one instanced draw. Singletons (and everything when instancing is off / wireframe) take the
            // classic per-object path below — which also preserves their individual material overrides.
            std::vector<std::pair<Desert::StaticMesh*, std::vector<ObjDraw>>> byMesh;
            const auto bucketFor = [&]( Desert::StaticMesh* mesh ) -> std::vector<ObjDraw>&
            {
                for ( auto& [m, v] : byMesh )
                    if ( m == mesh )
                        return v;
                byMesh.emplace_back( mesh, std::vector<ObjDraw>{} );
                return byMesh.back().second;
            };

            // The effective material is built ONCE per object here and reused for the glass split,
            // the batch entry and the per-object SSBO (it used to be rebuilt up to three times).
            // Transparency split (per-object so instance-level Transmission overrides are honoured): a material
            // with Transmission > 0 is GLASS — skipped by every opaque pass (forward + deferred G-buffer) and
            // drawn ONLY by the glass pass (m_GlassPass), which composites forward over the scene with blending.
            for ( const auto* obj : objects )
            {
                ObjDraw od;
                od.Obj  = obj;
                od.Inst = FirstPBRSlot( *obj->MaterialSlots );
                od.Gm   = BuildEffectiveMaterial( mat, od.Inst );
                if ( ( od.Gm.GlassTint.a > 0.001f ) != m_GlassPass )
                    continue;
                if ( od.Inst )
                    for ( const auto& [pname, prop] : od.Inst->GetPropertySet().GetProperties() )
                        if ( prop.bIsOverridden )
                        {
                            od.HasOverrides = true;
                            break;
                        }
                bucketFor( obj->Mesh ).push_back( od );
            }

            auto& singles = m_ScratchSingles;
            singles.clear();
            for ( auto& [mesh, bucket] : byMesh )
            {
                // Per-object state a shared batch entry can't carry: hidden submeshes, the
                // shadow-receive opt-out, and INSTANCE OVERRIDES — a batch shares one material
                // entry, so an overridden instance in it would silently render with someone
                // else's values. All of those take the per-object path.
                std::vector<ObjDraw> batchable;
                for ( auto& od : bucket )
                {
                    if ( od.Obj->HiddenSubmeshes != 0 || !od.Obj->ReceiveShadows || od.HasOverrides )
                        singles.push_back( od );
                    else
                        batchable.push_back( od );
                }

                if ( instancingOn && batchable.size() >= 2 )
                {
                    InstancedDraw d;
                    d.Mesh          = mesh;
                    d.InstanceCount = static_cast<uint32_t>( batchable.size() );
                    d.FirstInstance = static_cast<uint32_t>( instTransforms.size() );
                    d.MaterialIndex = static_cast<uint32_t>( instMaterials.size() );
                    for ( const auto& od : batchable )
                        instTransforms.push_back( od.Obj->Transform );
                    // No batchable object carries overrides (filtered above), so every instance of
                    // the batch genuinely shares the parent material's effective values.
                    instMaterials.push_back( batchable[0].Gm );
                    instDraws.push_back( d );
                }
                else
                {
                    for ( const auto& od : batchable )
                        singles.push_back( od );
                }
            }

            if ( singles.empty() )
                continue;

            // ---- Classic per-object path (singletons / wireframe) ----
            // Fill this material's per-object storage buffer (one GpuMaterial per drawn object).
            auto& gpuMaterials = m_ScratchGpuMaterials;
            gpuMaterials.clear();
            gpuMaterials.reserve( singles.size() );
            for ( const auto& od : singles )
            {
                PBRGpuMaterial gm = od.Gm;
                // ExtraParams.w rides the per-mesh Receive Shadows toggle (1 = skip sun shadows);
                // the batched path only ever carries receivers, so it stays 0 there.
                gm.ExtraParams.w = od.Obj->ReceiveShadows ? 0.0f : 1.0f;
                gpuMaterials.push_back( gm );
            }

            if ( auto* sb = mat->Get<StorageBufferProperty>( "Materials" ) )
                sb->SetRawData( gpuMaterials.data(),
                                static_cast<uint32_t>( gpuMaterials.size() * sizeof( PBRGpuMaterial ) ) );

            // SHARED per-frame scene data (camera / lights / shadow / env) is written ONCE per material
            // group, NOT per mesh: these Update* all write the PARENT material's buffers (shared by every
            // instance) and depend only on scene-global state. Only the transform is per-object (push
            // constant), so it stays in the draw loop below.
            {
                DESERT_PROFILE_SCOPE( "Mesh: SharedSceneSetup (1x/group)" );
                frameState.ApplyTo( singles[0].Inst );
            }

            for ( uint32_t i = 0; i < static_cast<uint32_t>( singles.size() ); ++i )
            {
                const auto*       obj  = singles[i].Obj;
                MaterialInstance* inst = singles[i].Inst;

                {
                    // Per-object work: transform (push constant) + material index + descriptor bind.
                    DESERT_PROFILE_SCOPE( "Mesh: PerObject Setup" );
                    StaticMaterialPBR::UpdateTransform( inst, obj->Transform );
                    mat->SetMaterialIndex( i );
                    mat->Bind( inst );
                }

                {
                    // The actual draw call (bind pipeline + descriptor sets + vkCmdDrawIndexed).
                    DESERT_PROFILE_SCOPE( "Mesh: RenderMesh (draw)" );
                    // Deferred: the same material data binds, but the pipeline writes the G-buffer (MRT) instead
                    // of shading. Otherwise forward (wireframe variant when enabled).
                    auto* pipeline = ( m_GlassPass && m_StaticGlassPipeline )
                                          ? m_StaticGlassPipeline.get()
                                          : ( m_DeferredGeometry && m_StaticGBufferPipeline )
                                                ? m_StaticGBufferPipeline.get()
                                                : ( m_Wireframe && m_StaticWireframePipeline )
                                                      ? m_StaticWireframePipeline.get()
                                                      : m_StaticPipeline.get();
                    const uint32_t lod = ComputeLOD( obj->Transform, obj->Mesh, obj->ForcedLOD, obj->LODBias );
                    renderer.RenderMesh( pipeline, obj->Mesh, obj->Transform, mat->GetMaterialExecutor(), 1, 0,
                                         obj->HiddenSubmeshes, lod );
                }
            }
        }

        // UE-style Instanced Static Meshes (one entity = N instances): fold into the same instanced
        // accumulation as the auto-batched static meshes. Each ISM is one batch; its transforms come
        // straight from the component array (no per-instance ECS cost), appended to the shared SSBO.
        if ( instancingOn )
        {
            for ( const auto& ism : m_InstancedQueue )
            {
                if ( !ism.Mesh || !ism.Material || !ism.Transforms || ism.Transforms->empty() )
                    continue;
                auto* mat = static_cast<StaticMaterialPBR*>( ism.Material->GetParentMaterial() );
                if ( !mat )
                    continue;

                InstancedDraw d;
                d.Mesh          = ism.Mesh;
                d.InstanceCount = static_cast<uint32_t>( ism.Transforms->size() );
                d.FirstInstance = static_cast<uint32_t>( instTransforms.size() );
                d.MaterialIndex = static_cast<uint32_t>( instMaterials.size() );
                instTransforms.insert( instTransforms.end(), ism.Transforms->begin(), ism.Transforms->end() );
                instMaterials.push_back( BuildEffectiveMaterial( mat, ism.Material ) );
                instDraws.push_back( d );
            }
        }

        // ---- Instanced path: upload the packed SSBOs ONCE (at final size) before recording any instanced
        // draw, so the descriptor points at the final VkBuffer (a later grow reallocates it). Scene data is
        // uploaded a single time for the whole frame; each batch is then one instanced draw call. ----
        if ( instancingOn && !instDraws.empty() )
        {
            DESERT_PROFILE_SCOPE( "Mesh: Instanced Batches" );
            auto* instMat  = m_StaticInstancedMaterial.get();
            auto* instInst = m_StaticInstancedInstance.get();

            if ( auto* sb = instMat->Get<StorageBufferProperty>( "InstanceTransforms" ) )
                sb->SetRawData( instTransforms.data(),
                                static_cast<uint32_t>( instTransforms.size() * sizeof( glm::mat4 ) ) );
            if ( auto* sb = instMat->Get<StorageBufferProperty>( "Materials" ) )
                sb->SetRawData( instMaterials.data(),
                                static_cast<uint32_t>( instMaterials.size() * sizeof( PBRGpuMaterial ) ) );

            frameState.ApplyTo( instInst );
            StaticMaterialPBR::UpdateTransform( instInst, glm::mat4( 1.0f ) ); // unused by the instanced VS

            for ( const auto& d : instDraws )
            {
                instMat->SetMaterialIndex( d.MaterialIndex );
                instMat->Bind( instInst );
                renderer.RenderMesh( m_StaticInstancedPipeline.get(), d.Mesh, glm::mat4( 1.0f ),
                                     instMat->GetMaterialExecutor(), d.InstanceCount, d.FirstInstance );
            }
        }
    }

    void MeshRenderer::DrawSkinnedMeshes( bool useLoadPass )
    {
        if ( m_SkinnedQueue.empty() )
            return;

        auto&       renderer    = Renderer::GetInstance();
        const auto  camera      = m_SceneRenderer->GetMainCamera();
        const auto& pointLights = m_SceneRenderer->GetPointLights();
        const auto& spotLights  = m_SceneRenderer->GetSpotLights();

        // Deferred forward-over-composite: a LOAD-render-pass variant of the skinned pipeline (built once via
        // the pipeline cache), so skinned meshes draw OVER the deferred scene instead of clearing it. Same
        // mechanism the generic + glass passes use. Forward path keeps the plain pipeline (no load).
        GraphicsPipeline* pipeline = m_SkinnedPipeline.get();
        if ( useLoadPass && m_SkinnedPipeline )
        {
            GraphicsPipelineSpecification spec = m_SkinnedPipeline->GetSpecification();
            spec.UseLoadRenderPass             = true;
            spec.DebugName                     = "SkinnedMesh_Load";
            if ( auto p = m_SceneRenderer->GetPipelineCache().GetOrCreate( spec ) )
                pipeline = p.get();
        }

        for ( const auto& data : m_SkinnedQueue )
        {
            if ( !data.Mesh || !data.Material || !data.Instance )
                continue;

            data.Material->Bind( { .instance        = data.Instance,
                                   .MainCamera      = camera,
                                   .MeshTransform   = data.Transform,
                                   .DirectionLights = m_SceneRenderer->GetDirectionLights(),
                                   .PointLights     = pointLights,
                                   .SpotLights      = spotLights,
                                   .SkinnedUB       = { .BoneMatrices = data.BoneMatrices } } );

            renderer.RenderMesh( pipeline, data.Mesh, data.Transform, data.Material->GetMaterialExecutor() );
        }
    }

    void MeshRenderer::RenderSkinnedManual()
    {
        if ( m_SkinnedQueue.empty() )
            return;
        const auto& target = m_SceneRenderer ? m_SceneRenderer->GetTargetFramebuffer() : nullptr;
        if ( !target || !m_SceneRenderer->GetMainCamera() )
            return;

        auto& renderer = Renderer::GetInstance();

        RenderPassSpecification rpSpec;
        rpSpec.TargetFramebuffer = target;
        rpSpec.DebugName         = "SkinnedForwardPass";
        auto rp                  = RenderPass::Create( rpSpec );

        renderer.BeginRenderPass( rp.get(), false ); // LOAD: over the deferred lighting composite
        DrawSkinnedMeshes( /*useLoadPass*/ true );
        renderer.EndRenderPass();
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

        // Pipelines come from the shared cache (deduped by shader + target + state). The mesh keeps its
        // explicit state for now; PBR's render-state moves to the shader's #pragma state in Phase 2.
        m_StaticPipeline = m_SceneRenderer->GetPipelineCache().GetOrCreate( spec );

        // Wireframe variant — identical spec, line polygon mode (device feature fillModeNonSolid is on).
        // Selected per-frame by the SceneSettings debug toggle; shares the same framebuffer/render pass.
        spec.DebugName   = "StaticMeshWireframe";
        spec.PolygonMode = PrimitivePolygonMode::Wireframe;
        m_StaticWireframePipeline = m_SceneRenderer->GetPipelineCache().GetOrCreate( spec );

        // Instanced variant: same vertex layout + state, but the vertex shader pulls the per-instance model
        // matrix from the InstanceTransforms SSBO (binding 16) by gl_InstanceIndex. Drawn via one instanced
        // draw call (RenderMeshInstanced). Optional — if the shader is missing, instancing is just disabled.
        m_InstancedGeometryShader =
             Runtime::ResourceRegistry::GetShaderService()->GetByName( "StaticMeshPBR_Instanced" );
        if ( m_InstancedGeometryShader )
        {
            GraphicsPipelineSpecification ispec;
            ispec.DebugName      = "StaticMeshGeometryInstanced";
            ispec.Layout         = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                     { Graphic::ShaderDataType::Float3, "a_Normal" },
                                     { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                     { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                     { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };
            ispec.DepthCompareOp = CompareOp::LessOrEqual;
            ispec.CullMode       = CullMode::Back;
            ispec.Shader         = m_InstancedGeometryShader;
            ispec.Framebuffer    = targetFb;
            m_StaticInstancedPipeline = m_SceneRenderer->GetPipelineCache().GetOrCreate( ispec );
        }

        return true;
    }

    bool MeshRenderer::SetupGBufferPass()
    {
        // Optional: only present when the deferred G-buffer shader exists and the scene renderer has a
        // G-buffer. Failure here does NOT fail Initialize — the forward path stays fully functional.
        m_StaticGBufferShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "StaticMeshGBuffer" );
        if ( !m_StaticGBufferShader )
            return false;

        const auto& gbuffer = m_SceneRenderer ? m_SceneRenderer->GetGBuffer() : nullptr;
        if ( !gbuffer )
            return false;

        GraphicsPipelineSpecification spec;
        spec.DebugName      = "StaticMeshGBuffer";
        spec.Layout         = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                { Graphic::ShaderDataType::Float3, "a_Normal" },
                                { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };
        spec.DepthCompareOp = CompareOp::LessOrEqual;
        spec.CullMode       = CullMode::Back;
        spec.Shader         = m_StaticGBufferShader;
        spec.Framebuffer    = gbuffer; // 2 color attachments -> the shader's 2 MRT outputs

        m_StaticGBufferPipeline = m_SceneRenderer->GetPipelineCache().GetOrCreate( spec );
        return m_StaticGBufferPipeline != nullptr;
    }

    bool MeshRenderer::SetupGlassPass()
    {
        // Optional (like the G-buffer pass): needs the glass shader + the scene target. Failure leaves the
        // rest fully functional — glass just won't draw.
        m_StaticGlassShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "StaticMeshGlass" );
        if ( !m_StaticGlassShader )
            return false;

        const auto& target = m_SceneRenderer ? m_SceneRenderer->GetTargetFramebuffer() : nullptr;
        if ( !target )
            return false;

        GraphicsPipelineSpecification spec;
        spec.DebugName         = "StaticMeshGlass";
        spec.Layout            = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                   { Graphic::ShaderDataType::Float3, "a_Normal" },
                                   { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                   { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                   { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };
        spec.Shader            = m_StaticGlassShader;
        spec.Framebuffer       = target;
        spec.DepthCompareOp    = CompareOp::LessOrEqual;
        spec.DepthWriteEnabled = false;      // transparent: don't occlude later fragments / itself
        spec.CullMode          = CullMode::Back;
        spec.BlendEnable       = true;       // src-alpha over the composited scene
        spec.UseLoadRenderPass = true;       // begun with clearFrame=false to preserve the opaque scene

        m_StaticGlassPipeline = m_SceneRenderer->GetPipelineCache().GetOrCreate( spec );
        if ( !m_StaticGlassPipeline )
            return false;

        // A dedicated material (+ one instance) owns the glass pass's per-frame UBs / Materials SSBO. It is
        // descriptor-layout-compatible with the glass pipeline because Glass.glsl.frag declares the same
        // bindings as StaticMeshPBR. Being separate from every opaque material, its per-frame ring is written
        // exactly once per frame (here) — no double-update hang.
        m_GlassMaterial = std::make_unique<MaterialGlass>();
        m_GlassInstance = m_GlassMaterial->CreateInstance();
        return m_GlassMaterial && m_GlassInstance;
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

        // Skinned silhouette variant (optional): same mask target/state, but the skinned vertex layout +
        // the Silhouette_Skinned shader (skins by the Bones SSBO). Used to outline selected skinned meshes.
        m_SilhouetteSkinnedShader =
             Runtime::ResourceRegistry::GetShaderService()->GetByName( "Silhouette_Skinned" );
        if ( m_SilhouetteSkinnedShader )
        {
            GraphicsPipelineSpecification sspec = spec;
            sspec.DebugName = "SilhouetteSkinnedPipeline";
            sspec.Layout    = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                { Graphic::ShaderDataType::Float3, "a_Normal" },
                                { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                { Graphic::ShaderDataType::Float2, "a_TextureCoord" },
                                { Graphic::ShaderDataType::Int4, "a_BoneIndices" },
                                { Graphic::ShaderDataType::Float4, "a_BoneWeights" } };
            sspec.Shader            = m_SilhouetteSkinnedShader;
            m_SilhouetteSkinnedPipeline = GraphicsPipeline::Create( sspec );
            m_SilhouetteSkinnedPipeline->Invalidate();
            m_SilhouetteSkinnedMaterial = std::make_unique<MaterialSilhouetteSkinned>();
        }

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

        // Instanced shadow caster (optional): same depth-only state, but the vertex pulls per-instance model
        // matrices from the InstanceTransforms SSBO. One instanced material per cascade (each its own light
        // matrix UBO + SSBO). If the shader is missing, instanced shadows are simply disabled.
        m_ShadowInstancedShader =
             Runtime::ResourceRegistry::GetShaderService()->GetByName( "Shadow_Instanced" );
        if ( m_ShadowInstancedShader )
        {
            GraphicsPipelineSpecification ispec = spec;
            ispec.DebugName = "ShadowPipelineInstanced";
            ispec.Shader    = m_ShadowInstancedShader;
            m_ShadowInstancedPipeline = GraphicsPipeline::Create( ispec );
            m_ShadowInstancedPipeline->Invalidate();

            for ( uint32_t i = 0; i < kNumCascades; ++i )
                m_ShadowInstancedMaterial[i] = std::make_unique<MaterialShadowInstanced>();
        }

        return true;
    }

    MeshRenderer::FrameState MeshRenderer::CaptureFrameState( const Core::Camera* camera ) const
    {
        FrameState frame;
        frame.Camera = camera;

        frame.PointLights     = &m_SceneRenderer->GetPointLights();
        frame.SpotLights      = &m_SceneRenderer->GetSpotLights();
        frame.DirectionLights = &m_SceneRenderer->GetDirectionLights();

        frame.CascadeViewProj = m_CascadeVP;
        for ( uint32_t c = 0; c < kNumCascades; ++c )
            frame.CascadeMaps[c] = m_CascadeFB[c] ? m_CascadeFB[c]->GetColorAttachmentImage().get() : nullptr;
        frame.CascadeTexelWorld = m_CascadeWorldPerTexel;
        frame.ShadowBias        = m_ShadowBias;
        frame.ShadowsEnabled    = m_ShadowsEnabled;
        frame.ShadowDebugMode   = m_ShadowDebugMode;
        frame.ShowNormals       = m_ShowNormals;
        frame.LightingDebug     = m_LightingDebug;

        // The active IBL environment (diffuse irradiance + prefiltered specular) and the split-sum BRDF
        // LUT, resolved once so each PBR object samples real ambient/reflections instead of the dummy cube.
        auto* imageService = Runtime::ResourceRegistry::GetImageService();
        if ( const auto& env = m_SceneRenderer->GetEnvironment(); env.has_value() )
        {
            if ( env->IrradianceMap.IsValid() )
                frame.IrradianceMap = static_cast<ImageCube*>( imageService->Resolve( env->IrradianceMap ) );
            if ( env->PreFilteredMap.IsValid() )
                frame.PrefilteredMap = static_cast<ImageCube*>( imageService->Resolve( env->PreFilteredMap ) );
        }
        if ( const auto& brdf = Renderer::GetInstance().GetBRDFTexture();
             brdf && brdf->GetImageHandle().IsValid() )
            frame.BrdfLut = static_cast<Image2D*>( imageService->Resolve( brdf->GetImageHandle() ) );

        return frame;
    }

    void MeshRenderer::FrameState::ApplyTo( MaterialInstance* instance ) const
    {
        if ( !instance )
            return;

        StaticMaterialPBR::UpdateCamera( instance, Camera );
        if ( PointLights && SpotLights && DirectionLights )
            StaticMaterialPBR::UpdateLights( instance, *PointLights, *SpotLights, *DirectionLights );

        // The const_cast is the shape of the old API (it takes a mutable pointer array); the snapshot
        // itself is read-only, which is the point.
        Image2D* maps[4] = { CascadeMaps[0], CascadeMaps[1], CascadeMaps[2], CascadeMaps[3] };
        StaticMaterialPBR::UpdateShadow( instance, CascadeViewProj, maps, kNumCascades, ShadowBias, ShadowsEnabled,
                                         ShadowDebugMode, ShowNormals, CascadeTexelWorld, LightingDebug );

        StaticMaterialPBR::UpdateEnvironment( instance, IrradianceMap, PrefilteredMap, BrdfLut );
    }

    void MeshRenderer::UpdateCascades()
    {
        const auto camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return;

        const auto& dirLights = m_SceneRenderer->GetDirectionLights();
        if ( dirLights.DirectionLights.empty() )
            return;

        // The fitting itself is pure math and lives in Engine/Graphic/ShadowCascades.hpp so a test can pin
        // it down — this function only feeds it the scene's numbers and stores the result.
        CascadeSetup setup;
        setup.CameraView       = camera->GetViewMatrix();
        setup.CameraProjection = camera->GetProjectionMatrix();
        setup.CameraNear       = camera->GetNear();
        setup.CameraFar        = camera->GetFar();
        setup.LightDirection   = glm::vec3( dirLights.DirectionLights[0].Direction );
        setup.MaxDistance      = kShadowMaxDistance;
        setup.SplitLambda      = m_SplitLambda;
        setup.CascadeCount     = kNumCascades;
        setup.ShadowMapSize    = kShadowMapSize;

        CascadeFit     fits[kMaxShadowCascades];
        const uint32_t n = ComputeShadowCascades( setup, fits );
        for ( uint32_t c = 0; c < n; ++c )
        {
            m_CascadeVP[c]            = fits[c].ViewProj;
            m_CascadeWorldPerTexel[c] = fits[c].WorldPerTexel;
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

                     // Shadow casters are MATERIAL-INDEPENDENT (depth only), so batch purely by Mesh*: any
                     // group of >= 2 identical meshes collapses into ONE instanced draw per cascade. This is
                     // the dominant cost in the 256-mesh stress test (256x4 per-object draws -> 4 draws).
                     const bool instancingOn = m_ShadowInstancedPipeline && m_ShadowInstancedMaterial[c];

                     std::vector<std::pair<Desert::StaticMesh*, std::vector<const StaticMeshRenderData*>>> byMesh;
                     const auto bucketFor =
                          [&]( Desert::StaticMesh* mesh ) -> std::vector<const StaticMeshRenderData*>&
                     {
                         for ( auto& [m, v] : byMesh )
                             if ( m == mesh )
                                 return v;
                         byMesh.emplace_back( mesh, std::vector<const StaticMeshRenderData*>{} );
                         return byMesh.back().second;
                     };
                     for ( const auto& rd : m_StaticQueue )
                         if ( rd.Mesh && rd.CastShadows )
                             bucketFor( rd.Mesh ).push_back( &rd );

                     // Pack all instanced-batch transforms contiguously; each batch reads its slice via
                     // firstInstance. Upload the SSBO ONCE (final size) before any instanced draw is recorded.
                     // Scratch members: capacity persists across cascades/frames (4 refills per frame).
                     auto& instTransforms = m_ScratchInstTransforms;
                     auto& batches        = m_ScratchShadowBatches;
                     auto& singles        = m_ScratchShadowSingles;
                     instTransforms.clear();
                     batches.clear();
                     singles.clear();
                     for ( auto& [mesh, bucket] : byMesh )
                     {
                         if ( instancingOn && bucket.size() >= 2 )
                         {
                             ShadowBatch b{ mesh, static_cast<uint32_t>( bucket.size() ),
                                            static_cast<uint32_t>( instTransforms.size() ) };
                             for ( const auto* rd : bucket )
                                 instTransforms.push_back( rd->Transform );
                             batches.push_back( b );
                         }
                         else
                         {
                             for ( const auto* rd : bucket )
                                 singles.push_back( rd );
                         }
                     }

                     // UE-style Instanced Static Meshes cast shadows too — append each as its own batch
                     // (transforms straight from the component array).
                     if ( instancingOn )
                     {
                         for ( const auto& ism : m_InstancedQueue )
                         {
                             if ( !ism.Mesh || !ism.Transforms || ism.Transforms->empty() )
                                 continue;
                             ShadowBatch b{ ism.Mesh, static_cast<uint32_t>( ism.Transforms->size() ),
                                            static_cast<uint32_t>( instTransforms.size() ) };
                             instTransforms.insert( instTransforms.end(), ism.Transforms->begin(),
                                                    ism.Transforms->end() );
                             batches.push_back( b );
                         }
                     }

                     // Per-object path (singletons).
                     for ( const auto* rd : singles )
                         renderer.RenderMesh( m_ShadowPipeline.get(), rd->Mesh, rd->Transform,
                                              m_ShadowMaterial[c]->GetMaterialExecutor(), 1, 0, 0,
                                              ComputeLOD( rd->Transform, rd->Mesh, rd->ForcedLOD,
                                                          rd->LODBias ) );

                     // Instanced path.
                     if ( instancingOn && !batches.empty() )
                     {
                         auto* instMat = m_ShadowInstancedMaterial[c].get();
                         instMat->SetLightMatrix( glm::mat4( 1.0f ), m_CascadeVP[c] );
                         if ( auto* sb = instMat->Get<StorageBufferProperty>( "InstanceTransforms" ) )
                             sb->SetRawData( instTransforms.data(),
                                             static_cast<uint32_t>( instTransforms.size() * sizeof( glm::mat4 ) ) );
                         for ( const auto& b : batches )
                             renderer.RenderMesh( m_ShadowInstancedPipeline.get(), b.Mesh, glm::mat4( 1.0f ),
                                                  instMat->GetMaterialExecutor(), b.Count, b.First );
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

    bool MeshRenderer::SetupOverdrawPass()
    {
        m_OverdrawShader        = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Overdraw" );
        m_OverdrawResolveShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "OverdrawResolve" );
        if ( !m_OverdrawShader || !m_OverdrawResolveShader )
            return false;

        const auto& targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return false;

        // Accumulation target: RGBA32F so many additive fragments don't clip. Cleared to 0 each frame; each
        // drawn fragment adds a small constant, so the .r channel ends up holding overdraw-count * step.
        FramebufferSpecification accumSpec;
        accumSpec.DebugName = "OverdrawAccum";
        accumSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );
        m_OverdrawFB = Graphic::Framebuffer::Create( accumSpec );
        m_OverdrawFB->Resize( targetFb->GetFramebufferWidth(), targetFb->GetFramebufferHeight() );

        // Geometry accumulation pipeline: static-mesh layout (same as silhouette), ADDITIVE blend, and NO
        // depth test — every fragment (even occluded ones) must count, which is exactly what overdraw measures.
        GraphicsPipelineSpecification spec;
        spec.DebugName           = "OverdrawPipeline";
        spec.Layout              = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                     { Graphic::ShaderDataType::Float3, "a_Normal" },
                                     { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                     { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                     { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };
        spec.Shader              = m_OverdrawShader;
        spec.Framebuffer         = m_OverdrawFB;
        spec.DepthTestEnabled    = false;
        spec.DepthWriteEnabled   = false;
        spec.CullMode            = CullMode::None;
        spec.BlendEnable         = true;
        spec.SrcColorBlendFactor = BlendFactor::One;
        spec.DstColorBlendFactor = BlendFactor::One;
        m_OverdrawPipeline       = GraphicsPipeline::Create( spec );
        m_OverdrawPipeline->Invalidate();
        m_OverdrawMaterial = std::make_unique<MaterialOverdraw>();

        // Fullscreen resolve: heat-map the accumulation over the scene colour (LOAD so the scene shows through).
        GraphicsPipelineSpecification rspec;
        rspec.DebugName           = "OverdrawResolvePipeline";
        rspec.Shader              = m_OverdrawResolveShader;
        rspec.Framebuffer         = targetFb;
        rspec.DepthTestEnabled    = false;
        rspec.DepthWriteEnabled   = false;
        rspec.UseLoadRenderPass   = true;
        m_OverdrawResolvePipeline = GraphicsPipeline::Create( rspec );
        m_OverdrawResolvePipeline->Invalidate();
        m_OverdrawResolveMaterial = std::make_unique<MaterialOverdrawResolve>();

        return true;
    }

    void MeshRenderer::RenderOverdrawManual()
    {
        if ( !m_OverdrawPipeline || !m_OverdrawFB || !m_OverdrawResolvePipeline )
            return;
        const auto& target = m_SceneRenderer ? m_SceneRenderer->GetTargetFramebuffer() : nullptr;
        const auto  camera = m_SceneRenderer ? m_SceneRenderer->GetMainCamera() : nullptr;
        if ( !target || !camera )
            return;

        auto& renderer = Renderer::GetInstance();

        // 1) Accumulate: clear to 0, then draw every opaque mesh additively (static + generic; both use the
        //    static vertex layout). Skinned meshes are skipped — they'd need the skinned layout + bone SSBO.
        {
            RenderPassSpecification rpSpec;
            rpSpec.TargetFramebuffer = m_OverdrawFB;
            rpSpec.DebugName         = "OverdrawAccumPass";
            rpSpec.ClearColor.Color  = glm::vec4( 0.0f );
            auto rp                  = RenderPass::Create( rpSpec );

            renderer.BeginRenderPass( rp.get() );
            m_OverdrawMaterial->UpdateCamera( camera );
            for ( const auto& rd : m_StaticQueue )
                if ( rd.Mesh )
                    renderer.RenderMesh( m_OverdrawPipeline.get(), rd.Mesh, rd.Transform,
                                         m_OverdrawMaterial->GetMaterialExecutor() );
            for ( const auto& g : m_GenericQueue )
                if ( g.Mesh )
                    renderer.RenderMesh( m_OverdrawPipeline.get(), g.Mesh, g.Transform,
                                         m_OverdrawMaterial->GetMaterialExecutor() );
            renderer.EndRenderPass();
        }

        // 2) Resolve: heat-map the accumulation over the scene colour (LOAD; the resolve discards empty texels).
        {
            RenderPassSpecification rpSpec;
            rpSpec.TargetFramebuffer = target;
            rpSpec.DebugName         = "OverdrawResolvePass";
            auto rp                  = RenderPass::Create( rpSpec );

            renderer.BeginRenderPass( rp.get(), false );
            m_OverdrawResolveMaterial->Bind( m_OverdrawFB->GetColorAttachmentImage( 0 ) );
            renderer.SubmitFullscreenQuad( m_OverdrawResolvePipeline.get(),
                                           m_OverdrawResolveMaterial->GetMaterialExecutor() );
            renderer.EndRenderPass();
        }
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

                             // ===== Generic (data-driven materials) — same Silhouette pipeline, the
                             // material's shader is irrelevant for the mask (just geometry + transform).
                             for ( const auto& g : m_GenericQueue )
                             {
                                 if ( !g.Outlined || !g.Mesh )
                                     continue;
                                 renderer.RenderMesh( m_SilhouettePipeline.get(), g.Mesh, g.Transform,
                                                      m_SilhouetteMaterial->GetMaterialExecutor() );
                             }

                             // ===== Skinned ===== — skin the mask by the SAME bone matrices the mesh is
                             // rendered with (animated or bind) so the outline tracks the posed shape.
                             if ( m_SilhouetteSkinnedPipeline && m_SilhouetteSkinnedMaterial )
                             {
                                 for ( const auto& sd : m_SkinnedQueue )
                                 {
                                     if ( !sd.Outlined || !sd.Mesh )
                                         continue;
                                     m_SilhouetteSkinnedMaterial->UpdateCamera( camera );
                                     m_SilhouetteSkinnedMaterial->SetBones( sd.BoneMatrices );
                                     renderer.RenderMesh( m_SilhouetteSkinnedPipeline.get(), sd.Mesh, sd.Transform,
                                                          m_SilhouetteSkinnedMaterial->GetMaterialExecutor() );
                                 }
                             }
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
                staticData.Mesh            = static_cast<StaticMesh*>( data.Mesh );
                staticData.Transform       = data.Transform;
                staticData.MaterialSlots   = data.MaterialSlots;
                staticData.Outlined        = data.Outlined;
                staticData.HiddenSubmeshes = data.HiddenSubmeshes;
                staticData.ForcedLOD       = data.ForcedLOD;
                staticData.LODBias         = data.LODBias;
                staticData.CastShadows     = data.CastShadows;
                staticData.ReceiveShadows  = data.ReceiveShadows;

                m_StaticQueue.push_back( staticData );
                break;
            }

            case MeshType::Skinned:
            {
                SkinnedMeshRenderData skinnedData;
                skinnedData.Mesh         = static_cast<SkinnedMesh*>( data.Mesh );
                skinnedData.Transform    = data.Transform;
                skinnedData.BoneMatrices = data.BoneMatrices;
                skinnedData.Outlined     = data.Outlined;
                if ( data.MaterialSlots && !data.MaterialSlots->empty() )
                {
                    // Custom-shader slot materials are not usable here: a skinned mesh needs the
                    // skinning vertex stage, which generic DSL surface shaders don't have. Take
                    // the first slot whose parent IS the skinned PBR material (mirrors the
                    // static path's FirstPBRSlot guard); warn once so the fallback isn't silent.
                    for ( auto* inst : *data.MaterialSlots )
                    {
                        if ( !inst )
                            continue;
                        if ( auto* pbr = dynamic_cast<SkinnedMaterialPBR*>( inst->GetParentMaterial() ) )
                        {
                            skinnedData.Instance = inst;
                            skinnedData.Material = pbr;
                            break;
                        }
                    }
                    if ( !skinnedData.Material )
                    {
                        static bool s_WarnedCustomSkinned = false;
                        if ( !s_WarnedCustomSkinned )
                        {
                            LOG_WARN( "Skinned meshes don't support custom-shader materials (no skinning "
                                      "stage in surface shaders) — mesh skipped. Assign a PBR material." );
                            s_WarnedCustomSkinned = true;
                        }
                    }
                }
                if ( skinnedData.Material && skinnedData.Instance )
                    m_SkinnedQueue.push_back( std::move( skinnedData ) );
                break;
            }
        }
    }

} // namespace Desert::Graphic::System
