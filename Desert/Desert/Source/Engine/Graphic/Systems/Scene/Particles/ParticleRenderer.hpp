#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Materials/Particles/MaterialParticleBillboard.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Graphic::System
{
    // GPU particle system. Per emitter: a PERSISTENT storage buffer of particle state that a compute shader
    // (ParticleSimulate) integrates + respawns each frame, drawn as camera-facing billboards (ParticleBillboard)
    // in the Transparency phase. Mirrors the grass GPU-cull flow: PrepareFrame snapshots emitters (CPU) in
    // BeginScene, SimulateInFrame dispatches the compute (command buffer active) BEFORE the render graph, and
    // the registered Transparency pass draws the result.
    class ParticleRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;
        ~ParticleRenderer() override; // defined in the .cpp so the unique_ptr<MaterialParticleBillboard> sees
                                      // the complete type

        Common::BoolResultStr Initialize() override;
        void                  Shutdown() override;
        void                  RegisterPasses( RenderGraphBuilder& builder ) override;

        // CPU snapshot of the scene's emitters (params, world position, per-frame spawn budget, zeroed spawn
        // counters). Call once per frame in BeginScene.
        void PrepareFrame( const ::Desert::Core::Scene& scene );

        // Record the per-emitter compute dispatches. Call in OnUpdate, outside any render pass, BEFORE the
        // render graph records the billboard draw (like grass CullGrassInFrame).
        void SimulateInFrame();

    private:
        // Push constant for ParticleSimulate (must match the shader's 128-byte block).
        struct SimPush
        {
            glm::vec4  EmitterPos; // xyz world pos, w dt
            glm::vec4  Gravity;    // xyz gravity, w time
            glm::vec4  Direction;  // xyz dir, w cone half-angle (rad)
            glm::vec4  Params;     // startSpeed, speedVar, lifetime, lifetimeVar
            glm::vec4  StartColor; // rgb + start alpha
            glm::vec4  EndColor;   // rgb + end alpha
            glm::vec4  Sizes;      // startSize, endSize, 0, 0
            glm::uvec4 Counts;     // maxParticles, spawnBudget, enabled, 0
        };

        // One emitter's persistent GPU state, cached across frames by entity id.
        struct EmitterGpu
        {
            std::shared_ptr<ShaderResources::StorageBuffer> Particles; // persistent particle state
            std::shared_ptr<ShaderResources::StorageBuffer> Counter;   // per-frame spawn counter
            int                                             MaxParticles = 0;
            float                                           SpawnAccum   = 0.0f; // fractional spawn carry
        };

        // This frame's active emitters (built by PrepareFrame, consumed by SimulateInFrame + the draw pass).
        struct FrameEmitter
        {
            EmitterGpu* Gpu = nullptr;
            SimPush     Push;
            bool        Additive = true;
        };

        bool        CreatePipelines();
        EmitterGpu& GetOrCreate( uint32_t entityId, int maxParticles );

        std::shared_ptr<ComputePipeline>           m_SimPipeline;
        std::shared_ptr<GraphicsPipeline>          m_AddPipeline;   // additive blend
        std::shared_ptr<GraphicsPipeline>          m_AlphaPipeline; // alpha blend
        std::unique_ptr<MaterialParticleBillboard> m_Material;

        std::unordered_map<uint32_t, EmitterGpu> m_Emitters;
        std::vector<FrameEmitter>                m_FrameEmitters;
        double                                   m_LastTime = 0.0;
    };
} // namespace Desert::Graphic::System
