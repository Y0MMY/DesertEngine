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
        struct UpdateInfo
        {
            Common::Timestep                Timestep;
            ShaderProtocols::DirectionLight DirLights;
        };

        struct RenderSubmissionExtra
        {
            std::vector<glm::mat4> BoneMatrices; // optional
        };

        ~SceneRenderer() = default;

        void Init();

        [[nodiscard]] Common::BoolResultStr BeginScene( const Desert::Core::Scene& scene );

        void OnUpdate( const UpdateInfo& sceneRenderInfo );

        [[nodiscard]] Common::BoolResultStr EndScene();

        void Resize( const uint32_t width, const uint32_t height );

        void SubmitMesh( const Mesh* mesh, const std::vector<MaterialInstance*> materialSlots, const glm::mat4& transform,
                         const RenderSubmissionExtra& extra );

        const Environment                 CreateEnvironment( const Common::Filepath& filepath );
        void                              SetEnvironment( const std::shared_ptr<MaterialSkybox>& material );
        const std::optional<Environment>& GetEnvironment();

        const auto& GetMainCamera() const
        {
            return m_SceneInfo.ActiveCamera;
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

        void AddPointLight( ShaderProtocols::PointLightPayload&& pointLight );

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
            Core::Camera* ActiveCamera;
        } m_SceneInfo;

        ShaderProtocols::DirectionLight m_DirectionLights;
        ShaderProtocols::PointLight     m_PointLight;

    private:
        std::shared_ptr<Framebuffer>                                    m_TargetFramebuffer;
        RenderGraphBuilder                                              m_RenderGraphBuilder;
        std::unordered_map<std::string, std::shared_ptr<IRenderSystem>> m_RenderSystems;

    private:
        template <typename System, typename... Args>
        void RegisterSystem( const std::string& system, Args&&... args )
        {
            m_RenderSystems.emplace( std::move( system ),
                                     std::make_shared<System>( std::forward<Args>( args )... ) );
        }
    };
} // namespace Desert::Graphic