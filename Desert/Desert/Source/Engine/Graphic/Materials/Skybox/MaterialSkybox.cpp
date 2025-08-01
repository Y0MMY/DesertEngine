#include "MaterialSkybox.hpp"

namespace Desert::Graphic
{
    MaterialSkybox::MaterialSkybox( const std::shared_ptr<Assets::SkyboxAsset>& baseAsset )
         : m_BaseMaterial( baseAsset )
    {

        const auto& shader = Graphic::ShaderLibrary::Get( "skybox.glsl", {} );

        m_Material = Graphic::MaterialExecutor::Create( "MaterialSkybox", shader.GetValue() );

        m_CameraModel = std::make_unique<Models::CameraData>( m_Material );
        m_SkyboxModel = std::make_unique<Models::SkyboxData>( m_Material );

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