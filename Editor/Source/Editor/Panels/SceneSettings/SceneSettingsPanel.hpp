#pragma once

#include "../IPanel.hpp"

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

    private:
        std::shared_ptr<::Desert::Core::Scene> m_Scene;
    };
} // namespace Desert::Editor
