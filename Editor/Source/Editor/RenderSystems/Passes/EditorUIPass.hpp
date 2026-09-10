#pragma once

#include <Engine/Desert.hpp>
#include <Engine/Graphic/Render2D/Render2D.hpp>
#include <Engine/UI/UICanvasContext.hpp>

namespace Desert::Editor::Render
{
    // Draws the scene's UI canvas with the engine's own 2D batcher (Render2D) instead of ImGui — a
    // UI-phase pass into the scene HDR target, composited on top via a load pass. Installed like the
    // grid, through the Editor Pass API (Scene::RegisterExternalPass). This is the migration vehicle:
    // it starts with flat-colour panels and grows toward full parity, after which the ImGui UI overlay
    // is retired.
    class EditorUIPass
    {
    public:
        ~EditorUIPass();

        // (Re)creates the Render2D pipeline against the scene's CURRENT target framebuffer and registers
        // the pass. Call after every Scene::Init — the framebuffers are recreated there.
        Common::BoolResultStr Install( const std::shared_ptr<Core::Scene>& scene );

    private:
        std::weak_ptr<Core::Scene>  m_Scene;
        Graphic::Render2D::Render2D m_Render2D;

        // The UI runtime state of THIS viewport — one cell per (canvas x this view). One EditorUIPass
        // exists per open scene document (Render::RenderRegistry builds one in its constructor), so this is
        // what keeps two viewports from sharing a hover clock, an elected hot element or a screen stack,
        // and the cells inside it are what keeps the level's own canvases from sharing them with each
        // other. It also drives the scene's UIAnim playheads — the UI Editor panel's preview deliberately
        // does not, or a clip would advance twice a frame.
        //
        // LIFETIME: by value in the pass, and the pass is owned by the document's RenderRegistry. Closing
        // the document destroys the pass and with it every cell — there is nothing to release by hand.
        ::Desert::UI::UIViewContext m_UIView;
    };
} // namespace Desert::Editor::Render
