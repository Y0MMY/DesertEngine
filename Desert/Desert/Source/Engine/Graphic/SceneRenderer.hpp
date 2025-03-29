#pragma once

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Camera.hpp>

#include <Common/Core/Events/WindowEvents.hpp>

namespace Desert::Graphic
{
    class ::Desert::Core::Scene;

    class SceneRenderer final
    {
    public:
        void Init();

        void BeginScene( const std::shared_ptr<Desert::Core::Scene>& scene, const Core::Camera& camera );
        void EndScene();

        void OnEvent( Common::Event& e );

        void RenderMesh( const std::shared_ptr<Mesh>& mesh );

    private:
        void CompositeRenderPass();
        void GeometryRenderPass();

        /*a temporary solution until we add a material system.*/
        void UpdateDescriptorSets( const std::shared_ptr<Pipeline>& pipeline );
        void UpdateDescriptorSets2( void* dst, void* imageview, void* sampler );

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