#pragma once

#include "../IPanel.hpp"

#include <Common/Core/UUID.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ax::NodeEditor
{
    struct EditorContext;
}
namespace Desert::Core
{
    class Scene;
}
namespace Desert::Animation
{
    class AnimationLibrary;
}
namespace Desert::ECS
{
    struct AnimationComponent;
}

namespace Desert::Editor
{
    // Visual AnimGraph editor: an imgui-node-editor canvas over the SELECTED entity's AnimationComponent graph
    // — STATES are nodes, TRANSITIONS are links. Edits the live in-memory graph directly (which persists with
    // the scene), so the running state machine reflects changes immediately. A side panel edits the selected
    // state (clip/loop/speed/entry) or transition (blend/exit-time/conditions), plus parameters with live
    // value controls. Hidden by default; enable via View -> Anim Graph.
    class AnimGraphPanel final : public IPanel
    {
    public:
        AnimGraphPanel( const std::shared_ptr<::Desert::Core::Scene>& scene,
                        const Animation::AnimationLibrary*            library );
        ~AnimGraphPanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 1040.0f, 640.0f );
        }

        void OnUIRender() override;

        // Contextual: a state machine belongs to an animated entity.
        bool IsContextual() const override
        {
            return true;
        }

        bool IsRelevant() const override;
        void OnPreUpdate() override;

        // Cross-panel: a Details "Open in Anim Graph" button asks the panel to reveal itself next frame.
        static void RequestOpen();

    private:
        void DrawCanvas( ECS::AnimationComponent& anim, const std::vector<std::string>& clipNames );
        void DrawSidePanel( ECS::AnimationComponent& anim, const std::vector<std::string>& clipNames );

        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        const Animation::AnimationLibrary*     m_Library = nullptr;
        ax::NodeEditor::EditorContext*         m_Context = nullptr;

        Common::UUID m_LastEntity{ 0 };
        bool         m_ApplyPositions = true; // push State.X/Y into the canvas when the target entity changes
    };
} // namespace Desert::Editor
