#include "MaterialGrid.hpp"

namespace Desert::Editor::Render
{
    MaterialGrid::MaterialGrid() : Material( "Grid", "Grid.glsl" )
    {
        m_CameraModel = std::make_unique<Graphic::Models::CameraData>( m_MaterialExecutor, "Camera" );
    }

    void MaterialGrid::Bind( const std::shared_ptr<Desert::Core::Camera>& camera )
    {
        m_CameraModel->Update( Graphic::Models::CameraDataUB{ .Projection = camera->GetProjectionMatrix(),
                                                              .View       = camera->GetViewMatrix(),
                                                              .CameraPos  = camera->GetPosition() } );
    }

    void MaterialGrid::SetGridProperties( float cellSize, float cellCount, const glm::vec4& color )
    {
       
    }

} // namespace Desert::Editor::Render