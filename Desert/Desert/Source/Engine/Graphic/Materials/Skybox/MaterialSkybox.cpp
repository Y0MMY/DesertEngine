#include "MaterialSkybox.hpp"

namespace Desert::Graphic
{
    MaterialSkybox::MaterialSkybox( const std::shared_ptr<Assets::SkyboxAsset>& baseAsset )
         : Material( "MaterialSkybox", "skybox.glsl" ), m_BaseMaterial( baseAsset )
    {

        m_CameraModel = std::make_unique<Models::CameraData>( m_MaterialExecutor, "Camera" );
        m_SkyboxModel = std::make_unique<Models::SkyboxData>( m_MaterialExecutor );

        m_Environment = Graphic::EnvironmentManager::Create( baseAsset );
    }

    void MaterialSkybox::Bind(const UpdateMaterialSkyboxInfo& data)
    {
        m_SkyboxModel->UpdateSkybox(
             m_Environment.RadianceMap ); // TODO: maybe it will be unnecessary to update every frame! the
                                          // approach needs to be reconsidered!

        m_CameraModel->Update( Models::CameraDataUB{ .Projection = data.Camera->GetProjectionMatrix(),
                                                     .View       = data.Camera->GetViewMatrix(),
                                                     .CameraPos  = data.Camera->GetPosition() } ); // TODO: constant push
    }

} // namespace Desert::Graphic