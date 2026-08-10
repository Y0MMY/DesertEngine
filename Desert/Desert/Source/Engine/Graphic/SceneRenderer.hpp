#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/MaterialOverrides.hpp>
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/SpotLight.hpp>
#include <Engine/Graphic/AtmosphereEnv.hpp>
#include <Engine/Graphic/SkySettings.hpp>
#include <Engine/Graphic/WindEnv.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/PipelineCache.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Camera.hpp>

#include <Common/Core/Events/WindowEvents.hpp>
#include <Common/Core/EventRegistry.hpp>

#include "Systems/Scene/Mesh/MeshRenderer.hpp"
#include "Systems/Scene/Skybox/SkyboxRenderer.hpp"
#include "Systems/Scene/Terrain/TerrainRenderer.hpp"
#include "Systems/Scene/PostProcessing/TonemapRenderer.hpp"
#include "Systems/Scene/PostProcessing/JumpFloodOutlineRenderer.hpp"
#include "Systems/Scene/PostProcessing/FXAARenderer.hpp"
#include "Systems/Scene/PostProcessing/SMAARenderer.hpp"
#include "Systems/Scene/PostProcessing/BackdropBlurRenderer.hpp"
#include "Systems/Scene/PostProcessing/BloomRenderer.hpp"
#include "Systems/Scene/PostProcessing/AutoExposureRenderer.hpp"
#include "Systems/Scene/Deferred/DeferredLightingRenderer.hpp"
#include "Systems/Scene/Deferred/SSAORenderer.hpp"
#include "Systems/Scene/Deferred/CopyRenderer.hpp"
#include "Systems/Scene/Deferred/SSRRenderer.hpp"
#include "Systems/Scene/Deferred/GIResolveRenderer.hpp"
#include "Systems/Scene/Particles/ParticleRenderer.hpp"

#include <Engine/Core/SceneSettings.hpp>

#include <Engine/Graphic/IRenderSystem.hpp>
#include <Engine/Graphic/ExternalRenderPass.hpp>

namespace Desert::Core
{
    class Scene;
};

namespace Desert::Graphic
{
    class SceneRenderer final
    {
    public:
        struct UpdateInfo
        {
            Common::Timestep                Timestep;
            ShaderProtocols::DirectionLight DirLights;
        };

        struct RenderSubmissionExtra
        {
            std::vector<glm::mat4> BoneMatrices; // optional
            bool                   Outlined        = false;
            uint64_t               HiddenSubmeshes = 0;  // bit i = submesh i hidden (static meshes)
            int                    ForcedLOD       = -1; // -1 = auto (by distance)
            int                    LODBias         = 0;  // shifts the auto LOD (ignored when forced)
            bool                   CastShadows     = true;
            bool                   ReceiveShadows  = true;
        };

        // Each renderer claims a slot on construction — the index that says WHICH view is recording, so
        // per-frame state can eventually be stored per renderer instead of being overwritten by the next
        // one (EngineContext::GetActiveRendererSlot, Docs/RENDERER_FRAME_STATE.md). Slots are claimed in
        // creation order and never reused; past kMaxRendererSlots they fold back to 0, which is the
        // current behaviour for everyone anyway.
        SceneRenderer();
        // Releases the renderer slot, so closing a scene view hands it back instead of using it up.
        ~SceneRenderer();

        void Init();

        [[nodiscard]] Common::BoolResultStr BeginScene( const Desert::Core::Scene& scene );

        void OnUpdate( const UpdateInfo& sceneRenderInfo );

        [[nodiscard]] Common::BoolResultStr EndScene();

        void Resize( const uint32_t width, const uint32_t height );

        void SubmitMesh( const Mesh* mesh, const std::vector<MaterialInstance*>& materialSlots,
                         const glm::mat4& transform, const RenderSubmissionExtra& extra );

        // Submit one terrain entity for this frame (from TerrainECSSystem via DrawTerrainCommand).
        void SubmitTerrain( const glm::mat4& transform, float size, int resolution, float heightScale,
                            float noiseFrequency, int seed, const glm::vec3& layerModes = glm::vec3( 0.0f ),
                            Image2D* splatMap = nullptr, const glm::vec4& grassParams = glm::vec4( 0.0f ),
                            const glm::vec3&         grassTint = glm::vec3( 1.0f ),
                            const MaterialOverrides& overrides = {} );

