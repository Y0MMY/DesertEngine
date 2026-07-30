#include "ParticleRenderer.hpp"

#include <Engine/Graphic/Materials/Particles/MaterialParticleBillboard.hpp>
#include <Engine/Graphic/RenderPhase.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Components.hpp>

#include <Common/Core/Logger.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace Desert::Graphic::System
{
    namespace
    {
        constexpr uint32_t kParticleStride = 64; // sizeof( 4 * vec4 ) — must match the shader's Particle
        constexpr uint32_t kLocalSize      = 64; // ParticleSimulate LocalSize.x

        double NowSeconds()
        {
            static const auto start = std::chrono::steady_clock::now();
            return std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
        }
    } // namespace

    ParticleRenderer::~ParticleRenderer() = default;

    Common::BoolResultStr ParticleRenderer::Initialize()
    {
        if ( !CreatePipelines() )
            return Common::MakeError( "ParticleRenderer: failed to create pipelines (missing shaders?)" );
        m_LastTime = NowSeconds();
        return BOOLSUCCESS;
    }

    void ParticleRenderer::Shutdown()
    {
        m_Emitters.clear();
        m_FrameEmitters.clear();
        m_SimPipeline.reset();
        m_AddPipeline.reset();
        m_AlphaPipeline.reset();
        m_Material.reset();
    }

    bool ParticleRenderer::CreatePipelines()
    {
        auto shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return false;

        auto simShader  = shaderService->GetByName( "ParticleSimulate" );
        auto billShader = shaderService->GetByName( "ParticleBillboard" );
        if ( !simShader || !billShader )
        {
            LOG_ERROR( "ParticleRenderer: missing Particle shaders" );
            return false;
        }

        m_SimPipeline = ComputePipeline::Create( { .Shader = simShader, .DebugName = "ParticleSimulate" } );
        m_SimPipeline->Invalidate();

        const auto& target = m_TargetFramebuffer.lock();
        if ( !target )
            return false;

        GraphicsPipelineSpecification base;
        base.Shader            = billShader;
        base.Framebuffer       = target;
        base.DepthTestEnabled  = true;  // occlude behind opaque geometry
        base.DepthWriteEnabled = false; // transparent: never write depth
        base.DepthCompareOp    = CompareOp::LessOrEqual;
        base.CullMode          = CullMode::None;
        base.Topology          = PrimitiveTopology::Triangles;
        base.BlendEnable       = true;

        GraphicsPipelineSpecification addSpec = base;
        addSpec.DebugName                     = "ParticleAdd";
        addSpec.SrcColorBlendFactor           = BlendFactor::SrcAlpha;
        addSpec.DstColorBlendFactor           = BlendFactor::One; // additive glow
        m_AddPipeline                         = GraphicsPipeline::Create( addSpec );
        m_AddPipeline->Invalidate();

        GraphicsPipelineSpecification alphaSpec = base;
        alphaSpec.DebugName                     = "ParticleAlpha";
        alphaSpec.SrcColorBlendFactor           = BlendFactor::SrcAlpha;
        alphaSpec.DstColorBlendFactor           = BlendFactor::OneMinusSrcAlpha; // soft over
        m_AlphaPipeline                         = GraphicsPipeline::Create( alphaSpec );
        m_AlphaPipeline->Invalidate();

        m_Material = std::make_unique<MaterialParticleBillboard>();

        return m_SimPipeline && m_AddPipeline && m_AlphaPipeline;
    }

    ParticleRenderer::EmitterGpu& ParticleRenderer::GetOrCreate( uint32_t entityId, int maxParticles )
    {
        auto&     e   = m_Emitters[entityId];
        const int cap = std::max( 1, maxParticles );
        if ( e.MaxParticles != cap || !e.Particles )
        {
            e.MaxParticles = cap;
            e.Particles    = ShaderResources::StorageBuffer::Create(
                 "ParticleState", static_cast<uint32_t>( cap ) * kParticleStride, 0, /*persistent=*/true );
            e.Counter    = ShaderResources::StorageBuffer::Create( "ParticleSpawn", sizeof( uint32_t ), 1 );
            e.SpawnAccum = 0.0f;

            // All particles start dead (VelLife.w = 0, Color.a = 0): a zeroed buffer, so compute respawns them.
            std::vector<uint8_t> zeros( static_cast<size_t>( cap ) * kParticleStride, 0 );
            e.Particles->SetData( zeros.data(), static_cast<uint32_t>( zeros.size() ) );
        }
        return e;
    }

    void ParticleRenderer::PrepareFrame( const ::Desert::Core::Scene& scene )
    {
        m_FrameEmitters.clear();

        const double now = NowSeconds();
        float        dt  = static_cast<float>( now - m_LastTime );
        m_LastTime       = now;
        dt               = std::clamp( dt, 0.0f, 0.1f ); // guard against pauses / first frame

        const float time = static_cast<float>( now );

        auto& reg  = const_cast<entt::registry&>( scene.GetRegistry() );
        auto  view = reg.view<ECS::ParticleEmitterComponent, ECS::TransformComponent>();
        view.each(
             [&]( entt::entity entity, ECS::ParticleEmitterComponent& emitter, ECS::TransformComponent& transform )
             {
                 const auto& d = emitter.Data;
                 if ( !d.Enabled || d.MaxParticles <= 0 )
                     return;

                 EmitterGpu& gpu = GetOrCreate( static_cast<uint32_t>( entity ), d.MaxParticles );

                 // Spawn budget for this frame (fractional carry so low rates still emit).
                 gpu.SpawnAccum += d.SpawnRate * dt;
                 uint32_t budget = static_cast<uint32_t>( gpu.SpawnAccum );
                 gpu.SpawnAccum -= static_cast<float>( budget );
                 if ( !d.Looping )
                     budget = 0; // one-shot bursts are a follow-up; looping emits continuously

                 // Zero the spawn counter for this frame.
                 const uint32_t zero = 0;
                 gpu.Counter->SetData( &zero, sizeof( zero ) );

                 const glm::vec3 worldPos = glm::vec3( transform.GetTransform()[3] );
                 glm::vec3       dir      = d.Direction;
                 if ( glm::dot( dir, dir ) < 1e-6f )
                     dir = glm::vec3( 0.0f, 1.0f, 0.0f );
                 dir = glm::normalize( dir );

                 FrameEmitter fe;
                 fe.Gpu             = &gpu;
                 fe.Additive        = ( d.Blend == ECS::ParticleBlendMode::Additive );
                 fe.Push.EmitterPos = glm::vec4( worldPos, dt );
                 fe.Push.Gravity    = glm::vec4( d.Gravity, time );
                 fe.Push.Direction  = glm::vec4( dir, glm::radians( d.ConeAngle ) );
                 fe.Push.Params     = glm::vec4( d.StartSpeed, d.SpeedVariance, d.Lifetime, d.LifetimeVariance );
                 fe.Push.StartColor = glm::vec4( d.StartColor, d.StartAlpha );
                 fe.Push.EndColor   = glm::vec4( d.EndColor, d.EndAlpha );
                 fe.Push.Sizes      = glm::vec4( d.StartSize, d.EndSize, 0.0f, 0.0f );
                 fe.Push.Counts     = glm::uvec4( static_cast<uint32_t>( gpu.MaxParticles ), budget, 1u, 0u );

                 m_FrameEmitters.push_back( fe );
             } );
    }

    void ParticleRenderer::SimulateInFrame()
    {
        if ( !m_SimPipeline || m_FrameEmitters.empty() )
            return;

        auto& renderer = Renderer::GetInstance();
        for ( auto& fe : m_FrameEmitters )
        {
            m_SimPipeline->SetStorageBuffer( 0, fe.Gpu->Particles.get() );
            m_SimPipeline->SetStorageBuffer( 1, fe.Gpu->Counter.get() );
            m_SimPipeline->SetPushConstants( &fe.Push, sizeof( fe.Push ) );

            const uint32_t groups =
                 ( static_cast<uint32_t>( fe.Gpu->MaxParticles ) + kLocalSize - 1 ) / kLocalSize;
            // DispatchComputeCull (not InFrame): its barrier makes the writes visible to the VERTEX stage that
            // the billboard shader reads the particle buffer from.
            renderer.DispatchComputeCull( m_SimPipeline.get(), groups, 1, 1 );
        }
    }

    void ParticleRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb || !m_AddPipeline )
            return;

        builder.AddPass(
             "ParticlePass", RenderPhase::Transparency,
             [this]()
             {
                 if ( m_FrameEmitters.empty() )
                     return;
                 const auto camera = m_SceneRenderer->GetMainCamera();
                 if ( !camera )
                     return;

                 auto& renderer = Renderer::GetInstance();
                 for ( auto& fe : m_FrameEmitters )
                 {
                     if ( !fe.Gpu || !fe.Gpu->Particles )
                         continue;
                     m_Material->Update( camera, fe.Gpu->Particles );
                     auto* pipeline = fe.Additive ? m_AddPipeline.get() : m_AlphaPipeline.get();
                     renderer.SubmitVertices( pipeline, static_cast<uint32_t>( fe.Gpu->MaxParticles ) * 6u,
                                              m_Material->GetMaterialExecutor() );
                 }
             },
             m_AddPipeline->GetSpecification(), targetFb, { RenderPassDependency( RenderPhase::Geometry ) } );
    }
} // namespace Desert::Graphic::System
