#pragma once

#include <Engine/Desert.hpp>

namespace Desert
{
    class EditorLayer : public Common::Layer
    {
    public:
        EditorLayer( const std::string& layerName );

        virtual void OnAttach() override;
        virtual void OnDetach() override
        {
        }
        virtual void OnUpdate( Common::Timestep ts ) override;

    private:
        std::shared_ptr<Core::Scene>            m_testscene;
        std::shared_ptr<Graphic::SceneRenderer> m_testscenerenderer;
        std::shared_ptr<Graphic::TextureCube>   m_Environment;

        Core::Camera m_EditorCamera;
    };
} // namespace Desert