#include "MaterialPBRBase.hpp"

#include <Engine/Assets/Mapper.hpp>

#include <Engine/Graphic/ShaderProtocols/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/Metadata.hpp>

namespace Desert::Graphic
{

    MaterialPBRBase::MaterialPBRBase( std::string&& debugName, std::string&& shaderName, const PBRMaterialData& data )
         : Material( std::move( debugName ), std::move( shaderName ) ), m_RuntimeData( data )
    {
    }

    void MaterialPBRBase::UpdateCamera( MaterialInstance* instance, const Core::Camera* camera )
    {
        if ( !camera ) return;
        static ShaderProtocols::Camera CameraUB;
        CameraUB.View       = camera->GetViewMatrix();
        CameraUB.Projection = camera->GetProjectionMatrix();
        CameraUB.CameraPos  = camera->GetPosition();

        instance->GetParentMaterial()->Get<UniformBufferProperty>( CameraUB.Name )
             ->SetRawData( (std::byte*)&CameraUB, sizeof( CameraUB ) );
    }

    void MaterialPBRBase::UpdateLights( MaterialInstance* instance, const ShaderProtocols::PointLight& pointLights,
                                       const ShaderProtocols::DirectionLight& dirLights )
    {
        UpdatePointLights( instance, pointLights );
        UpdateDirectionLights( instance, dirLights );
        UpdateLightsMetadata( instance, pointLights, dirLights );
    }

    void MaterialPBRBase::UpdatePointLights( MaterialInstance* instance, const ShaderProtocols::PointLight& lights )
    {
        if ( lights.PointLights.empty() )
        {
            return;
        }
        instance->GetParentMaterial()->Get<UniformBufferProperty>( lights.Name )
             ->SetRawData( (std::byte*)lights.PointLights.data(),
                           lights.PointLights.size() * sizeof( ShaderProtocols::PointLightPayload ) );
    }

    void MaterialPBRBase::UpdateDirectionLights( MaterialInstance*                     instance,
                                                 const ShaderProtocols::DirectionLight& lights )
    {
        if ( lights.DirectionLights.empty() )
        {
            return;
        }
        instance->GetParentMaterial()->Get<UniformBufferProperty>( lights.Name )
             ->SetRawData( (std::byte*)lights.DirectionLights.data(),
                           lights.DirectionLights.size() * sizeof( ShaderProtocols::DirectionLightPayload ) );
    }

    void MaterialPBRBase::UpdateLightsMetadata( MaterialInstance* instance, const ShaderProtocols::PointLight& point,
                                                const ShaderProtocols::DirectionLight& dir )
    {
        static ShaderProtocols::LightsMetadata LightsMetadataUB;

        LightsMetadataUB.DirectionLightsCount = dir.DirectionLights.size();
        LightsMetadataUB.PointLightsCount     = point.PointLights.size();

        instance->GetParentMaterial()->Get<UniformBufferProperty>( LightsMetadataUB.Name )
             ->SetRawData( (std::byte*)&LightsMetadataUB, sizeof( LightsMetadataUB ) );
    }

    void MaterialPBRBase::UpdatePBRTextures( MaterialInstance* instance, const ShaderProtocols::PBRTexturesUB& textures )
    {
    }

    void MaterialPBRBase::UpdatePBRMaterial( MaterialInstance*                          instance,
                                             const ShaderProtocols::PBRMeshMaterialsUB& meshUB )
    {
    }

} // namespace Desert::Graphic
