#pragma once

#include "IRender.hpp"

namespace Desert::Editor::Render
{
    // The scene grid is now an engine-side System::GridRenderer (registered in SceneRenderer); this
    // editor registry no longer owns it.
    class RenderRegistry
    {
    public:
        RenderRegistry( const std::shared_ptr<Core::Scene>& scene );

        void Render();

    private:
        std::weak_ptr<Core::Scene> m_Scene;
    };
} // namespace Desert::Editor::Render