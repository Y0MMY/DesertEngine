#pragma once

#include <Engine/Desert.hpp>
#include <Engine/Graphic/Render2D/Render2D.hpp>

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
    };
} // namespace Desert::Editor::Render
