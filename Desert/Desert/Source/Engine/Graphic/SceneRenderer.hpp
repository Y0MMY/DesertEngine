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

        void BeginScene( const std::shared_ptr<Desert::Core::Scene>& scene, const Core::Camera& camera);
        void EndScene();

        void OnEvent( Common::Event& e );

    private:
        void CompositeRenderPass();
        void GeometryRenderPass();

        /*a temporary solution until we add a material system.*/
        void UpdateDescriptorSets( const std::shared_ptr<Pipeline>& pipeline );

    private:
        bool OnWindowResize( Common::EventWindowResize& e );

    private:
        struct
        {
            std::shared_ptr<Core::Scene> ActiveScene;
            Core::Camera* ActiveCamera;
        } m_SceneInfo;

    private:
        std::shared_ptr<Graphic::Pipeline>    m_TESTPipelineSkybox;
        std::shared_ptr<Graphic::Framebuffer> m_TESTFramebufferSkybox;
        std::shared_ptr<Graphic::Shader>      m_TESTShaderSkybox;
        std::shared_ptr<Graphic::RenderPass>  m_TESTRenderPassSkybox;

        std::shared_ptr<Graphic::Texture> m_TESTTextureSkybox;

    };
} // namespace Desert::Graphic