        // Submit a mesh drawn with a generic data-driven material (MaterialComponent with a non-PBR shader).
        // directTexture (optional): a runtime-owned Image2D bound to `directTextureSampler`, for
        // procedural textures with no TextureAsset handle (the text SDF atlas).
        void SubmitGenericMesh( const Mesh* mesh, const glm::mat4& transform, const std::string& shaderName,
                                const MaterialOverrides& overrides, bool outlined = false,
                                Image2D* directTexture = nullptr, const std::string& directTextureSampler = {} );

        // v3 per-slot custom shaders: draw only @p visibleSubmeshMask submeshes of the mesh with the
        // slot's own runtime material (a MaterialService-owned DataDrivenMaterial).
        void SubmitSlotMaterialMesh( const Mesh* mesh, const glm::mat4& transform, Material* material,
                                     uint64_t visibleSubmeshMask, bool outlined = false );

        // UE-style Instanced Static Mesh: one mesh + one PBR material drawn for every transform in
        // @p transforms (a pointer to the component's stable per-frame array — not copied).
        void SubmitInstancedMesh( const Mesh* mesh, MaterialInstance* material,
                                  const std::vector<glm::mat4>* transforms );

        const Environment CreateEnvironment( const Common::Filepath& filepath );
        void SetEnvironment( const std::shared_ptr<MaterialSkybox>& material, float intensity = 1.0f );

        // Selection-outline (Jump Flood) appearance. Editor-only: pushed each frame from EditorPreferences
        // (the outline is a viewport visualization, not a scene property, so it does not live in SceneSettings).
        void SetOutlineSettings( const glm::vec3& color, float width, float smoothness, bool enabled );
        const std::optional<Environment>& GetEnvironment();

        // Procedural sky configuration (from the SkyAtmosphereComponent + the atmosphere sun, via the ECS).
        // sunDir is the direction TOWARD the sun, already normalized; bakeNow is the one-shot request from
        // the editor's Bake button.
        void SetProceduralSky( bool enabled, const glm::vec3& sunDir, bool bakeNow, const SkySettings& sky );

        // The evaluated per-frame sky: sun direction and radiance, ambient above/below, night factor, the
        // planet radius, and an OPAQUE handle to the packed sky-parameter buffer. This is the whole surface
        // the volumetric cloud pass consumes — it never sees the sky's authoring representation, so a
        // change to the palette cannot break it. Mirrors GetWind()/WindEnv.
        const AtmosphereEnv& GetAtmosphere() const;

        // How many SceneRenderers are alive right now. Every one of them pays for its own baked sky
        // environment, which is why the bake announces its cost with this number beside it.
        static uint32_t GetLiveRendererCount();

        const auto& GetMainCamera() const
        {
            return m_SceneInfo.ActiveCamera;
        }

        const auto& GetDirectionLights() const
        {
            return m_DirectionLights;
        }

        // Scene-global SHARED wind (authored in SceneSettings, refreshed each BeginScene). Renderers that
        // respond to wind (grass/foliage now; clouds/hair/cloth next) read it from here so one direction +
        // strength animate the whole world coherently.
        const WindEnv& GetWind() const
        {
            return m_Wind;
        }

        // Grass "interactor": a single actor (the player character) that bends grass away as it moves.
        // xyz = world position, w = influence radius in WORLD UNITS / centimetres (0 = disabled).
        // Refreshed each BeginScene.
        const glm::vec4& GetGrassInteractor() const
        {
            return m_GrassInteractor;
        }

        // CSM debug: the per-cascade shadow depth maps (for the editor's cascade viewer).
        std::shared_ptr<Image2D> GetShadowCascadeImage( uint32_t cascade );
        uint32_t                 GetShadowCascadeCount();

        const std::shared_ptr<Image2D>      GetFinalImage();
        const std::shared_ptr<Framebuffer>& GetTargetFramebuffer() const
        {
            return m_TargetFramebuffer;
        }

        // Deferred G-buffer (Albedo+Metallic / Normal+Roughness / depth). Populated only in the Deferred path.
        const std::shared_ptr<Framebuffer>& GetGBuffer() const
        {
            return m_GBuffer;
        }

        // Reflective Shadow Map (a G-buffer rendered from the sun) — the bounce source for GIMode::RSM.
        // Same attachment layout as the G-buffer, at a fixed light-space resolution.
        const std::shared_ptr<Framebuffer>& GetRSMBuffer() const
        {
            return m_RSMBuffer;
        }

