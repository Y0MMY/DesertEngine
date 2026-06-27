#include "TerrainRenderer.hpp"

#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Core/Formats/ImageFormat.hpp>

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

        // CPU-bake a grass-clump alpha texture ONCE (many fine tapered blades + baked root->tip color +
        // per-blade variation). The grass fragment samples this once instead of looping over blades per
        // pixel — removes the per-fragment cost so FPS stays high regardless of blade detail.
        std::shared_ptr<Image2D> BakeGrassClumpTexture()
        {
            const uint32_t             W = 128, H = 256;
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
                float x, h, w, vr, vg, vb;
            };
            std::vector<Blade> blades;
            const int          NB = 36; // sparse thin blades -> lots of transparency between them (fine grass)
            for ( int i = 0; i < NB; ++i )
            {
                float fi = static_cast<float>( i );
                float br = rnd( fi * 5.9f );
                blades.push_back( { rnd( fi * 1.7f ), 0.55f + 0.45f * rnd( fi * 3.1f ),
                                    0.010f + 0.016f * rnd( fi * 4.3f ), 0.80f + 0.5f * br, 0.95f + 0.1f * br,
                                    0.60f + 0.3f * ( 1.0f - br ) } );
            }

            for ( uint32_t y = 0; y < H; ++y )
            {
                float v = static_cast<float>( y ) / static_cast<float>( H - 1 ); // 0 = root, 1 = top
                for ( uint32_t x = 0; x < W; ++x )
                {
                    float        u     = static_cast<float>( x ) / static_cast<float>( W - 1 );
                    float        bestA = 0.0f, t = 0.0f;
                    const Blade* bb = nullptr;
                    for ( const auto& b : blades )
                    {
                        if ( v > b.h )
                            continue;
                        float hw = b.w * ( 1.0f - v / b.h );
                        float a  = 1.0f - ss( hw * 0.55f, hw, std::fabs( u - b.x ) );
                        if ( a > bestA )
                        {
                            bestA = a;
                            bb    = &b;
                            t     = v / b.h;
                        }
                    }
                    size_t idx = ( static_cast<size_t>( y ) * W + x ) * 4;
                    if ( bestA > 0.02f && bb )
                    {
                        float r     = ( 0.06f + 0.28f * t ) * bb->vr;
                        float g     = ( 0.09f + 0.31f * t ) * bb->vg;
                        float b     = ( 0.03f + 0.15f * t ) * bb->vb;
                        px[idx + 0] = static_cast<unsigned char>( std::clamp( r, 0.0f, 1.0f ) * 255.0f );
                        px[idx + 1] = static_cast<unsigned char>( std::clamp( g, 0.0f, 1.0f ) * 255.0f );
                        px[idx + 2] = static_cast<unsigned char>( std::clamp( b, 0.0f, 1.0f ) * 255.0f );
                        px[idx + 3] = static_cast<unsigned char>( std::clamp( bestA, 0.0f, 1.0f ) * 255.0f );
                    }
                }
            }

            ::Desert::Core::Formats::Image2DSpecification spec = {
                 .Tag        = "GrassClump",
                 .Width      = W,
                 .Height     = H,
                 .Format     = ::Desert::Core::Formats::ImageFormat::RGBA8F,
                 .Mips       = 1,
                 .Data       = px,
                 .Usage      = ::Desert::Core::Formats::Image2DUsage::Image2D,
                 .Properties = ::Desert::Core::Formats::ImageProperties::Sample,
            };
            return Image2D::Create( spec, nullptr );
        }

        // Seconds since first frame — drives the grass wind sway (a future global WindComponent replaces it).
        float GrassTime()
        {
            static const auto start = std::chrono::steady_clock::now();
            return std::chrono::duration<float>( std::chrono::steady_clock::now() - start ).count();
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
                     ub.Params2    = glm::vec4( t.NoiseFrequency, static_cast<float>( t.Seed ), 0.0f, 0.0f );
                     ub.LayerModes = glm::vec4( t.LayerModes, t.GrassParams.x ); // .w = grass enabled (soil tint)
                     GetSun( m_SceneRenderer, ub.SunDir, ub.SunColor );
                     if ( auto* terrainUB = m_Material->Get<UniformBufferProperty>( "TerrainUB" ) )
                         terrainUB->SetRawData( reinterpret_cast<const std::byte*>( &ub ), sizeof( ub ) );

                     // Material params: reset to #pragma defaults, then apply this entity's overrides by name.
                     m_Material->ApplyDefaults();
                     for ( const auto& [name, value] : t.ParamOverrides )
                         m_Material->SetParamRaw( name, value );

                     // Texture overrides: resolve asset handle -> runtime Image2D and bind by sampler name.
                     // Unset samplers keep the backend white fallback, so this is purely additive.
                     for ( const auto& [name, handle] : t.TextureOverrides )
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
                 if ( !camera || m_Queue.empty() )
                     return;

                 constexpr uint32_t kVertsPerInstance = 3u * 6u; // kCards(3) * 6 (cross-card grass clump)

                 for ( const auto& t : m_Queue )
                 {
                     if ( t.GrassParams.x < 0.5f )
                         continue; // grass disabled for this terrain

                     const uint32_t grassGrid =
                          std::clamp<uint32_t>( static_cast<uint32_t>( t.GrassParams.y ), 8u, 512u );

                     GrassUB ub{};
                     ub.View       = camera->GetViewMatrix();
                     ub.Projection = camera->GetProjectionMatrix();
                     ub.Model      = t.Transform;
                     ub.Params     = glm::vec4( t.Size, t.GrassParams.w, t.HeightScale, t.NoiseFrequency );
                     ub.Params2    = glm::vec4( static_cast<float>( t.Seed ), static_cast<float>( grassGrid ),
                                                t.GrassParams.z, 45.0f ); // w = grass draw distance (FPS)
                     ub.Wind       = glm::vec4( 1.0f, 0.35f, 0.15f, GrassTime() );
                     ub.CameraPos  = glm::vec4( camera->GetPosition(), 0.0f );
                     ub.Interactor = glm::vec4( 0.0f ); // actor trample hook (future global wind/interaction)
                     GetSun( m_SceneRenderer, ub.SunDir, ub.SunColor );
                     ub.GrassTint = glm::vec4( t.GrassTint, 0.0f );
                     if ( auto* gub = m_GrassMaterial->Get<UniformBufferProperty>( "GrassUB" ) )
                         gub->SetRawData( reinterpret_cast<const std::byte*>( &ub ), sizeof( ub ) );

                     m_GrassMaterial->ApplyDefaults();
                     if ( t.SplatMap )
                         m_GrassMaterial->SetTexture( "u_SplatMap", t.SplatMap );
                     if ( m_GrassClumpTex )
                         m_GrassMaterial->SetTexture( "u_GrassClump", m_GrassClumpTex.get() );

                     const uint32_t instanceCount = grassGrid * grassGrid;
                     Renderer::GetInstance().SubmitVertices( m_GrassPipeline.get(), kVertsPerInstance,
                                                             m_GrassMaterial->GetMaterialExecutor(),
                                                             instanceCount );
                 }
             },
             m_GrassPipeline->GetSpecification(), targetFb,
             { RenderPassDependency( RenderPhase::DepthPrePass ) } );
    }
} // namespace Desert::Graphic::System
