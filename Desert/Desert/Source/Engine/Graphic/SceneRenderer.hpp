#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/SpotLight.hpp>
#include <Engine/Graphic/CloudSettings.hpp>
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
            bool                   Outlined = false;
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
                            const glm::vec3& grassTint = glm::vec3( 1.0f ),
                            const std::vector<std::pair<std::string, glm::vec4>>& paramOverrides   = {},
                            const std::vector<std::pair<std::string, uint64_t>>&  textureOverrides = {} );

        // Submit a mesh drawn with a generic data-driven material (MaterialComponent with a non-PBR shader).
        void SubmitGenericMesh( const Mesh* mesh, const glm::mat4& transform, const std::string& shaderName,
                                const std::vector<std::pair<std::string, glm::vec4>>& paramOverrides,
                                const std::vector<std::pair<std::string, uint64_t>>&  textureOverrides = {},
                                bool                                                  outlined = false );

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
                               bool bakeNow, const CloudSettings& clouds );

        const auto& GetMainCamera() const
        {
            return m_SceneInfo.ActiveCamera;
        }

        const auto& GetDirectionLights() const
        {
            return m_DirectionLights;
        }

        // CSM debug: the per-cascade shadow depth maps (for the editor's cascade viewer).
        std::shared_ptr<Image2D> GetShadowCascadeImage( uint32_t cascade );
        uint32_t                 GetShadowCascadeCount();

        const std::shared_ptr<Image2D>     GetFinalImage();
        const std::shared_ptr<Framebuffer>& GetTargetFramebuffer() const
        {
            return m_TargetFramebuffer;
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

        // Selected post-process anti-aliasing technique, refreshed from SceneSettings each BeginScene.
        Core::AntiAliasingMode m_AAMode      = Core::AntiAliasingMode::FXAA;
        bool                   m_BloomEnabled = false;

    private:
        std::shared_ptr<Framebuffer>                                    m_TargetFramebuffer;
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