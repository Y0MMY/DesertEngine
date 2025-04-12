#pragma once

#include <Engine/Desert.hpp>

namespace Desert
{
    class EditorLayer : public Common::Layer
    {
    public:
        EditorLayer( const std::string& layerName );

        [[nodiscard]] virtual Common::BoolResult OnAttach() override;
        [[nodiscard]] virtual Common::BoolResult OnDetach() override
        {
            return BOOLSUCCESS;
        }
        [[nodiscard]] virtual Common::BoolResult OnUpdate( Common::Timestep ts ) override;
        [[nodiscard]] virtual Common::BoolResult OnImGuiRender() override
        {
            return BOOLSUCCESS;
        }

    private:
        std::shared_ptr<Core::Scene>            m_testscene;
        std::shared_ptr<Graphic::SceneRenderer> m_testscenerenderer;
        std::shared_ptr<Graphic::TextureCube>   m_Environment;
        std::shared_ptr<Mesh>                   m_Mesh;

        Core::Camera m_EditorCamera;
    };
} // namespace Desert