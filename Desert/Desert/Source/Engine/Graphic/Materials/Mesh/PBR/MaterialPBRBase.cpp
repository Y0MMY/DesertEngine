#include "MaterialPBRBase.hpp"

#include <Engine/Assets/Mapper.hpp>

#include <Engine/Graphic/ShaderProtocols/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/Metadata.hpp>
#include <Engine/Graphic/Materials/Properties/Texture2DProperty.hpp>
#include <Engine/Graphic/Materials/Properties/TextureCubeProperty.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic
{

    MaterialPBRBase::MaterialPBRBase( std::string&& debugName, std::string&& shaderName )
         : Material( std::move( debugName ), std::move( shaderName ) )
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

    void MaterialPBRBase::UpdateShadow( MaterialInstance* instance, const glm::mat4* cascadeViewProj,
                                        Image2D* const* cascadeMaps, uint32_t numCascades, float bias,
                                        bool enabled, int debugMode, bool showNormals )
    {
        // Matches ShadowUB in PBR.glsl.frag: mat4 u_LightViewProj[4]; vec4 u_ShadowParams; vec4 u_DebugParams.
        constexpr uint32_t kMaxCascades = 4;
        struct ShadowUBData
        {
            glm::mat4 LightViewProj[kMaxCascades];
            glm::vec4 Params;      // x = bias, y = enabled, z = debug mode, w = cascade count
            glm::vec4 DebugParams; // x = show normals
        } data;

        const uint32_t n = numCascades < kMaxCascades ? numCascades : kMaxCascades;
        for ( uint32_t i = 0; i < kMaxCascades; ++i )
            data.LightViewProj[i] = ( i < n ) ? cascadeViewProj[i] : glm::mat4( 1.0f );
        data.Params = glm::vec4( bias, enabled ? 1.0f : 0.0f, static_cast<float>( debugMode ),
                                 static_cast<float>( n ) );
        data.DebugParams = glm::vec4( showNormals ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f );

        auto* parent = instance->GetParentMaterial();
        if ( auto* ub = parent->Get<UniformBufferProperty>( "ShadowUB" ) )
            ub->SetRawData( reinterpret_cast<const std::byte*>( &data ), sizeof( data ) );

        // Bind every cascade map (descriptors must stay valid each frame). Names u_ShadowMap0..3.
        static const char* kNames[kMaxCascades] = { "u_ShadowMap0", "u_ShadowMap1", "u_ShadowMap2",
                                                    "u_ShadowMap3" };
        for ( uint32_t i = 0; i < kMaxCascades; ++i )
        {
            Image2D* img = ( i < n ) ? cascadeMaps[i] : nullptr;
            if ( img )
                if ( auto* tex = parent->Get<Texture2DProperty>( kNames[i] ) )
                    tex->SetImage( img );
        }
    }

    void MaterialPBRBase::UpdateEnvironment( MaterialInstance* instance, ImageCube* irradiance,
                                             ImageCube* prefiltered, Image2D* brdfLut )
    {
        auto* parent = instance->GetParentMaterial();
        if ( irradiance )
            if ( auto* tex = parent->Get<TextureCubeProperty>( "u_EnvIrradianceTex" ) )
                tex->SetTexture( irradiance );
        if ( prefiltered )
            if ( auto* tex = parent->Get<TextureCubeProperty>( "u_EnvSpecularTex" ) )
                tex->SetTexture( prefiltered );
        if ( brdfLut )
            if ( auto* tex = parent->Get<Texture2DProperty>( "u_BRDFLUTTexture" ) )
                tex->SetImage( brdfLut );
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

} // namespace Desert::Graphic
