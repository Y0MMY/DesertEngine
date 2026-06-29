#include "MaterialShadow.hpp"

#include <Engine/Graphic/ShaderProtocols/Camera.hpp>

namespace Desert::Graphic
{
    MaterialShadow::MaterialShadow() : Material( "MaterialShadow", "Shadow" )
    {
    }

    MaterialShadow::MaterialShadow( std::string&& shaderName )
         : Material( "MaterialShadowInstanced", std::move( shaderName ) )
    {
    }

    MaterialShadowInstanced::MaterialShadowInstanced() : MaterialShadow( "Shadow_Instanced" )
    {
    }

    void MaterialShadow::SetLightMatrix( const glm::mat4& view, const glm::mat4& projection )
    {
        ShaderProtocols::Camera cameraUB;
        cameraUB.Projection = projection;
        cameraUB.View       = view;
        cameraUB.CameraPos  = glm::vec3( 0.0f );

        Get<UniformBufferProperty>( ShaderProtocols::Camera::Name )
             ->SetRawData( reinterpret_cast<const std::byte*>( &cameraUB ), sizeof( cameraUB ) );
    }
} // namespace Desert::Graphic
