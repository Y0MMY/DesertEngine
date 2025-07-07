#pragma once

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

    private:
        void CompositeRenderPass();
        void ToneMapRenderPass();

        const glm::vec3 BuildDirectionLight( const std::vector<DirectionLight>& dirLights );

    private:
        struct
        {
            std::shared_ptr<Core::Scene> ActiveScene;
            Core::Camera*                ActiveCamera;
        } m_SceneInfo;

    private:
        std::unique_ptr<System::MeshRenderer>   m_MeshRenderer;
        std::unique_ptr<System::SkyboxRenderer> m_SkyboxRenderer;
    };
} // namespace Desert::Graphic