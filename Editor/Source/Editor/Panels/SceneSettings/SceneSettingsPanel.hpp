#pragma once

#include "../IPanel.hpp"
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"

#include <memory>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Editor
{
    // Live editor for the active scene's Core::SceneSettings (outline, anti-aliasing, environment,
    // post-processing, debug). Edits the mutable settings reference in place — the renderer reads them
    // every BeginScene.
    class SceneSettingsPanel final : public IPanel
    {
    public:
        explicit SceneSettingsPanel( std::shared_ptr<::Desert::Core::Scene> scene );

        void OnUIRender() override;
        void SetScene( const std::shared_ptr<Desert::Core::Scene>& scene ) override
        {
            m_Scene = scene;
        }

    private:
        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        std::unique_ptr<UI::UIHelper>          m_UIHelper; // for the CSM cascade depth-map thumbnails
    };
} // namespace Desert::Editor
