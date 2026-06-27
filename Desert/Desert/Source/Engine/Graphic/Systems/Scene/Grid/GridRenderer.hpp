#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/Debug/MaterialGrid.hpp>

#include <memory>

namespace Desert::Graphic::System
{
    // Infinite editor ground-plane grid. Renders a fullscreen pass (after Geometry, into the shared scene
    // framebuffer so it's depth-occluded by meshes) that ray-marches y=0 and draws a distance-LOD grid.
    // Gated by SceneSettings.ShowGrid (forwarded via SetShowGrid) so a shipped game can disable it.
    class GridRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;
        void                          RegisterPasses( RenderGraphBuilder& builder ) override;

        void SetShowGrid( bool show )
        {
            m_ShowGrid = show;
        }

    private:
        std::shared_ptr<GraphicsPipeline> m_Pipeline;
        std::unique_ptr<MaterialGrid>     m_Material;
        bool                              m_ShowGrid = true;
    };
} // namespace Desert::Graphic::System
