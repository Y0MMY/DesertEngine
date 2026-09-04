#include "TerrainRenderer.hpp"

#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/Clouds/CloudShadowBinding.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Engine/Graphic/Materials/Properties/StorageBufferProperty.hpp>
#include <Common/Core/Units.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace Desert::Graphic::System
{
    namespace
    {
        // Matches the "TerrainUB" block (binding 0) in the Terrain shader stages. Engine-filled.
        struct TerrainUB
        {
            glm::mat4 View;
            glm::mat4 Projection;
            glm::mat4 Model;
            glm::vec4 Params;     // x=size, y=gridDim, z=heightScale, w=tessLevel
            glm::vec4 Params2;    // x=noiseFrequency, y=seed, z/w=spare
            glm::vec4 LayerModes; // x=grass, y=rock, z=snow (0=Auto,1=Manual,2=Off), w=grassEnable
            glm::vec4 SunDir;     // xyz = normalized light direction (scene directional light)
            glm::vec4 SunColor;   // rgb = color, a = intensity
        };

        // Matches the "GrassUB" block (binding 0) in Grass.glsl.vert. Engine-filled.
        struct GrassUB
        {
            glm::mat4 View;
            glm::mat4 Projection;
            glm::mat4 Model;
            glm::vec4 Params;     // x=size, y=unused, z=heightScale, w=noiseFrequency
            glm::vec4 Params2;    // x=seed, y=grassGridDim, z=bladeHeight, w=maxDist
            glm::vec4 Wind;       // xy=dir, z=strength, w=time
            glm::vec4 CameraPos;  // xyz
            glm::vec4 Interactor; // xyz=pos, w=radius (0=none) — actor trample hook (future)
            glm::vec4 SunDir;     // xyz = normalized light direction (scene directional light)
            glm::vec4 SunColor;   // rgb = color, a = intensity
            glm::vec4 GrassTint;  // rgb = user tint multiplier, w = spare
        };

        // Push constants for GrassCull.glsl.comp (96 bytes, well under the 128-byte guaranteed minimum).
        struct GrassCullPush
        {
            glm::mat4 MVP;     // Proj * View * Model (terrain-local -> clip)
            glm::vec4 Params;  // x=size, y=splatPresent, z=heightScale, w=noiseFrequency
            glm::vec4 Params2; // x=seed, y=grid, z=bladeHeight, w=maxDist
        };

        // VkDrawIndirectCommand mirror (the cull compute writes instanceCount; the draw reads all four).
        struct GrassDrawIndirect
        {
            uint32_t VertexCount;
            uint32_t InstanceCount;
            uint32_t FirstVertex;
            uint32_t FirstInstance;
        };

        // Grass draw distance, in WORLD UNITS (centimetres). It reaches both the cull compute and the grass
        // vertex shader as Params2.w, and in BOTH it now means the same thing: the distance at which a blade
        // has shrunk to nothing and the clump is rejected.
        //
        // It was a bare 45.0f from the metre era — forty-five CENTIMETRES, and the vertex shader faded blades
        // out over 0.25..0.45 of it, so grass reached twenty centimetres from the camera and the field was
        // empty at every real scene scale.
        //
        // 120 m is the UE landscape-grass band (End Cull Distance is typically authored at 100-200 m for
        // ground clutter). What it costs: the grid is GrassDensity^2 clumps spread over the terrain, so the
        // spacing is Size/GrassDensity and the clumps inside a 16:9 frustum wedge number about
        // 0.2 * pi * dist^2 / spacing^2. For the 400 m showcase terrain at density 320 (spacing 1.25 m) that
        // is ~5.9k clumps, i.e. ~0.7 M vertices per frame at 5 blades x 24 verts — one medium mesh. On a
        // terrain small enough that the whole field is inside 120 m the limit is GrassDensity^2 instead, and
        // the density slider is what pays for it.
        constexpr float kGrassMaxDist = Common::Units::Metres( 120.0f );

        // Geometric blade: kSegments(4) quads -> kSegments*6 = 24 verts/blade. The indirect draw's
        // per-instance vertex count = bladesPerClump * this (MUST match Grass.glsl.vert's kSegments).
        constexpr uint32_t kGrassVertsPerBlade = 24u;

        // Resolve the scene's main directional light (or a sensible default sun if none exists).
        void GetSun( const SceneRenderer* sr, glm::vec4& outDir, glm::vec4& outColor )
        {
            outDir   = glm::vec4( glm::normalize( glm::vec3( -0.4f, -0.85f, -0.35f ) ), 0.0f );
            outColor = glm::vec4( 1.0f, 0.98f, 0.92f, 3.0f );
            if ( sr )
            {
                const auto& dl = sr->GetDirectionLights().DirectionLights;
                if ( !dl.empty() )
                {
                    outDir   = dl[0].Direction;
                    outColor = dl[0].ColorIntensity;
                }
            }
        }

        // CPU-bake a grass-clump alpha ATLAS ONCE: 4 columns = 4 blade FORMS (0=straight, 1=curved,
        // 2=stem, 3=bush). The vertex shader picks one column per clump so the field shows varied
        // silhouettes from a single texture + single draw. Each cell holds many fine tapered blades
        // (rgb = baked root->tip olive gradient + per-blade variation, a = coverage); the fragment
        // samples it once, so FPS is independent of blade detail.
        std::shared_ptr<Image2D> BakeGrassClumpTexture()
        {
            const uint32_t             kForms = 4, CW = 128, H = 256, W = CW * kForms;
            std::vector<unsigned char> px( static_cast<size_t>( W ) * H * 4, 0 );

            auto rnd = []( float n )
            {
                float s = std::sin( n * 78.233f ) * 43758.5453f;
                return s - std::floor( s );
            };
            auto ss = []( float e0, float e1, float x )
            {
                float t = std::clamp( ( x - e0 ) / ( e1 - e0 ), 0.0f, 1.0f );
                return t * t * ( 3.0f - 2.0f * t );
            };

            struct Blade
            {
                float x0, h, w, bend, headW, vr, vg, vb; // bend = sideways curve, headW = seed-head widen
            };

            for ( uint32_t c = 0; c < kForms; ++c )
            {
                std::vector<Blade> blades;
                const float        seed = static_cast<float>( c ) * 13.7f + 1.0f;

                if ( c == 0 ) // STRAIGHT — many fine upright blades
                {
                    for ( int i = 0; i < 30; ++i )
                    {
                        float fi = seed + i, br = rnd( fi * 5.9f );
                        blades.push_back( { 0.12f + 0.76f * rnd( fi * 1.7f ), 0.60f + 0.40f * rnd( fi * 3.1f ),
                                            0.012f + 0.016f * rnd( fi * 4.3f ), ( rnd( fi * 2.3f ) - 0.5f ) * 0.06f,
                                            0.0f, 0.80f + 0.5f * br, 0.95f + 0.1f * br, 0.60f + 0.3f * ( 1.0f - br ) } );
                    }
                }
                else if ( c == 1 ) // CURVED — blades arc to one side
                {
                    for ( int i = 0; i < 26; ++i )
                    {
                        float fi = seed + i, br = rnd( fi * 5.9f ), side = rnd( fi * 7.1f ) < 0.5f ? -1.0f : 1.0f;
                        blades.push_back( { 0.20f + 0.60f * rnd( fi * 1.7f ), 0.62f + 0.38f * rnd( fi * 3.1f ),
                                            0.013f + 0.018f * rnd( fi * 4.3f ),
                                            side * ( 0.18f + 0.22f * rnd( fi * 2.9f ) ), 0.0f, 0.80f + 0.5f * br,
                                            0.95f + 0.1f * br, 0.55f + 0.3f * ( 1.0f - br ) } );
                    }
                }
                else if ( c == 2 ) // STEM — a few tall thicker stalks with a small seed head
                {
                    for ( int i = 0; i < 7; ++i )
                    {
                        float fi = seed + i, br = rnd( fi * 5.9f );
                        blades.push_back( { 0.32f + 0.36f * rnd( fi * 1.7f ), 0.85f + 0.15f * rnd( fi * 3.1f ),
                                            0.022f + 0.012f * rnd( fi * 4.3f ), ( rnd( fi * 2.3f ) - 0.5f ) * 0.10f,
                                            0.030f + 0.020f * br, 0.90f + 0.3f * br, 0.90f + 0.1f * br,
                                            0.45f + 0.25f * ( 1.0f - br ) } );
                    }
                }
                else // BUSH — dense blades radiating from the base, rounder & shorter
                {
                    for ( int i = 0; i < 40; ++i )
                    {
                        float fi = seed + i, br = rnd( fi * 5.9f ), side = rnd( fi * 7.1f ) - 0.5f;
                        blades.push_back( { 0.50f + side * 0.42f, 0.40f + 0.45f * rnd( fi * 3.1f ),
                                            0.012f + 0.015f * rnd( fi * 4.3f ), side * ( 0.30f + 0.30f * rnd( fi * 2.9f ) ),
                                            0.0f, 0.70f + 0.4f * br, 0.85f + 0.12f * br, 0.50f + 0.3f * ( 1.0f - br ) } );
                    }
                }

                for ( uint32_t y = 0; y < H; ++y )
                {
                    float v = static_cast<float>( y ) / static_cast<float>( H - 1 ); // 0 = root, 1 = top
                    for ( uint32_t lx = 0; lx < CW; ++lx )
                    {
                        float        u     = static_cast<float>( lx ) / static_cast<float>( CW - 1 );
                        float        bestA = 0.0f, t = 0.0f;
                        const Blade* bb = nullptr;
                        for ( const auto& b : blades )
                        {
                            if ( v > b.h )
                                continue;
                            float vv = v / b.h;
                            float cx = b.x0 + b.bend * vv * vv;                       // sideways curve
                            float hw = b.w * ( 1.0f - 0.85f * vv ) + b.headW * ss( 0.6f, 1.0f, vv );
                            float a  = 1.0f - ss( hw * 0.55f, hw, std::fabs( u - cx ) );
                            if ( a > bestA )
                            {
                                bestA = a;
                                bb    = &b;
                                t     = vv;
                            }
                        }
                        size_t idx = ( static_cast<size_t>( y ) * W + ( c * CW + lx ) ) * 4;
                        if ( bestA > 0.02f && bb )
                        {
                            // Fresh-green gradient: G clearly dominates R so the field reads green (not
                            // olive/dry) under the warm scene sun. Root darker, tip brighter green.
                            float r     = ( 0.030f + 0.16f * t ) * bb->vr;
                            float g     = ( 0.10f + 0.40f * t ) * bb->vg;
                            float b     = ( 0.020f + 0.10f * t ) * bb->vb;
                            px[idx + 0] = static_cast<unsigned char>( std::clamp( r, 0.0f, 1.0f ) * 255.0f );
                            px[idx + 1] = static_cast<unsigned char>( std::clamp( g, 0.0f, 1.0f ) * 255.0f );
                            px[idx + 2] = static_cast<unsigned char>( std::clamp( b, 0.0f, 1.0f ) * 255.0f );
                            px[idx + 3] = static_cast<unsigned char>( std::clamp( bestA, 0.0f, 1.0f ) * 255.0f );
                        }
                    }
                }
            }

            ::Desert::Core::Formats::Image2DSpecification spec = {
                 .Tag          = "GrassClump",
                 .Width        = W,
                 .Height       = H,
                 .Format       = ::Desert::Core::Formats::ImageFormat::RGBA8F,
                 .Mips         = 1,
                 .Data         = px,
                 .Usage        = ::Desert::Core::Formats::Image2DUsage::Image2D,
                 .Properties   = ::Desert::Core::Formats::ImageProperties::Sample,
                 .GenerateMips = true, // full mip chain -> far blades filter instead of aliasing into bands
            };
            return Image2D::Create( spec, nullptr );
        }

    } // namespace

    Common::BoolResultStr TerrainRenderer::Initialize()
    {
        const auto shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Terrain" );
        if ( !shader )
            return Common::MakeError( "TerrainRenderer: missing shader 'Terrain'" );

        GraphicsPipelineSpecification spec;
        spec.DebugName   = "TerrainPipeline";
        spec.Shader      = shader;
        spec.Framebuffer = m_TargetFramebuffer.lock();
        spec.BlendEnable = false;

        // Render-state (patch-list topology + control points, cull, depth) is declared by the shader's
        // `#pragma state` — no longer hardcoded here. The pipeline comes from the shared cache.
        ApplyShaderRenderState( spec, shader->GetProgramMeta().State );

        m_Pipeline = m_SceneRenderer->GetPipelineCache().GetOrCreate( spec );
        if ( !m_Pipeline )
            return Common::MakeError( "TerrainRenderer: failed to create pipeline" );

        // Generic material built from the "Terrain" shader: engine fills TerrainUB (matrices) by name;
        // material params (Tint, DetailTiling) come from the entity's MaterialComponent overrides.
        m_Material = std::make_unique<DataDrivenMaterial>( "Terrain" );

        // GPU-instanced grass (Stage 7) — optional second pipeline. Absence of the shader just disables grass.
        if ( const auto grassShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Grass" ) )
        {
            GraphicsPipelineSpecification grassSpec;
            grassSpec.DebugName   = "GrassPipeline";
            grassSpec.Shader      = grassShader;
            grassSpec.Framebuffer = m_TargetFramebuffer.lock();
            grassSpec.BlendEnable = false;
            ApplyShaderRenderState( grassSpec, grassShader->GetProgramMeta().State );

            m_GrassPipeline = m_SceneRenderer->GetPipelineCache().GetOrCreate( grassSpec );
            if ( m_GrassPipeline )
            {
                m_GrassMaterial = std::make_unique<DataDrivenMaterial>( "Grass" );
                m_GrassClumpTex = BakeGrassClumpTexture(); // baked once, sampled per-fragment (cheap)

                // GPU cull compute (compacts visible clumps + writes the indirect draw count). Optional:
                // if the shader is missing we fall back gracefully (grass simply won't draw).
                if ( const auto cullShader =
                          Runtime::ResourceRegistry::GetShaderService()->GetByName( "GrassCull" ) )
                {
                    m_GrassCullPipeline = ComputePipeline::Create( { .Shader = cullShader, .DebugName = "GrassCull" } );
                    if ( m_GrassCullPipeline )
                        m_GrassCullPipeline->Invalidate();
                }
            }
        }
        return BOOLSUCCESS;
    }

    void TerrainRenderer::Shutdown()
    {
        m_Pipeline.reset();
        m_Material.reset();
        m_GrassPipeline.reset();
        m_GrassMaterial.reset();
        m_GrassClumpTex.reset();
        m_GrassCullPipeline.reset();
        m_GrassVisibleBuf.reset();
        m_GrassIndirectBuf.reset();
    }

    void TerrainRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb || !m_Pipeline )
            return;

        // Same Geometry phase + scene framebuffer as the meshes: merges into the open render pass
        // (depth shared, no clear) so terrain and meshes depth-resolve against each other.
        builder.AddPass(
             "TerrainPass", RenderPhase::Geometry,
             [this]()
             {
                 const auto* camera = m_SceneRenderer->GetMainCamera();
                 if ( !camera || m_Queue.empty() )
                     return;

                 // Max (near) tessellation level — the TCS scales each patch edge from this down to ~2 by
                 // view-space distance (Stage 4 LOD). The patch grid density is driven by the entity's
                 // Resolution, clamped to keep the patch count sane.
                 constexpr float    kTessLevel  = 16.0f;
                 constexpr uint32_t kMaxGridDim = 64u;

                 for ( const auto& t : m_Queue )
                 {
                     const uint32_t gridDim =
                          std::clamp<uint32_t>( static_cast<uint32_t>( t.Resolution ), 1u, kMaxGridDim );

                     // Engine data -> TerrainUB (binding 0), set wholesale by name.
                     TerrainUB ub{};
                     ub.View       = camera->GetViewMatrix();
                     ub.Projection = camera->GetProjectionMatrix();
                     ub.Model      = t.Transform;
                     ub.Params     = glm::vec4( t.Size, static_cast<float>( gridDim ), t.HeightScale, kTessLevel );
                     // .z = grass Brightness (GrassTint.x), so the lawn ground in Terrain.glsl.frag tracks
                     // the blade brightness from the single Grass Brightness slider.
                     ub.Params2 = glm::vec4( t.NoiseFrequency, static_cast<float>( t.Seed ), t.GrassTint.x, 0.0f );
                     ub.LayerModes = glm::vec4( t.LayerModes, t.GrassParams.x ); // .w = grass enabled (soil tint)
                     GetSun( m_SceneRenderer, ub.SunDir, ub.SunColor );
                     if ( auto* terrainUB = m_Material->Get<UniformBufferProperty>( "TerrainUB" ) )
                         terrainUB->SetRawData( reinterpret_cast<const std::byte*>( &ub ), sizeof( ub ) );

                     // The cloud layer's shadow on the sun this terrain is lit by — the SAME payload the
                     // deferred composite and the forward mesh materials receive, written by the same
                     // one writer. A terrain is drawn by neither render path's mesh shaders, so while
                     // the map's only reader was the deferred composite a terrain never darkened under a
                     // cloud at all: the ground beside it did and it did not.
                     CloudShadowBind( m_Material.get(), m_SceneRenderer->GetCloudShadowInput() );

                     // Material params: reset to #pragma defaults, then apply this entity's overrides by name.
                     m_Material->ApplyDefaults();
                     for ( const auto& [name, value] : t.Overrides.Params )
                         m_Material->SetParamRaw( name, value );

                     // Texture overrides: resolve asset handle -> runtime Image2D and bind by sampler name.
                     // Unset samplers keep the backend white fallback, so this is purely additive.
                     for ( const auto& [name, handle] : t.Overrides.Textures )
                     {
                         if ( handle == 0 )
                             continue;
                         auto* tex = Runtime::ResourceRegistry::GetTextureService()->Get( Common::UUID( handle ) );
                         if ( !tex )
                             continue;
                         auto* img = static_cast<Image2D*>(
                              Runtime::ResourceRegistry::GetImageService()->Resolve( tex->GetImageHandle() ) );
                         if ( img )
                             m_Material->SetTexture( name, img );
                     }

                     // Per-terrain painted splat map (Manual layers). Null -> white fallback stays bound.
                     if ( t.SplatMap )
                         m_Material->SetTexture( "u_SplatMap", t.SplatMap );

                     const uint32_t vertexCount = gridDim * gridDim * 4u; // patches * control points
                     Renderer::GetInstance().SubmitVertices( m_Pipeline.get(), vertexCount,
                                                             m_Material->GetMaterialExecutor() );
                 }
             },
             m_Pipeline->GetSpecification(), targetFb, { RenderPassDependency( RenderPhase::DepthPrePass ) } );

        // GPU-instanced grass — same Geometry phase + framebuffer (depth-tested against the terrain).
        if ( !m_GrassPipeline || !m_GrassMaterial )
            return;

        builder.AddPass(
             "GrassPass", RenderPhase::Geometry,
             [this]()
             {
                 const auto* camera = m_SceneRenderer->GetMainCamera();
                 if ( !camera || m_Queue.empty() || !m_GrassCullPipeline || !m_GrassVisibleBuf ||
                      !m_GrassIndirectBuf )
                     return;

                 // GPU-culled grass draws the FIRST grass-enabled terrain indirectly (instanceCount comes
                 // from the cull compute that ran before the render graph). CullGrassInFrame picks the same
                 // terrain, so the visible buffer + indirect args match this draw.
                 const TerrainDrawData* gt = nullptr;
                 for ( const auto& t : m_Queue )
                 {
                     if ( t.GrassParams.x >= 0.5f )
                     {
                         gt = &t;
                         break;
                     }
                 }
                 if ( !gt )
                     return;

                 const uint32_t grassGrid =
                      std::clamp<uint32_t>( static_cast<uint32_t>( gt->GrassParams.y ), 8u, 512u );

                 GrassUB ub{};
                 ub.View       = camera->GetViewMatrix();
                 ub.Projection = camera->GetProjectionMatrix();
                 ub.Model      = gt->Transform;
                 ub.Params     = glm::vec4( gt->Size, gt->GrassParams.w, gt->HeightScale, gt->NoiseFrequency );
                 ub.Params2    = glm::vec4( static_cast<float>( gt->Seed ), static_cast<float>( grassGrid ),
                                            gt->GrassParams.z, kGrassMaxDist );
                 // Shared scene wind (SceneSettings -> SceneRenderer::GetWind()): xy = normalized ground
                 // direction, z = strength, w = animation time. No longer a per-terrain hardcode — the same
                 // wind that will drive hair/cloth drives the grass, so the field moves coherently.
                 const auto& wind = m_SceneRenderer->GetWind();
                 ub.Wind          = glm::vec4( wind.Direction.x, wind.Direction.y, wind.Strength, wind.Time );
                 ub.CameraPos  = glm::vec4( camera->GetPosition(), 0.0f );
                 // Player character bends grass away as it walks through (xyz world pos, w radius). Sourced
                 // from SceneRenderer (set each BeginScene from the first CharacterController); 0 = no actor.
                 ub.Interactor = m_SceneRenderer->GetGrassInteractor();
                 GetSun( m_SceneRenderer, ub.SunDir, ub.SunColor );
                 // GrassTint channel packs (x=brightness, y=bladesPerClump). The shader wants a uniform
                 // brightness in rgb, so broadcast .x; blades only drives the CPU vertex count below.
                 ub.GrassTint = glm::vec4( glm::vec3( gt->GrassTint.x ), 0.0f );
                 if ( auto* gub = m_GrassMaterial->Get<UniformBufferProperty>( "GrassUB" ) )
                     gub->SetRawData( reinterpret_cast<const std::byte*>( &ub ), sizeof( ub ) );

                 // The blades take the SAME cloud shadow as the lawn under them. Terrain.shader and
                 // Grass.shader are authored to read as one material (the ground tint tracks the blade
                 // brightness from a single slider for exactly that reason), so shading one and not the
                 // other turns a field into bright fuzz over dark soil.
                 CloudShadowBind( m_GrassMaterial.get(), m_SceneRenderer->GetCloudShadowInput() );

                 m_GrassMaterial->ApplyDefaults();
                 if ( gt->SplatMap )
                     m_GrassMaterial->SetTexture( "u_SplatMap", gt->SplatMap );
                 if ( m_GrassClumpTex )
                     m_GrassMaterial->SetTexture( "u_GrassClump", m_GrassClumpTex.get() );

                 Renderer::GetInstance().SubmitVerticesIndirect( m_GrassPipeline.get(), m_GrassIndirectBuf.get(),
                                                                 m_GrassMaterial->GetMaterialExecutor() );
             },
             m_GrassPipeline->GetSpecification(), targetFb,
             { RenderPassDependency( RenderPhase::DepthPrePass ) } );
    }

    void TerrainRenderer::EnsureGrassCullBuffers( uint32_t maxInstances )
    {
        if ( !m_GrassIndirectBuf )
        {
            // Holds a single VkDrawIndirectCommand. Binding 2 is the compute-side slot (the draw uses the
            // raw VkBuffer, not a descriptor, so the binding only matters for the compute dispatch).
            m_GrassIndirectBuf =
                 ShaderResources::StorageBuffer::Create( "GrassIndirect", sizeof( GrassDrawIndirect ), 2 );
        }

        if ( !m_GrassVisibleBuf || m_GrassVisibleCapacity < maxInstances )
        {
            // Binding 3 = the grass GRAPHICS shader's GrassVisible slot (ApplyStorageBuffer binds by the
            // buffer's own GetBinding()); the compute dispatch rebinds it explicitly at slot 1.
            m_GrassVisibleBuf = ShaderResources::StorageBuffer::Create(
                 "GrassVisible", maxInstances * static_cast<uint32_t>( sizeof( uint32_t ) ), 3 );
            m_GrassVisibleCapacity = maxInstances;

            // Point the grass material's (reflection-created) storage buffer at OUR sized, compute-written
            // buffer so the vertex shader and the compute pass share the same VkBuffer.
            if ( m_GrassMaterial )
                if ( auto* sb = m_GrassMaterial->Get<StorageBufferProperty>( "GrassVisible" ) )
                    sb->SetBuffer( m_GrassVisibleBuf );
        }
    }

    void TerrainRenderer::CullGrassInFrame()
    {
        if ( !m_GrassCullPipeline || !m_GrassPipeline || !m_GrassMaterial || m_Queue.empty() )
            return;

        const auto* camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return;

        // Cull the FIRST grass-enabled terrain (the grass draw pass picks the same one).
        const TerrainDrawData* gt = nullptr;
        for ( const auto& t : m_Queue )
        {
            if ( t.GrassParams.x >= 0.5f )
            {
                gt = &t;
                break;
            }
        }
        if ( !gt )
            return;

        const uint32_t grid         = std::clamp<uint32_t>( static_cast<uint32_t>( gt->GrassParams.y ), 8u, 512u );
        const uint32_t maxInstances = grid * grid;
        EnsureGrassCullBuffers( maxInstances );

        // Reset the indirect args each frame: vertexCount = bladesPerClump * 24 geometric-blade verts
        // (blades packed into GrassTint.y by the ECS), instanceCount = 0 (the cull compute atomicAdds the
        // visible count). Host-mapped per-frame buffer -> visible before this dispatch.
        const uint32_t bladesPerClump =
             static_cast<uint32_t>( std::clamp( gt->GrassTint.y, 1.0f, 12.0f ) + 0.5f );
        GrassDrawIndirect args{ bladesPerClump * kGrassVertsPerBlade, 0u, 0u, 0u };
        m_GrassIndirectBuf->SetData( &args, sizeof( args ) );

        GrassCullPush push{};
        push.MVP =
             camera->GetProjectionMatrix() * camera->GetViewMatrix() * gt->Transform; // local -> clip
        push.Params  = glm::vec4( gt->Size, gt->SplatMap ? 1.0f : 0.0f, gt->HeightScale, gt->NoiseFrequency );
        push.Params2 = glm::vec4( static_cast<float>( gt->Seed ), static_cast<float>( grid ),
                                  gt->GrassParams.z, kGrassMaxDist );

        // The splat binding must always be valid; fall back to the baked clump texture when unpainted.
        Image2D* splat = gt->SplatMap ? gt->SplatMap : m_GrassClumpTex.get();
        m_GrassCullPipeline->SetInput( 0, splat );
        m_GrassCullPipeline->SetStorageBuffer( 1, m_GrassVisibleBuf.get() );
        m_GrassCullPipeline->SetStorageBuffer( 2, m_GrassIndirectBuf.get() );
        m_GrassCullPipeline->SetPushConstants( &push, sizeof( push ) );

        const uint32_t groups = ( maxInstances + 63u ) / 64u;
        Renderer::GetInstance().DispatchComputeCull( m_GrassCullPipeline.get(), groups, 1, 1 );
    }
} // namespace Desert::Graphic::System
