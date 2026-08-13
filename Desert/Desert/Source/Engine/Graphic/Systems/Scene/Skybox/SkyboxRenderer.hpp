#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/AtmosphereEnv.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialProceduralSky.hpp>
#include <Engine/Graphic/SkyRules.hpp>
#include <Engine/Graphic/SkySettings.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

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
        void PrepareMaterial( const std::shared_ptr<MaterialSkybox>& material, float intensity = 1.0f );

        // When enabled, the Sky pass renders the engine-generated atmosphere instead of the HDR cubemap.
        // @p sunDir is the direction TOWARD the sun, normalized. @p bakeNow is the editor's one-shot
        // request. Everything else the sky needs — palette, sun radiance, planet radius, bake knobs — is
        // in @p sky, which MakeSkySettings produced from the component.
        // @p cloudLuminanceScale is the sun light's Cloud Scattered Luminance Scale — it lands on the
        // SunIrradiance the clouds consume, inside the same evaluation everything else reads.
        void SetProceduralSky( bool enabled, const glm::vec3& sunDir, bool bakeNow, const SkySettings& sky,
                               const glm::vec3& cloudLuminanceScale );

        // Bakes / rebakes the procedural-sky IBL when the rule says so (see ShouldRebakeSkyEnvironment
        // for WHETHER, SkyEnvironmentRebakeMayRun for WHEN). Call once per frame from a
        // frame-boundary-safe point (BEFORE the render graph records), NOT from inside a pass — the bake
        // idles the device. @p deltaSeconds drives the debounce that keeps a drag from baking on every
        // frame it crosses the angular threshold.
        void EnsureProceduralEnvironment( float deltaSeconds );

        const std::optional<Environment> GetEnvironment() const
        {
            // While the procedural sky is active, the baked atmosphere IBL drives ambient/reflections.
            if ( m_UseProceduralSky && m_ProceduralEnv )
                return m_ProceduralEnv;

            if ( const auto& material = m_MaterialSkybox.lock() )
            {
                return material->GetEnvironment();
            }
            return std::nullopt;
        }

        // The evaluated per-frame sky, for renderers that must agree with it (the volumetric clouds).
        // Valid == false when no enabled atmosphere is driving this frame.
        const AtmosphereEnv& GetAtmosphere() const
        {
            return m_Atmosphere;
        }

        void RegisterPasses( RenderGraphBuilder& builder ) override;

    private:
        void Render();

        // Writes the packed parameter block into the SSBO. One buffer serves the graphics pass and the
        // bake's compute dispatch, so both are guaranteed to describe the same sky.
        void UploadSkyParams();

    private:
        std::weak_ptr<MaterialSkybox> m_MaterialSkybox;

        Core::Camera*                     m_ActiveCamera    = nullptr;
        float                             m_SkyboxIntensity = 1.0f; // HDR-cubemap brightness
        std::shared_ptr<GraphicsPipeline> m_Pipeline;
        std::shared_ptr<Shader>           m_Shader;

        // Procedural sky (engine-generated atmosphere) — alternative to the HDR cubemap, same Sky pass.
        std::shared_ptr<GraphicsPipeline>      m_ProceduralPipeline;
        std::shared_ptr<Shader>                m_ProceduralShader;
        std::shared_ptr<MaterialProceduralSky> m_ProceduralMaterial;

        // The sky parameter block, created NON-PERSISTENT so the backend keeps one copy per
        // (frame in flight x renderer slot). A persistent buffer would be shared by every live
        // SceneRenderer by design, and the mesh preview would overwrite the viewport's sky.
        std::shared_ptr<ShaderResources::StorageBuffer> m_SkyParams;

        bool          m_UseProceduralSky = false;
        glm::vec3     m_SunDir           = glm::vec3( 0.0f, 1.0f, 0.0f ); // TOWARD the sun, normalized
        SkySettings   m_Sky;
        AtmosphereEnv m_Atmosphere;

        // Baked sky IBL (radiance/irradiance/prefiltered cubes) generated from the procedural atmosphere.
        Environment m_ProceduralEnv;
        glm::vec3   m_BakedSunDir = glm::vec3( 0.0f, 1.0f, 0.0f );
        bool        m_BakeRequested = false;

        // Debounce state (see SkyEnvironmentRebakeMayRun). m_SecondsSinceSunMoved is how long the sun has
        // held still, m_SecondsSinceStale how long a rebake has been wanted; the first collapses a drag
        // into one bake, the second stops a sun that never stops from deferring it forever.
        glm::vec3 m_LastSeenSunDir       = glm::vec3( 0.0f, 1.0f, 0.0f );
        float     m_SecondsSinceSunMoved = 0.0f;
        float     m_SecondsSinceStale    = 0.0f;

        // The High-resolution memory report is emitted once per renderer, not once per bake: with the
        // time-of-day driver a bake can happen every few seconds, and a cost that is announced every time
        // stops being read. Same reasoning for the below-horizon warning, which is a per-frame condition.
        bool m_HighResCostLogged  = false;
        bool m_BelowHorizonLogged = false;
    };
} // namespace Desert::Graphic::System
