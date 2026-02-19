#pragma once

#include <Engine/Desert.hpp>

#include "MaterialGridModel.hpp"

namespace Desert::Editor::Render
{

    class MaterialGrid : public Graphic::Material
    {
    public:
        MaterialGrid();
        ~MaterialGrid() = default;

        void Bind( const std::shared_ptr<Desert::Core::Camera>& camera );
        void SetGridProperties( float cellSize, float cellCount, const glm::vec4& color );

    private:
    };
} // namespace Desert::Editor::Render