#include "MaterialSilhouette.hpp"

#include <Engine/Graphic/ShaderProtocols/Camera.hpp>

namespace Desert::Graphic
{
    MaterialSilhouette::MaterialSilhouette() : Material( "MaterialSilhouette", "Silhouette" )
    {
    }

    void MaterialSilhouette::UpdateCamera( const Core::Camera* camera )
    {
        if ( !camera )
            return;

        ShaderProtocols::Camera cameraUB;
        cameraUB.Projection = camera->GetProjectionMatrix();
        cameraUB.View       = camera->GetViewMatrix();
        cameraUB.CameraPos  = camera->GetPosition();

        Get<UniformBufferProperty>( ShaderProtocols::Camera::Name )
             ->SetRawData( reinterpret_cast<const std::byte*>( &cameraUB ), sizeof( cameraUB ) );
    }
} // namespace Desert::Graphic
