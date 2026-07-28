#pragma once

#include "IRender.hpp"
#include "Passes/EditorGridPass.hpp"

namespace Desert::Editor::Render
{
    // Owns the editor-side render passes injected into the scene render graph through the engine's
    // Editor Pass API (Scene::RegisterExternalPass): the grid now, gizmos/debug draw next. Recreated
    // after every Scene::Init so the pass pipelines rebuild against the fresh scene framebuffers —
    // reset the old registry BEFORE constructing the new one, or the old destructor unregisters the
    // freshly installed passes.
    class RenderRegistry
    {
    public:
        RenderRegistry( const std::shared_ptr<Core::Scene>& scene );

        void Render();

    private:
        std::weak_ptr<Core::Scene> m_Scene;

        std::unique_ptr<EditorGridPass> m_GridPass;
    };
} // namespace Desert::Editor::Render
