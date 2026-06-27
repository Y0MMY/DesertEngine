#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/DataDrivenMaterial.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Desert::Graphic::System
{
    // One terrain entity's render parameters, submitted per-frame from the ECS (TerrainECSSystem ->
    // SceneRenderer::SubmitTerrain). Mirrors the ECS TerrainData but stays Graphic-side (no ECS dep).
    struct TerrainDrawData
    {
        glm::mat4 Transform      = glm::mat4( 1.0f );
        float     Size           = 50.0f;
        int       Resolution     = 64;    // patch grid subdivisions per side (clamped at draw time)
        float     HeightScale    = 5.0f;  // Stage 2 (displacement)
        float     NoiseFrequency = 0.08f; // Stage 2
        int       Seed           = 1337;  // Stage 2

        // Per-layer splat mode (grass, rock, snow): 0 = Auto (rules), 1 = Manual (splat map), 2 = Off.
        glm::vec3 LayerModes = glm::vec3( 0.0f );

        // Per-terrain splat map (R=grass,G=rock,B=snow weights), painted by the editor brush. Non-owning;
        // null => the white fallback is used (Manual layers show everywhere until painted).
        Image2D* SplatMap = nullptr;

        // GPU-instanced grass (Stage 7): x = enable (>0.5), y = density (blades/side), z = blade height,
        // w = width scale.
        glm::vec4 GrassParams = glm::vec4( 0.0f );
        glm::vec3 GrassTint   = glm::vec3( 1.0f ); // RGB tint multiplier on the grass color

        // Material param overrides from the entity's MaterialComponent (name -> value), applied generically
        // to the DataDrivenMaterial by name (e.g. "Tint", "DetailTiling").
        std::vector<std::pair<std::string, glm::vec4>> ParamOverrides;

        // Texture overrides (sampler name -> asset handle); resolved to Image2D and bound by name. Unset
        // samplers keep the backend white fallback. Drives the splat layers (u_GrassTex/u_RockTex/...).
        std::vector<std::pair<std::string, uint64_t>> TextureOverrides;
    };

    // GPU terrain renderer. Draws a tessellated patch grid into the scene framebuffer's Geometry phase
    // (vertexless patch-list draw -> TCS LOD -> TES displacement). Driven by the ECS: each TerrainComponent
    // entity is submitted as a TerrainDrawData every frame. Stage 1 keeps the surface flat (validates the
    // tessellation pipeline); later stages add compute-heightmap displacement + PBR shading.
    class TerrainRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;
        void                          RegisterPasses( RenderGraphBuilder& builder ) override;

        void Submit( const TerrainDrawData& data )
        {
            m_Queue.push_back( data );
        }

        void ClearQueue()
        {
            m_Queue.clear();
        }

        // GPU grass culling: dispatch the cull compute (compacts visible clumps + writes the indirect
        // draw count) into the frame command buffer BEFORE the render graph records the grass draw. Must
        // run outside any render pass (SceneRenderer calls it after ClearMainFramebuffer).
        void CullGrassInFrame();

    private:
        // Lazily (re)create the visible-instance + indirect-args buffers when the grid grows; injects the
        // visible buffer into the grass material so the vertex shader reads the SAME compute-written SSBO.
        void EnsureGrassCullBuffers( uint32_t maxInstances );

    private:
        std::shared_ptr<GraphicsPipeline>  m_Pipeline;
        std::unique_ptr<DataDrivenMaterial> m_Material;
        std::vector<TerrainDrawData>        m_Queue;

        // GPU-instanced grass (Stage 7): a second pipeline/material drawn in the same Geometry phase.
        std::shared_ptr<GraphicsPipeline>   m_GrassPipeline;
        std::unique_ptr<DataDrivenMaterial> m_GrassMaterial;
        // Baked grass-clump alpha texture: the fragment samples this ONCE instead of looping over blades
        // per pixel — keeps FPS high regardless of blade detail.
        std::shared_ptr<Image2D>            m_GrassClumpTex;

        // GPU grass culling (compacts visible clumps -> indirect instanced draw). Single grass terrain:
        // one visible-instance buffer + one indirect-args buffer, reused each frame.
        std::shared_ptr<ComputePipeline>                m_GrassCullPipeline;
        std::shared_ptr<ShaderResources::StorageBuffer> m_GrassVisibleBuf;  // compacted visible clump ids
        std::shared_ptr<ShaderResources::StorageBuffer> m_GrassIndirectBuf; // VkDrawIndirectCommand
        uint32_t                                        m_GrassVisibleCapacity = 0; // in clumps
    };
} // namespace Desert::Graphic::System
