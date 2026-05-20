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

        // Runtime data update (these will be called per frame)
        void UpdateCamera( const Core::Camera* camera );
        void UpdateLights( const ShaderProtocols::PointLight&     pointLights,
                           const ShaderProtocols::DirectionLight& dirLights );
        void UpdateTransform( const glm::mat4& modelMatrix );

    protected:
        void OnBind( MaterialInstance* instance ) override;

    private:
        struct PBRUniforms
        {
            glm::vec3 AlbedoColor         = glm::vec3( 1.0f );
            float     MetallicFactor      = 0.0f;
            float     RoughnessFactor     = 0.5f;
            float     EmissiveIntensity   = 1.0f;
            int       UseAlbedoTexture    = 0;
            int       UseNormalTexture    = 0;
            int       UseMetallicTexture  = 0;
            int       UseRoughnessTexture = 0;
            int       UseAOTexture        = 0;
            int       UseEmissiveTexture  = 0;
        };

        PBRUniforms m_CurrentUniforms;
        bool        m_UniformsDirty   = true;
        glm::mat4   m_TransformMatrix = glm::mat4( 1.0f );

        // Cached texture pointers
        const Image2D* m_AlbedoTexture    = nullptr;
        const Image2D* m_NormalTexture    = nullptr;
        const Image2D* m_MetallicTexture  = nullptr;
        const Image2D* m_RoughnessTexture = nullptr;
        const Image2D* m_AOTexture        = nullptr;
        const Image2D* m_EmissiveTexture  = nullptr;
    };
} // namespace Desert::Graphic