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
                                                     const Core::Camera&                         camera );

        void OnUpdate( const SceneRendererUpdate& sceneRenderInfo );

        [[nodiscard]] Common::BoolResult EndScene();

        void Resize( const uint32_t width, const uint32_t height );

        void AddToRenderMeshList( const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<MaterialPBR>& material,
                                  const glm::mat4& transform );
        const Environment  CreateEnvironment( const Common::Filepath& filepath );
        void               SetEnvironment( const std::shared_ptr<MaterialSkybox>& material );
        const Environment& GetEnvironment();

        const std::shared_ptr<Image2D> GetFinalImage() const;

        void RegisterExternalPass( std::string&& name, std::function<void()> execute,
                                   std::shared_ptr<RenderPass>&& renderPass );

    private:
        void CompositeRenderPass();

        const glm::vec3 BuildDirectionLight( const std::vector<DirectionLight>& dirLights );

    private:
        struct
        {
            std::shared_ptr<Core::Scene> ActiveScene;
            Core::Camera*                ActiveCamera;
        } m_SceneInfo;

    private:
        // RenderGraph m_RenderGraph;
        // RenderGraph m_ExternalRenderGraph;

    private:
        std::shared_ptr<Framebuffer> m_CompositeFramebuffer;

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