        // Active rendering path, refreshed from SceneSettings each BeginScene.
        Core::RenderPath GetRenderPath() const
        {
            return m_RenderPath;
        }

        // Shared GraphicsPipeline cache (keyed by shader + target + render-state). Renderers request
        // pipelines from here instead of creating their own; cleared on Init (full rebuild).
        PipelineCache& GetPipelineCache()
        {
            return m_PipelineCache;
        }

        void RegisterRenderPass( RenderPhaseID phase, const std::string& name, std::function<void()> executeFunc,
                                 const GraphicsPipelineSpecification& pipeSpec = {} );

        // External (editor) pass injection: wraps the specification into an internal render system so
        // the pass participates in the normal graph build (phases, dependencies, pass merging).
        // Re-registering the same name replaces the previous pass; both rebuild the graph.
        void RegisterExternalPass( ExternalPassSpecification&& spec );
        void UnregisterExternalPass( const std::string& name );

        // True while the scene runs in Play mode (refreshed each BeginScene). External passes use this
        // to hide authoring aids during gameplay.
        bool IsScenePlaying() const
        {
            return m_ScenePlaying;
        }

        std::shared_ptr<Framebuffer> GetFramebufferForPhase( RenderPhaseID phase );
        std::shared_ptr<Texture>     GetTexture( const std::string& name );

        void RegisterRenderSystem( const std::string& name, std::shared_ptr<IRenderSystem> system );
        void UnregisterRenderSystem( const std::string& name );

        void RebuildRenderGraph();

        void AddPointLight( ShaderProtocols::PointLightPayload&& pointLight );

        const auto& GetPointLights() const
        {
            return m_PointLight;
        }

        void AddSpotLight( ShaderProtocols::SpotLightPayload&& spotLight );

        const auto& GetSpotLights() const
        {
            return m_SpotLight;
        }

    private:
        // Which view this renderer is; see the constructor.
        uint32_t m_RendererSlot = 0;

        void ClearMainFramebuffer();
        void CompositeRenderPass();
        void ExecuteRenderGraph();
        // Debug-phase passes (bounding boxes, colliders) drawn as a LOAD overlay AFTER the deferred
        // lighting composite — in Deferred the composite would otherwise paint lit meshes over any
        // debug lines recorded earlier in the graph, hiding them wherever geometry is present.
        void ExecuteDebugOverlay();
        // Transparency-phase passes (GPU particles, ...) drawn as a LOAD overlay AFTER the deferred
        // lighting composite, for the exact same reason as ExecuteDebugOverlay: recorded inside the
        // graph they land on the target BEFORE the composite and get painted over wherever geometry
        // exists (visible against sky, gone against the ground — the particle "top-down" bug).
        void ExecuteTransparency();
        // UI-phase passes (the Render2D canvas) drawn as a LOAD overlay AFTER the deferred lighting
        // composite — same reason as ExecuteTransparency/ExecuteDebugOverlay: recorded inside the graph
        // they land on the target BEFORE the composite (painted over) AND a CLEAR begin would wipe the
        // depth the grid/overlays load afterwards. Runs on top of the finished scene.
        void ExecuteUI();

    private:
        struct
        {
            Core::Camera* ActiveCamera;
        } m_SceneInfo;

        ShaderProtocols::DirectionLight m_DirectionLights;
        ShaderProtocols::PointLight     m_PointLight;
        ShaderProtocols::SpotLight      m_SpotLight;

        WindEnv m_Wind; // scene-global shared wind, refreshed from SceneSettings each BeginScene

        // Player-character grass interactor (xyz world pos, w radius), refreshed each BeginScene.
        glm::vec4 m_GrassInteractor{ 0.0f };

        // Selected post-process anti-aliasing technique, refreshed from SceneSettings each BeginScene.
        Core::AntiAliasingMode m_AAMode       = Core::AntiAliasingMode::FXAA;
        bool                   m_BloomEnabled = false;
        // Raised by the UI canvas when it drew glass; consumed at the top of the next frame's UI phase.
        bool                   m_BackdropBlurNeeded = false;
        bool                   m_ScenePlaying = false; // set per frame in BeginScene (hides authoring aids)

    public:
        // --- UI glass (backdrop blur) -----------------------------------------------------------
        // The UI canvas raises this when it recorded a glass element; the blur pyramid is then built
        // before the NEXT frame's UI phase. Latched per frame, so a canvas that stops using glass stops
        // paying for it.
        void SetBackdropBlurNeeded( bool needed )
        {
            m_BackdropBlurNeeded = needed;
        }

