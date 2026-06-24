#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>

#include <Engine/Graphic/Materials/PostProcessing/MaterialTonemap.hpp>

namespace Desert::Graphic::System
{
    class TonemapRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void               Shutdown() override {};

        // Tonemap runs as part of the explicit post-process chain (after the Jump Flood outline),
        // not through the render graph.
        void RegisterPasses( RenderGraphBuilder& builder ) override
        {
        }

        // Reads the configured source framebuffer and writes the tonemapped final image.
        void Execute();

        void Resize( uint32_t width, uint32_t height );

        // Tonemap parameters, refreshed from SceneSettings each frame (SceneRenderer::BeginScene).
        void SetParams( float exposure, float gamma )
        {
            m_Exposure = exposure;
            m_Gamma    = gamma;
        }

        // The bloom result framebuffer (set once after the bloom system is created) and its strength
        // (0 disables bloom). Tonemap samples it and adds it to the scene before tonemapping.
        void SetBloomFramebuffer( const std::shared_ptr<Framebuffer>& bloom )
        {
            m_BloomFramebuffer = bloom;
        }
        void SetBloomIntensity( float intensity )
        {
            m_BloomIntensity = intensity;
        }

        // Auto-exposure (eye adaptation): the 1x1 adapted-luminance buffer + key, set per-frame. When
        // disabled, the manual Exposure is used instead.
        void SetAutoExposureFramebuffer( const std::shared_ptr<Framebuffer>& fb )
        {
            m_AutoExposureFramebuffer = fb;
        }
        void SetAutoExposure( bool enabled, float key )
        {
            m_AutoExposureEnabled = enabled;
            m_ExposureKey         = key;
        }

    private:
        void Render();
    private:
        std::shared_ptr<GraphicsPipeline> m_Pipeline;
        std::shared_ptr<Shader>   m_Shader;

        std::unique_ptr<MaterialTonemap> m_MaterialTonemap;

        float m_Exposure = 1.0f;
        float m_Gamma    = 2.2f;

        std::weak_ptr<Framebuffer> m_BloomFramebuffer;
        float                      m_BloomIntensity = 0.0f;

        std::weak_ptr<Framebuffer> m_AutoExposureFramebuffer;
        bool                       m_AutoExposureEnabled = false;
        float                      m_ExposureKey         = 0.18f;
    };
} // namespace Desert::Graphic::System