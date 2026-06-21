#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/Properties/TProperty.hpp>

#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/DirectionLight.hpp>
#include <Engine/Graphic/ShaderProtocols/PBRTextures.hpp>
#include <Engine/Graphic/ShaderProtocols/PBRMeshMaterials.hpp>

namespace Desert::Graphic
{
    class StaticMaterialPBR : public Material
    {
    public:
        StaticMaterialPBR();
        ~StaticMaterialPBR() override = default;

        void Bind( const MaterialInstance* instance ) override;

        // Typed PBR properties — auto-registered at construction; available to editor via GetRegisteredProperties()
        MPROPERTY( glm::vec3, AlbedoColor,      "AlbedoColor",      glm::vec3( 1.0f ) )
        MPROPERTY( float,     AlbedoBlend,       "AlbedoBlend",      1.0f )
        MPROPERTY( float,     MetallicValue,     "MetallicValue",    0.0f )
        MPROPERTY( float,     MetallicBlend,     "MetallicBlend",    1.0f )
        MPROPERTY( float,     RoughnessValue,    "RoughnessValue",   0.5f )
        MPROPERTY( float,     RoughnessBlend,    "RoughnessBlend",   1.0f )
        MPROPERTY( glm::vec3, EmissionColor,     "EmissionColor",    glm::vec3( 0.0f ) )
        MPROPERTY( float,     EmissionStrength,  "EmissionStrength", 1.0f )
        MPROPERTY( float,     AOValue,           "AOValue",          1.0f )

        MTEXTURE_PROPERTY( AlbedoTexture, "u_AlbedoTexture" )
        MTEXTURE_PROPERTY( NormalTexture, "u_NormalTexture" )

        // Static helpers — write per-instance overrides into a MaterialInstance
        static void SetAlbedo( MaterialInstance* instance, const Image2D* texture,
                               const glm::vec3& color = glm::vec3( 1.0f ) );
        static void SetNormalMap( MaterialInstance* instance, const Image2D* texture );
        static void SetMetallic( MaterialInstance* instance, float value, const Image2D* texture = nullptr );
        static void SetRoughness( MaterialInstance* instance, float value, const Image2D* texture = nullptr );
        static void SetAmbientOcclusion( MaterialInstance* instance, const Image2D* texture );
        static void SetEmissive( MaterialInstance* instance, const Image2D* texture, float intensity = 1.0f );

        static void UpdateTransform( MaterialInstance* instance, const glm::mat4& transform );
        static void UpdateCamera( MaterialInstance* instance, const Core::Camera* camera );
        static void UpdateLights( MaterialInstance* instance, const ShaderProtocols::PointLight& pointLights,
                                  const ShaderProtocols::DirectionLight& dirLights );

    protected:
        void OnBind( MaterialInstance* instance ) override;
    };
} // namespace Desert::Graphic
