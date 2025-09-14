#pragma once

#include <Engine/Desert.hpp>
#include <Engine/Graphic/Materials/Models/Common/Camera.hpp>

#include "MaterialGridModel.hpp"

namespace Desert::Editor::Render
{

    class MaterialGrid : public Graphic::Material
    {
    public:
        MaterialGrid();
        ~MaterialGrid() = default;

        void Bind( const std::shared_ptr<Desert::Core::Camera>& camera ) ;
        void SetGridProperties( float cellSize, float cellCount, const glm::vec4& color );

    private:
        std::unique_ptr<Graphic::Models::CameraData>   m_CameraModel;
    };
} // namespace Desert::Editor::Render