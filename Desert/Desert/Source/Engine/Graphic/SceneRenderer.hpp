#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Camera.hpp>

#include <Common/Core/Events/WindowEvents.hpp>
#include <Common/Core/EventRegistry.hpp>

#include "Systems/Scene/Mesh/MeshRenderer.hpp"
#include "Systems/Scene/Skybox/SkyboxRenderer.hpp"
#include "Systems/Scene/PostProcessing/TonemapRenderer.hpp"

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
        [[nodiscard]] Common::BoolResult Init();
        void                             Shutdown();

        [[nodiscard]] Common::BoolResult BeginScene( const std::shared_ptr<Desert::Core::Scene>& scene,
                                                     const std::shared_ptr<Core::Camera>&        camera );

        void OnUpdate( const SceneRendererUpdate& sceneRenderInfo );

        [[nodiscard]] Common::BoolResult EndScene();

        void Resize( const uint32_t width, const uint32_t height );

        void AddToRenderMeshList( const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<MaterialPBR>& material,
                                  const glm::mat4& transform );
        const Environment                 CreateEnvironment( const Common::Filepath& filepath );
        void                              SetEnvironment( const std::shared_ptr<MaterialSkybox>& material );
        const std::optional<Environment>& GetEnvironment();

        const auto& GetMainCamera() const
        {
            return m_SceneInfo.ActiveCamera;
        }

        const auto& GetMeshRenderList() const
        {
            return m_MeshRenderData;
        }

        const auto& GetDirectionLights() const
        {
            return m_DirectionLights;
        }

        const std::shared_ptr<Image2D>     GetFinalImage();
        const std::shared_ptr<Framebuffer> GetTargetFramebuffer() const
        {
            return m_TargetFramebuffer;
        }

        void RegisterRenderPass( RenderPhase phase, const std::string& name, std::function<void()> executeFunc,
                                 const PipelineSpecification& pipeSpec = {} );

        void RegisterExternalPass( std::string&& name, std::function<void()> execute,
                                   std::shared_ptr<RenderPass>&& renderPass )
        {
        }

        std::shared_ptr<Framebuffer> GetFramebufferForPhase( RenderPhase phase );
        std::shared_ptr<Texture>     GetTexture( const std::string& name );

        void RegisterRenderSystem( const std::string& name, std::shared_ptr<IRenderSystem> system );
        void UnregisterRenderSystem( const std::string& name );

        void RebuildRenderGraph();

        void AddPointLight( PointLight&& pointLight );

        const auto& GetPointLights() const
        {
            return m_PointLight;
        }

    private:
        void ClearMainFramebuffer();
        void CompositeRenderPass();
        void ExecuteRenderGraph();

    private:
        struct
        {
            std::weak_ptr<Core::Scene>  ActiveScene;
            std::weak_ptr<Core::Camera> ActiveCamera;
        } m_SceneInfo;

        std::vector<DirectionLight> m_DirectionLights;
        std::vector<PointLight>     m_PointLight;
        std::vector<MeshRenderData> m_MeshRenderData;

    private:
        std::shared_ptr<Framebuffer>                                    m_TargetFramebuffer;
        RenderGraphBuilder                                              m_RenderGraphBuilder;
        std::unordered_map<std::string, std::shared_ptr<IRenderSystem>> m_RenderSystems;

    private:
        template <typename System, typename... Args>
        void RegisterSystem( const std::string& system, Args&&... args )
        {
            m_RenderSystems[system] = std::make_unique<System>( std::forward<Args>( args )... );
        }
    };
} // namespace Desert::Graphic