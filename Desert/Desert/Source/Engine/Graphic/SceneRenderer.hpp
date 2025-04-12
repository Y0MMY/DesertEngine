#pragma once

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Material.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Camera.hpp>

#include <Common/Core/Events/WindowEvents.hpp>

namespace Desert::Graphic
{
    class ::Desert::Core::Scene;

    class SceneRenderer final
    {
    public:
        [[nodiscard]] Common::BoolResult Init();

        [[nodiscard]] Common::BoolResult BeginScene( const std::shared_ptr<Desert::Core::Scene>& scene,
                                                     const Core::Camera&                         camera );
        [[nodiscard]] Common::BoolResult EndScene();

        void OnEvent( Common::Event& e );

        void RenderMesh( const std::shared_ptr<Mesh>& mesh );

    private:
        void CompositeRenderPass();
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
            std::shared_ptr<Graphic::Shader>      Shader;
            std::shared_ptr<Graphic::Framebuffer> Framebuffer;
            std::shared_ptr<Graphic::RenderPass>  RenderPass;
            std::shared_ptr<Graphic::Pipeline>    Pipeline;
            std::shared_ptr<Graphic::Material>    Material;
        };

        struct
        {
            std::shared_ptr<Core::Scene> ActiveScene;
            Core::Camera*                ActiveCamera;

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