#pragma once

#include "../IPanel.hpp"

#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>

#include <Engine/Graphic/Render2D/Render2D.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Graphic
{
    class Framebuffer;
    class RenderPass;
} // namespace Desert::Graphic

namespace Desert::Editor
{
    // Authoring surface for the screen-space UI: a toolbar that places elements on the scene's UICanvas and
    // a preview of that canvas.
    //
    // THE PREVIEW IS DRAWN BY THE SHIPPING RENDERER. It used to be drawn by a second, ImGui-based canvas
    // renderer that knew six of the engine's twenty-two UI component types, so pressing "+ Slider" in this
    // very panel added a slider its own preview could not show. That renderer is gone; this panel now runs
    // UI::RenderCanvas2D — the function the game and the viewport run — into its own offscreen target and
    // shows the result as an image.
    //
    // FRAME ORDERING IS PART OF THE CONTRACT, as in PreviewViewport: RenderPreview() records a render pass
    // and must run from OnPreUpdate(); OnUIRender() only shows the image it produced. Recording from inside
    // the ImGui pass would release pipelines and descriptor pools bound to the recording command buffer.
    class UIEditorPanel final : public IPanel
    {
    public:
        explicit UIEditorPanel( const std::shared_ptr<::Desert::Core::Scene>& scene );

        // Waits for device idle before releasing the preview target: it owns a framebuffer and the three
        // Render2D pipelines built against it, which a submitted frame may still be executing.
        ~UIEditorPanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 900.0f, 560.0f );
        }

        // Records this frame's canvas render into the offscreen target (see the class comment).
        void OnPreUpdate() override;
        void OnUIRender() override;

        // Contextual: it authors a UI element.
        // NOT contextual: selecting an object is not a request to open the UI editor. It opens only when
        // asked for — a button in Details, the View menu, or the command palette (Core::PanelRequests).
        bool IsContextual() const override
        {
            return false;
        }
        bool IsRelevant() const override;
        void SetScene( const std::shared_ptr<Desert::Core::Scene>& scene ) override
        {
            m_Scene = scene;
        }

    private:
        // (Re)create the offscreen target + the Render2D pipelines for a @p width x @p height design
        // resolution. Returns false and fills m_PreviewError (logged with the reason) on failure.
        bool EnsureTarget( uint32_t width, uint32_t height );

        // Release the target and everything built against it, behind a device-idle wait.
        void ReleaseTarget();

        std::shared_ptr<::Desert::Core::Scene> m_Scene;

        std::unique_ptr<Editor::UI::UIHelper>       m_UIHelper;
        std::shared_ptr<Graphic::Framebuffer>       m_Target;
        std::shared_ptr<Graphic::RenderPass>        m_RenderPass;
        Graphic::Render2D::Render2D                 m_Render2D;
        uint32_t                                    m_TargetWidth  = 0;
        uint32_t                                    m_TargetHeight = 0;

        // True once OnPreUpdate has recorded a canvas into m_Target this frame — OnUIRender only shows the
        // image when it has one, so a panel opened mid-frame draws its chrome and the picture one frame on.
        bool m_PreviewRecorded = false;

        // Non-empty when the target or its pipelines could not be built. Shown in the panel AND logged at
        // the site with the reason; a preview that silently stays black is the fallback this project bans.
        std::string m_PreviewError;
    };
} // namespace Desert::Editor
