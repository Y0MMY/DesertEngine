#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

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

        // Static helper methods for common PBR operations
        static void SetAlbedo( MaterialInstance* instance, const Image2D* texture,
                               const glm::vec3& color = glm::vec3( 1.0f ) );
        static void SetNormalMap( MaterialInstance* instance, const Image2D* texture );
        static void SetMetallic( MaterialInstance* instance, float value, const Image2D* texture = nullptr );
        static void SetRoughness( MaterialInstance* instance, float value, const Image2D* texture = nullptr );
        static void SetAmbientOcclusion( MaterialInstance* instance, const Image2D* texture );
        static void SetEmissive( MaterialInstance* instance, const Image2D* texture, float intensity = 1.0f );

        // Runtime data update (Stateless: writes to instance)
        static void UpdateTransform( MaterialInstance* instance, const glm::mat4& transform );
        static void UpdateCamera( MaterialInstance* instance, const Core::Camera* camera );
        static void UpdateLights( MaterialInstance* instance, const ShaderProtocols::PointLight& pointLights,
                                  const ShaderProtocols::DirectionLight& dirLights );

    protected:
        void OnBind( MaterialInstance* instance ) override;

    private:
        struct PBRUniforms
        {
            glm::vec3 AlbedoColor;
            float     AlbedoBlend;
            float     MetallicValue;
            float     MetallicBlend;
            float     RoughnessValue;
            float     RoughnessBlend;
            glm::vec3 EmissionColor;
            float     EmissionStrength;
            float     AOValue;
        };
    };
} // namespace Desert::Graphic