#include "MaterialSkybox.hpp"

namespace Desert::Graphic
{
    MaterialSkybox::MaterialSkybox( const std::shared_ptr<Assets::SkyboxAsset>& baseAsset )
         : Material( "MaterialSkybox", "skybox.glsl" ), m_BaseMaterial( baseAsset )
    {

        m_CameraModel = std::make_unique<Models::CameraData>( m_MaterialExecutor, "camera");
        m_SkyboxModel = std::make_unique<Models::SkyboxData>( m_MaterialExecutor );

        m_Environment = Graphic::EnvironmentManager::Create( baseAsset );
    }

    void MaterialSkybox::UpdateRenderParameters( const Core::Camera& camera )
    {
        m_SkyboxModel->UpdateSkybox(
             m_Environment.RadianceMap ); // TODO: maybe it will be unnecessary to update every frame! the
                                          // approach needs to be reconsidered!

        m_CameraModel->UpdateCameraUB( camera ); // TODO: constant push
    }

} // namespace Desert::Graphic