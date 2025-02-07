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
        Core::Scene                             m_testscene;
        std::shared_ptr<Graphic::SceneRenderer> m_testscenerenderer;
    };
} // namespace Desert