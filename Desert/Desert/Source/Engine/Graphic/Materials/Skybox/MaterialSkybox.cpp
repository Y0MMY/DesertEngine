#include "MaterialSkybox.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/ShaderProtocols/Camera.hpp>

namespace Desert::Graphic
{
    MaterialSkybox::MaterialSkybox( const std::shared_ptr<Assets::SkyboxAsset>& baseAsset )
         : Material( "MaterialSkybox", "Skybox" ), m_BaseMaterial( baseAsset )
    {
        m_CubeMapTexture = m_MaterialExecutor->GetTextureCubeProperty( "samplerCubeMap" ).get();
        m_Environment    = Graphic::EnvironmentManager::Create( baseAsset );
    }

    void MaterialSkybox::Bind( const UpdateMaterialSkyboxInfo& data )
    {
        if ( !m_Environment.RadianceMap.IsValid() ||
             m_Environment.RadianceMap.ImageType != Runtime::ImageHandle::Type::ImageCube )
        {
            return;
        }

        auto* image = static_cast<ImageCube*>(
             Runtime::ResourceRegistry::GetImageService()->Resolve( m_Environment.RadianceMap ) );
        if ( !image )
            return;

        if ( m_CubeMapTexture )
            m_CubeMapTexture->SetTexture( image );

        static ShaderProtocols::Camera CameraUB;
        CameraUB.View       = data.Camera->GetViewMatrix();
        CameraUB.Projection = data.Camera->GetProjectionMatrix();
        CameraUB.CameraPos  = data.Camera->GetPosition();

        Get<UniformBufferProperty>( CameraUB.Name )
             ->SetRawData( reinterpret_cast<std::byte*>( &CameraUB ), sizeof( ShaderProtocols::Camera ) );
    }

} // namespace Desert::Graphic
