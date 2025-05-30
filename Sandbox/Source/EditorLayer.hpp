#pragma once

#include <Engine/Desert.hpp>

namespace Desert
{
    class EditorLayer : public Common::Layer
    {
    public:
        EditorLayer( const std::string& layerName );
        ~EditorLayer();

        [[nodiscard]] virtual Common::BoolResult OnAttach() override;
        [[nodiscard]] virtual Common::BoolResult OnDetach() override;
        [[nodiscard]] virtual Common::BoolResult OnUpdate( Common::Timestep ts ) override;
        [[nodiscard]] virtual Common::BoolResult OnImGuiRender() override;

    private:
        std::shared_ptr<Core::Scene>            m_testscene;
        std::shared_ptr<Graphic::SceneRenderer> m_testscenerenderer;
        std::shared_ptr<Graphic::TextureCube>   m_Skybox;
        std::shared_ptr<Mesh>                   m_Mesh;

        Core::Camera m_EditorCamera;

        ImVec2 m_Size;

#ifdef EBABLE_IMGUI
        std::shared_ptr<ImGui::ImGuiLayer> m_ImGuiLayer;
#endif // EBABLE_IMGUI
    };
} // namespace Desert