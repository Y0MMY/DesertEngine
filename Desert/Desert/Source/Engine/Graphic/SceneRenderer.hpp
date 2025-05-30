#pragma once

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Camera.hpp>

#include <Common/Core/Events/WindowEvents.hpp>
#include <Common/Core/EventRegistry.hpp>

namespace Desert::Core
{
    class Scene;
};

namespace Desert::Graphic
{
    class SceneRenderer final : public Common::EventHandler
    {
    public:
        [[nodiscard]] Common::BoolResult Init();
        void                             Shutdown();

        [[nodiscard]] Common::BoolResult BeginScene( const std::shared_ptr<Desert::Core::Scene>& scene,
                                                     const Core::Camera&                         camera );

        void OnUpdate();

        [[nodiscard]] Common::BoolResult EndScene();

        void OnEvent( Common::Event& e ) override;

        void               RenderMesh( const std::shared_ptr<Mesh>& mesh );
        const Environment  CreateEnvironment( const Common::Filepath& filepath );
        void               SetEnvironment( const Environment& environment );
        const Environment& GetEnvironment();

        const std::shared_ptr<Image2D> GetFinalImage() const;

    private:
        void CompositeRenderPass();
        void ToneMapRenderPass();
        void GeometryRenderPass();

    private:
        bool OnWindowResize( Common::EventWindowResize& e );

    private:
        struct MeshRenderInfo
        {
            std::shared_ptr<Mesh> Mesh;
        };

        struct RenderInfo
        {
            std::shared_ptr<Graphic::Shader>               Shader;
            std::shared_ptr<Graphic::Framebuffer>          Framebuffer;
            std::shared_ptr<Graphic::RenderPass>           RenderPass;
            std::shared_ptr<Graphic::Pipeline>             Pipeline;
            std::shared_ptr<Graphic::Material>             Material;
            std::shared_ptr<Graphic::UniformBufferManager> UBManager;
        };

        struct
        {
            std::shared_ptr<Core::Scene> ActiveScene;
            Core::Camera*                ActiveCamera;
            Environment                  EnvironmentData;

            struct
            {
                RenderInfo Skybox;
                RenderInfo Composite;
                RenderInfo Geometry;

                std::vector<MeshRenderInfo> MeshInfo;
            } Renderdata;
        } m_SceneInfo;
    };
} // namespace Desert::Graphic