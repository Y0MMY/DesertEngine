#pragma once

#include "../IPanel.hpp"

#include <memory>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Editor
{
    // 2D authoring canvas for the screen-space UI (Godot-Control-style). Renders the scene's UICanvas + its
    // UILayout/UIPanel/UIText/UIButton child tree letterboxed to the design resolution, resolving anchors +
    // offsets each frame (Engine/UI/UILayout). Lets you build + SEE a main menu directly; buttons highlight on
    // hover. Hidden by default; View -> UI Editor. (Runtime in-game rendering via a Vulkan 2D pass is a
    // follow-up; this is the authoring + preview surface.)
    class UIEditorPanel final : public IPanel
    {
    public:
        explicit UIEditorPanel( const std::shared_ptr<::Desert::Core::Scene>& scene );

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 900.0f, 560.0f );
        }
        void OnUIRender() override;

    private:
        std::shared_ptr<::Desert::Core::Scene> m_Scene;
    };
} // namespace Desert::Editor
