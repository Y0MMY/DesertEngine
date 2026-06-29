#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/Materials/PostProcessing/JumpFloodMaterials.hpp>
#include <Engine/Graphic/Materials/PostProcessing/MaterialJFAComposite.hpp>

namespace Desert::Graphic::System
{
    // Jump Flood Algorithm based object outline.
    //
    // Pipeline (all fullscreen passes, executed explicitly by SceneRenderer after the scene graph):
    //   Init   : silhouette mask        -> seed[0]
    //   Step xN: seed[i%2]              -> seed[(i+1)%2]   (ping-pong, halving step length)
    //   Final  : seed[N%2] + scene color -> output (outlined scene)
    //
    // The output framebuffer is what the tonemap stage consumes. When the outline is disabled the
    // Final pass simply passes the scene color through (see JFA_Final.glsl.frag).
    class JumpFloodOutlineRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;

        // JFA is driven explicitly (see Execute), not through the render graph.
        void RegisterPasses( RenderGraphBuilder& builder ) override
        {
        }

        // Runs the full Init -> Step xN -> Final chain for the current frame.
        void Execute();

        void OnResize( uint32_t width, uint32_t height );

        // The silhouette mask is produced by MeshRenderer; wired once after both systems init.
        void SetMaskFramebuffer( const std::weak_ptr<Framebuffer>& maskFramebuffer )
        {
            m_MaskFramebuffer = maskFramebuffer;
        }

        void SetOutlineColor( const glm::vec3& color )
        {
            m_OutlineColor = color;
        }
        void SetOutlineWidth( float width )
        {
            m_OutlineWidth = width;
        }
        void SetOutlineSmoothness( float smoothness )
        {
            m_Smoothness = smoothness;
        }
        void SetEnabled( bool enabled )
        {
            m_Enabled = enabled;
        }
        // When false (nothing selected) Execute() skips the ~log2(width) ping-pong STEP passes (it still
        // runs the cheap Init so seed[0] stays valid/sampleable for the composite). Safe: the framebuffer
        // render pass finalLayout is SHADER_READ_ONLY and EndRenderPass inserts a COLOR_WRITE->SHADER_READ
        // barrier, so Init's write of seed[0] is laid-out + visible for the composite's sample.
        void SetOutlineActive( bool active )
        {
            m_OutlineActive = active;
        }

    private:
        bool CreateFramebuffers( uint32_t width, uint32_t height );
        bool CreatePipelines();

        static uint32_t ComputeStepCount( uint32_t width, uint32_t height );

        void RunQuad( const std::shared_ptr<Framebuffer>& target, const std::string& debugName,
                      const GraphicsPipeline* pipeline, const MaterialExecutor* executor );

    private:
        // Ping-pong seed targets + final output (m_Framebuffer from RenderSystem base).
        std::shared_ptr<Framebuffer> m_SeedFramebuffers[2];

        std::shared_ptr<GraphicsPipeline> m_InitPipeline;
        std::shared_ptr<GraphicsPipeline> m_StepPipeline;
        std::shared_ptr<GraphicsPipeline> m_FinalPipeline;

        std::unique_ptr<MaterialJFAInit>                   m_MaterialInit;
        std::vector<std::unique_ptr<MaterialJFAStep>>      m_StepMaterials; // one per step (avoids descriptor aliasing)
        std::unique_ptr<MaterialJFAComposite>              m_MaterialComposite;

        std::weak_ptr<Framebuffer> m_MaskFramebuffer;

        uint32_t  m_StepCount    = 1;
        glm::vec3 m_OutlineColor = glm::vec3( 1.0f, 0.5f, 0.0f );
        float     m_OutlineWidth = 4.0f;
        float     m_Smoothness   = 2.0f;
        bool      m_Enabled       = true;
        bool      m_OutlineActive = false; // per-frame: is anything selected/outlined? (MeshRenderer::HasOutline)
    };
} // namespace Desert::Graphic::System
