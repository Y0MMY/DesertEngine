#pragma once

#include "IRender.hpp"

#include "Grid/Grid.hpp"

namespace Desert::Editor::Render
{
    class RenderRegistry
    {
    public:
        RenderRegistry( const std::shared_ptr<Core::Scene>& scene );

        void Render();

    private:
        std::weak_ptr<Core::Scene> m_Scene;
        std::unique_ptr<Grid>      m_RenderGrid;
    };
} // namespace Desert::Editor::Render