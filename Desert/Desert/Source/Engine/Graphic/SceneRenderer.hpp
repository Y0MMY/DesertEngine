#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/MaterialOverrides.hpp>
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/SpotLight.hpp>
#include <Engine/Graphic/CloudSettings.hpp>
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
#include "Systems/Scene/Grid/GridRenderer.hpp"
#include "Systems/Scene/Terrain/TerrainRenderer.hpp"
#include "Systems/Scene/PostProcessing/TonemapRenderer.hpp"
#include "Systems/Scene/PostProcessing/JumpFloodOutlineRenderer.hpp"
#include "Systems/Scene/PostProcessing/FXAARenderer.hpp"
#include "Systems/Scene/PostProcessing/SMAARenderer.hpp"
#include "Systems/Scene/PostProcessing/BloomRenderer.hpp"
#include "Systems/Scene/PostProcessing/AutoExposureRenderer.hpp"
#include "Systems/Scene/Deferred/DeferredLightingRenderer.hpp"
#include "Systems/Scene/Deferred/SSAORenderer.hpp"
#include "Systems/Scene/Deferred/CopyRenderer.hpp"

#include <Engine/Core/SceneSettings.hpp>

#include <Engine/Graphic/IRenderSystem.hpp>

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
            uint64_t               HiddenSubmeshes = 0; // bit i = submesh i hidden (static meshes)
        };

        ~SceneRenderer() = default;

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
                            const glm::vec3&          grassTint = glm::vec3( 1.0f ),
                            const MaterialOverrides& overrides = {} );

        // Submit a mesh drawn with a generic data-driven material (MaterialComponent with a non-PBR shader).
        void SubmitGenericMesh( const Mesh* mesh, const glm::mat4& transform, const std::string& shaderName,
                                const MaterialOverrides& overrides, bool outlined = false );

        // v3 per-slot custom shaders: draw only @p visibleSubmeshMask submeshes of the mesh with the
        // slot's own runtime material (a MaterialService-owned DataDrivenMaterial).
        void SubmitSlotMaterialMesh( const Mesh* mesh, const glm::mat4& transform, Material* material,
                                     uint64_t visibleSubmeshMask, bool outlined = false );

        // UE-style Instanced Static Mesh: one mesh + one PBR material drawn for every transform in
        // @p transforms (a pointer to the component's stable per-frame array — not copied).
        void SubmitInstancedMesh( const Mesh* mesh, MaterialInstance* material,
                                  const std::vector<glm::mat4>* transforms );

        const Environment                 CreateEnvironment( const Common::Filepath& filepath );
        void                              SetEnvironment( const std::shared_ptr<MaterialSkybox>& material );
        const std::optional<Environment>& GetEnvironment();

        // Procedural sky configuration (from the SkyboxComponent + directional light via the ECS).
        // bakeNow = one-shot request from the editor's Bake button (rebuild the sky IBL).
        void SetProceduralSky( bool enabled, const glm::vec3& sunDir, float sunIntensity, float sunDiskRadius,
                               bool bakeNow, const CloudSettings& clouds, const SkySettings& sky );

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
        // xyz = world position, w = influence radius in metres (0 = disabled). Refreshed each BeginScene.
        const glm::vec4& GetGrassInteractor() const
        {
            return m_GrassInteractor;
        }

        // CSM debug: the per-cascade shadow depth maps (for the editor's cascade viewer).
        std::shared_ptr<Image2D> GetShadowCascadeImage( uint32_t cascade );
        uint32_t                 GetShadowCascadeCount();

        const std::shared_ptr<Image2D>     GetFinalImage();
        const std::shared_ptr<Framebuffer>& GetTargetFramebuffer() const
        {
            return m_TargetFramebuffer;
        }

        // Deferred G-buffer (Albedo+Metallic / Normal+Roughness / depth). Populated only in the Deferred path.
        const std::shared_ptr<Framebuffer>& GetGBuffer() const
        {
            return m_GBuffer;
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

        void RegisterExternalPass( std::string&& name, std::function<void()> execute,
                                   std::shared_ptr<RenderPass>&& renderPass )
        {
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
        void ClearMainFramebuffer();
        void CompositeRenderPass();
        void ExecuteRenderGraph();

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
        Core::AntiAliasingMode m_AAMode      = Core::AntiAliasingMode::FXAA;
        bool                   m_BloomEnabled = false;
        bool                   m_ScenePlaying = false; // set per frame in BeginScene (hides authoring aids)

    private:
        std::shared_ptr<Framebuffer>                                    m_TargetFramebuffer;
        std::shared_ptr<Framebuffer>                                    m_GBuffer; // deferred G-buffer (MRT)
        std::shared_ptr<Framebuffer>                                    m_SSAOBuffer; // deferred SSAO (AO factor)
        std::shared_ptr<Framebuffer>                                    m_SceneColorCopy; // scene snapshot for glass refraction
        Core::RenderPath m_RenderPath = Core::RenderPath::Forward; // refreshed from SceneSettings each BeginScene
        Core::DeferredDebugMode m_DeferredDebug = Core::DeferredDebugMode::Off; // G-buffer debug view (deferred)
        bool m_EnableSSAO = true; // deferred SSAO pass on/off (refreshed from SceneSettings)
        bool m_EnableSSGI = true; // deferred SSGI (indirect bounce) on/off
        RenderGraphBuilder                                              m_RenderGraphBuilder;
        std::unordered_map<std::string, std::shared_ptr<IRenderSystem>> m_RenderSystems;
        PipelineCache                                                  m_PipelineCache;

    private:
        template <typename System, typename... Args>
        void RegisterSystem( const std::string& system, Args&&... args )
        {
            m_RenderSystems.emplace( std::move( system ),
                                     std::make_shared<System>( std::forward<Args>( args )... ) );
        }
    };
} // namespace Desert::Graphic