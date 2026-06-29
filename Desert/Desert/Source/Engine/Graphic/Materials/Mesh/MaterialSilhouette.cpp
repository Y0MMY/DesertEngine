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

    MaterialSilhouetteSkinned::MaterialSilhouetteSkinned()
         : Material( "MaterialSilhouetteSkinned", "Silhouette_Skinned" )
    {
    }

    void MaterialSilhouetteSkinned::UpdateCamera( const Core::Camera* camera )
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

    void MaterialSilhouetteSkinned::SetBones( const std::vector<glm::mat4>& boneMatrices )
    {
        if ( boneMatrices.empty() )
            return;
        if ( auto* sb = Get<StorageBufferProperty>( "Bones" ) )
            sb->SetRawData( boneMatrices.data(),
                            static_cast<uint32_t>( boneMatrices.size() * sizeof( glm::mat4 ) ) );
    }
} // namespace Desert::Graphic
