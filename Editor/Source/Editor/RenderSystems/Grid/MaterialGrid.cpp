#include "MaterialGrid.hpp"

namespace Desert::Editor::Render
{
    MaterialGrid::MaterialGrid() : Material( "Grid", "Grid.glsl" )
    {
        m_GridModel   = std::make_unique<Model::MaterialGridProperties>( m_MaterialExecutor );
        m_CameraModel = std::make_unique<Graphic::Models::CameraData>( m_MaterialExecutor, "Camera" );
    }

    void MaterialGrid::UpdateRenderParameters( const Core::Camera& camera )
    {
        m_CameraModel->UpdateCameraUB( camera );
    }

    void MaterialGrid::SetGridProperties( float cellSize, float cellCount, const glm::vec4& color )
    {
        Model::GridMaterialPropertiesUB model{ .CellSize = cellSize, .CellScale = cellCount };

        m_GridModel->Update( model );
    }

} // namespace Desert::Editor::Render