#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Pipeline.hpp>

#include <glm/glm.hpp>

#include <memory>

namespace Desert::Graphic::System
{
    // Light shafts from the atmosphere sun — UE's "Light Shaft Bloom": a bright-pass mask of the HDR
    // scene colour around the sun's screen position, radially blurred toward it (Mitchell, GPU Gems 3
    // ch.13), added back in by the tonemap exactly the way bloom is. Occlusion comes for free: clouds
    // are composited into the scene colour with their real transmittance before this runs, so the
    // streaks exist only where the sun actually breaks through.
    //
    // The parameters are the SUN LIGHT's, not a scene setting: DirectionalLightData's Light Shafts
    // category (UE parity), carried here by the ProceduralSkyCommand alongside the sun direction —
    // shafts without a sun to cast them are not a thing.
    class LightShaftRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        struct Params
        {
            bool      Enabled       = false;
            float     BloomScale    = 0.2f;   // UE default
            float     Threshold     = 0.0f;   // UE default
            float     MaxBrightness = 100.0f; // UE default
            glm::vec3 BloomTint     = glm::vec3( 1.0f );
        };

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;

        void RegisterPasses( RenderGraphBuilder& builder ) override
        {
        }

        // @p sunScreenUv is the sun's position in [0,1] screen UV; @p screenFade is the CPU-computed
        // fade for a sun leaving the view (0 = fully off-screen or behind, dispatches are skipped).
        void Execute( const glm::vec2& sunScreenUv, float screenFade );
        void Resize( uint32_t width, uint32_t height );

        void SetParams( const Params& params )
        {
            m_Params = params;
        }

        const Params& GetParams() const
        {
            return m_Params;
        }

        // The blurred streak image the tonemap adds in — valid after Initialize. When the effect is off
        // this frame its contents are STALE and that is fine: the tonemap's shaft intensity is derived
        // from the same params and is zero in exactly those frames, the same contract the bloom image
        // has. The sun's screen position and edge fade are pure maths and live in
        // Engine/Graphic/PostProcessing/LightShaftRules.hpp, where the tests compile them.
        const std::shared_ptr<Image2D>& GetShaftImage() const
        {
            return m_ShaftImage;
        }

    private:
        bool CreateImages( uint32_t width, uint32_t height );
        bool CreatePipelines();

        static constexpr uint32_t kBlurPasses = 3;

        // Ping-pong pair at half resolution; m_ShaftImage aliases whichever held the last blur output.
        std::shared_ptr<Image2D> m_PingImage;
        std::shared_ptr<Image2D> m_PongImage;
        std::shared_ptr<Image2D> m_ShaftImage;

        std::shared_ptr<ComputePipeline> m_MaskPipeline;
        std::shared_ptr<ComputePipeline> m_BlurPipeline;

        Params m_Params;
    };
} // namespace Desert::Graphic::System