        // The blur pyramid glass samples, or null when it has never been built (the UI then falls back to
        // a flat tint). Mip 0 is a mild blur; higher LODs are blurrier — see BackdropBlurRenderer.
        const std::shared_ptr<Image2D>& GetBackdropBlurImage() const;
        uint32_t                        GetBackdropBlurMaxLod() const;

    private:
        std::shared_ptr<Framebuffer> m_TargetFramebuffer;
        std::shared_ptr<Framebuffer> m_GBuffer;                    // deferred G-buffer (MRT)
        std::shared_ptr<Framebuffer> m_SSAOBuffer;                 // deferred SSAO (AO factor)
        std::shared_ptr<Framebuffer> m_SceneColorCopy;             // scene snapshot for glass refraction
        std::shared_ptr<Framebuffer> m_SSRBuffer;                  // SSR trace target (denoised, then composited)
        std::shared_ptr<Framebuffer> m_GIBuffer;                   // RSM-GI resolve target (blur-read by lighting)
        std::shared_ptr<Framebuffer> m_RSMBuffer;                  // reflective shadow map (G-buffer from the sun)
        Core::RenderPath m_RenderPath = Core::RenderPath::Forward; // refreshed from SceneSettings each BeginScene
        Core::DeferredDebugMode m_DeferredDebug = Core::DeferredDebugMode::Off; // G-buffer debug view (deferred)
        bool                    m_EnableSSAO    = true; // deferred SSAO pass on/off (refreshed from SceneSettings)
        Core::GIMode            m_GIMode        = Core::GIMode::ScreenSpace; // indirect-light source
        float                   m_GIIntensity   = 2.0f;
        bool                    m_EnableSSR     = false;
        float                   m_SSRIntensity  = 1.0f;
        float                   m_SSRMaxDistance = 40.0f;

        // The RSM is a LOW-FREQUENCY input to a temporally-accumulated resolve, so it does not need to be
        // re-rendered every frame — refreshing it every 4th frame (and immediately when the sun moves) keeps
        // the GI stable while cutting the extra geometry pass to a quarter of its cost.
        static constexpr uint32_t kRSMResolution   = 512;
        static constexpr uint32_t kRSMRefreshEvery = 4;
        glm::vec3                 m_RSMLastSunDir{ 0.0f };
        uint32_t                  m_RSMFrameCounter = 0;

        // Allocate the SSR / RSM-GI targets + their systems on FIRST USE, not in the constructor: each
        // PreviewViewport (thumbnails, the Details mesh preview) owns a SceneRenderer, and a preview never
        // enables either feature — building them up front multiplied a lot of VRAM by the preview count.
        // Return false when the feature is unavailable; the failure is latched so it is not retried each frame.
        bool EnsureGIResources();
        bool EnsureSSRResources();
        // Device can sample+blend RGBA32F colour attachments — the precondition both features share.
        bool HasFloatRenderTargetSupport() const;
        bool m_GIResourcesReady   = false;
        bool m_GIResourcesFailed  = false;
        bool m_SSRResourcesReady  = false;
        bool m_SSRResourcesFailed = false;
        RenderGraphBuilder      m_RenderGraphBuilder;
        std::unordered_map<std::string, std::shared_ptr<IRenderSystem>> m_RenderSystems;

        // The names above, in the order they were first registered. RebuildRenderGraph walks THIS, not
        // the map: the map hands its systems out in hash-bucket order, so "the pass registered first"
        // meant "the pass whose system name happened to hash low", and it changed whenever a system was
        // added. The render graph tie-breaks equal passes inside a phase by registration order, so this
        // vector is what turns the order of the RegisterSystem calls in Init into the draw order.
        std::vector<std::string>                                        m_RenderSystemOrder;
        PipelineCache                                                   m_PipelineCache;

        // Registers a system under `name`, or replaces the system already registered under it while
        // keeping its original position in the registration order.
        void TrackRenderSystem( const std::string& name, std::shared_ptr<IRenderSystem> system );
        void ForgetRenderSystem( const std::string& name );

    private:
        template <typename System, typename... Args>
        void RegisterSystem( const std::string& system, Args&&... args )
        {
            TrackRenderSystem( system, std::make_shared<System>( std::forward<Args>( args )... ) );
        }
    };
} // namespace Desert::Graphic