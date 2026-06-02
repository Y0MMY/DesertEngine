#include "MaterialSkybox.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Engine/Graphic/ShaderProtocols/Camera.hpp>

namespace Desert::Graphic
{
    MaterialSkybox::MaterialSkybox( const std::shared_ptr<Assets::SkyboxAsset>& baseAsset )
         : Material( "MaterialSkybox", "Skybox" ), m_BaseMaterial( baseAsset )
    {
        m_SkyboxBinding = std::make_unique<MaterialHelper::SkyboxDataBinding>( m_MaterialExecutor.get() );

        m_Environment = Graphic::EnvironmentManager::Create( baseAsset );
    }

    void MaterialSkybox::Bind( const UpdateMaterialSkyboxInfo& data )
    {
        if ( !m_Environment.RadianceMap.IsValid() ||
             m_Environment.RadianceMap.ImageType != Runtime::ImageHandle::Type::ImageCube )
        {
            return;
        }
        ImageCube* image =
             (ImageCube*)Runtime::ResourceRegistry::GetImageService()->Resolve( m_Environment.RadianceMap );
        if ( !image )
        {
            return;
        }

        m_SkyboxBinding->UpdateSkybox( image );

        static ShaderProtocols::Camera CameraUB;
        CameraUB.View       = data.Camera->GetViewMatrix();
        CameraUB.Projection = data.Camera->GetProjectionMatrix();
        CameraUB.CameraPos  = data.Camera->GetPosition();

        Get<UniformBufferProperty>( CameraUB.Name )
             ->SetRawData( (std::byte*)&CameraUB, sizeof( ShaderProtocols::Camera ) );

        m_MaterialExecutor->Apply();
    }

} // namespace Desert::Graphic