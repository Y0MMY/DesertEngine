#include "MaterialPBRBase.hpp"

#include <Engine/Assets/Mapper.hpp>

#include <Engine/Graphic/Clouds/CloudShadowBinding.hpp>
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
                                       const ShaderProtocols::SpotLight&      spotLights,
                                       const ShaderProtocols::DirectionLight& dirLights )
    {
        UpdatePointLights( instance, pointLights );
        UpdateSpotLights( instance, spotLights );
        UpdateDirectionLights( instance, dirLights );
        UpdateLightsMetadata( instance, pointLights, spotLights, dirLights );
    }

    void MaterialPBRBase::UpdatePointLights( MaterialInstance* instance, const ShaderProtocols::PointLight& lights )
    {
        // Point lights live in an unbounded std430 storage buffer (no MAX_POINT_LIGHT cap). Empty is fine —
        // the shader loops 0..PointLightCount, so a stale buffer is simply never read.
        if ( lights.PointLights.empty() )
        {
            return;
        }
        if ( auto* sb = instance->GetParentMaterial()->Get<StorageBufferProperty>( lights.Name ) )
            sb->SetRawData( (std::byte*)lights.PointLights.data(),
                            static_cast<uint32_t>( lights.PointLights.size() *
                                                   sizeof( ShaderProtocols::PointLightPayload ) ) );
    }

    void MaterialPBRBase::UpdateSpotLights( MaterialInstance* instance, const ShaderProtocols::SpotLight& lights )
    {
        if ( lights.SpotLights.empty() )
        {
            return;
        }
        if ( auto* sb = instance->GetParentMaterial()->Get<StorageBufferProperty>( lights.Name ) )
            sb->SetRawData( (std::byte*)lights.SpotLights.data(),
                            static_cast<uint32_t>( lights.SpotLights.size() *
                                                   sizeof( ShaderProtocols::SpotLightPayload ) ) );
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
                                        bool enabled, int debugMode, bool showNormals,
                                        const glm::vec4& cascadeWorldPerTexel, bool lightingDebug )
    {
        // Matches ShadowUB in PBR.glsl.frag.
        constexpr uint32_t kMaxCascades = 4;
        struct ShadowUBData
        {
            glm::mat4 LightViewProj[kMaxCascades];
            glm::vec4 Params;           // x = bias, y = enabled, z = debug mode, w = cascade count
            glm::vec4 DebugParams;      // x = show normals
            glm::vec4 CascadeTexelWorld; // per-cascade world size of one shadow-map texel
        } data;

        const uint32_t n = numCascades < kMaxCascades ? numCascades : kMaxCascades;
        for ( uint32_t i = 0; i < kMaxCascades; ++i )
            data.LightViewProj[i] = ( i < n ) ? cascadeViewProj[i] : glm::mat4( 1.0f );
        data.Params = glm::vec4( bias, enabled ? 1.0f : 0.0f, static_cast<float>( debugMode ),
                                 static_cast<float>( n ) );
        data.DebugParams = glm::vec4( showNormals ? 1.0f : 0.0f, lightingDebug ? 1.0f : 0.0f, 0.0f, 0.0f );
        data.CascadeTexelWorld = cascadeWorldPerTexel;

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

    void MaterialPBRBase::UpdateCloudShadow( MaterialInstance* instance, const CloudShadowInput& cloudShadow )
    {
        if ( !instance )
            return;
        CloudShadowBind( instance->GetParentMaterial(), cloudShadow );
    }

    void MaterialPBRBase::UpdateLightsMetadata( MaterialInstance* instance, const ShaderProtocols::PointLight& point,
                                                const ShaderProtocols::SpotLight&      spot,
                                                const ShaderProtocols::DirectionLight& dir )
    {
        ShaderProtocols::LightsMetadata LightsMetadataUB;

        LightsMetadataUB.DirectionLightsCount = static_cast<uint32_t>( dir.DirectionLights.size() );
        LightsMetadataUB.PointLightsCount     = static_cast<uint32_t>( point.PointLights.size() );
        LightsMetadataUB.SpotLightsCount      = static_cast<uint32_t>( spot.SpotLights.size() );

        // `Name` is a static member (not in the object), so the struct is just the three uint counts.
        const uint32_t counts[3] = { LightsMetadataUB.DirectionLightsCount, LightsMetadataUB.PointLightsCount,
                                     LightsMetadataUB.SpotLightsCount };
        instance->GetParentMaterial()->Get<UniformBufferProperty>( LightsMetadataUB.Name )
             ->SetRawData( (std::byte*)counts, sizeof( counts ) );
    }

} // namespace Desert::Graphic
