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

        // HDR skybox brightness: a std140 vec4 (x = intensity); the Skybox fragment multiplies the sampled
        // radiance by .x. Null-guarded so an older Skybox shader without the UBO still binds cleanly.
        const float params[4] = { data.Intensity, 0.0f, 0.0f, 0.0f };
        if ( auto* ub = Get<UniformBufferProperty>( "SkyboxParamsUB" ) )
            ub->SetRawData( reinterpret_cast<const std::byte*>( params ), sizeof( params ) );
    }

} // namespace Desert::Graphic
