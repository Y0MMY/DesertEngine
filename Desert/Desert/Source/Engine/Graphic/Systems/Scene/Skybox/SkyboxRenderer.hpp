#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialProceduralSky.hpp>
#include <Engine/Graphic/CloudSettings.hpp>
#include <Engine/Graphic/SkySettings.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic::System
{
    class SkyboxRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        void                          Shutdown()
        {
        }

        void PrepareCamera( Core::Camera* camera );
        void PrepareMaterial( const std::shared_ptr<MaterialSkybox>& material );

        // When enabled, the Sky pass renders the engine-generated procedural atmosphere instead of the
        // HDR cubemap. The sun direction is the directional light's (toward-sun) direction.
        void SetProceduralSky( bool enabled, const glm::vec3& sunDir, float sunIntensity, float sunDiskRadius,
                               bool bakeNow, const CloudSettings& clouds, const SkySettings& sky )
        {
            m_UseProceduralSky = enabled;
            m_SunDir           = sunDir;
            m_SunIntensity     = sunIntensity;
            m_SunDiskRadius    = sunDiskRadius;
            m_Clouds           = clouds;
            m_Sky              = sky;

            // Bake the sky IBL on FIRST enable (so the scene isn't unlit by default) or on an explicit
            // Bake request from the editor. Moving the sun no longer auto-rebakes — the user controls it
            // with the Bake button (baking is a heavy device-idle operation).
            if ( enabled && ( bakeNow || !m_ProceduralEnv ) )
                m_EnvDirty = true;
        }

        // Bakes / rebakes the procedural-sky IBL when needed (throttled). Call once per frame from a
        // frame-boundary-safe point (BEFORE the render graph records), NOT from inside a pass.
        void EnsureProceduralEnvironment();

        const std::optional<Environment> GetEnvironment() const
        {
            // While procedural sky is active, the baked atmosphere IBL drives ambient/reflections.
            if ( m_UseProceduralSky && m_ProceduralEnv )
                return m_ProceduralEnv;

            if ( const auto& material = m_MaterialSkybox.lock() )
            {
                return material->GetEnvironment();
            }
            return std::nullopt;
        }

        void RegisterPasses( RenderGraphBuilder& builder ) override;

    private:
        void Render();

    private:
        std::weak_ptr<MaterialSkybox> m_MaterialSkybox;

        Core::Camera*             m_ActiveCamera = nullptr;
        std::shared_ptr<GraphicsPipeline> m_Pipeline;
        std::shared_ptr<Shader>   m_Shader;

        // Procedural sky (engine-generated atmosphere) — alternative to the HDR cubemap, same Sky pass.
        std::shared_ptr<GraphicsPipeline>      m_ProceduralPipeline;
        std::shared_ptr<Shader>                m_ProceduralShader;
        std::shared_ptr<MaterialProceduralSky> m_ProceduralMaterial;
        bool      m_UseProceduralSky = false;
        glm::vec3 m_SunDir           = glm::vec3( 0.0f, 1.0f, 0.0f );
        float     m_SunIntensity     = 22.0f;
        float     m_SunDiskRadius    = 0.02f;
        CloudSettings m_Clouds;
        SkySettings   m_Sky;

        // Baked sky IBL (radiance/irradiance/prefiltered cubes) generated from the procedural atmosphere.
        Environment m_ProceduralEnv;
        glm::vec3   m_BakedSunDir = glm::vec3( 0.0f, 1.0f, 0.0f );
        bool        m_EnvDirty    = false;
    };
} // namespace Desert::Graphic::System