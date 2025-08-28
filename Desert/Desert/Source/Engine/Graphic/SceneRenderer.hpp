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

        const std::shared_ptr<Image2D>     GetFinalImage() const;
        const std::shared_ptr<Framebuffer> GetTargetFramebuffer() const
        {
            return m_TargetFramebuffer;
        }

        void RegisterExternalPass( std::string&& name, std::function<void()> execute,
                                   std::shared_ptr<RenderPass>&& renderPass );

    private:
        void CompositeRenderPass();
    private:
        struct
        {
            std::weak_ptr<Core::Scene>  ActiveScene;
            std::weak_ptr<Core::Camera> ActiveCamera;
        } m_SceneInfo;

        std::vector<DirectionLight> m_DirectionLights;
        std::vector<MeshRenderData> m_MeshRenderData;

    private:
        std::shared_ptr<RenderGraph> m_RenderGraphRenderSystems;
        std::shared_ptr<RenderGraph> m_RenderGraphPPSystems; // Post-Proccessing

    private:
        std::shared_ptr<Framebuffer> m_TargetFramebuffer;

    private:
        static constexpr size_t                                                 s_RenderSystemsCount = 3U;
        std::array<std::unique_ptr<System::RenderSystem>, s_RenderSystemsCount> m_FixedRenderSystems;

        template <typename System, typename... Args>
        void RegisterSystem( const uint32_t system, Args&&... args )
        {
            m_FixedRenderSystems[system] = std::make_unique<System>( std::forward<Args>( args )... );
        }
    };
} // namespace Desert::Graphic