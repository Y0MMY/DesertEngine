#pragma once

#include <Engine/Assets/Mesh/MaterialAsset.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Graphic/Materials/Models/Global.hpp>
#include <Engine/Graphic/Materials/Models/PBR/PBRTextures.hpp>
#include <Engine/Graphic/Materials/Models/PBR/MaterialPBRProperties.hpp>
#include <Engine/Graphic/Materials/Models/Lighting.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    // TODO: should override the base class, as we may want to support not only PBR
    class MaterialPBR
    {
    public:
        explicit MaterialPBR( const std::shared_ptr<Assets::MaterialAsset>& baseAsset );

        std::shared_ptr<Assets::MaterialAsset> GetBaseMaterial() const
        {
            if ( auto material = m_BaseMaterial.lock() )
            {
                return material;
            }
            return nullptr;
        }
        bool IsUsingBaseMaterial() const
        {
            return m_BaseMaterial.lock() != nullptr;
        }

        const auto& GetMaterial() const
        {
            return m_Material;
        }

        // Albedo properties
        void             SetAlbedo( const glm::vec3& color, float textureBlend = 1.0f );
        const glm::vec3& GetAlbedoColor() const
        {
            return m_AlbedoColor;
        }
        float GetAlbedoBlend() const
        {
            return m_AlbedoBlend;
        }

        // Metallic properties
        void  SetMetallic( float value, float textureBlend = 1.0f );
        float GetMetallicValue() const
        {
            return m_MetallicValue;
        }
        float GetMetallicBlend() const
        {
            return m_MetallicBlend;
        }

        // Roughness properties
        void  SetRoughness( float value, float textureBlend = 1.0f );
        float GetRoughnessValue() const
        {
            return m_RoughnessValue;
        }
        float GetRoughnessBlend() const
        {
            return m_RoughnessBlend;
        }

        // Emission properties
        void             SetEmission( const glm::vec3& color, float strength = 1.0f );
        const glm::vec3& GetEmissionColor() const
        {
            return m_EmissionColor;
        }
        float GetEmissionStrength() const
        {
            return m_EmissionStrength;
        }

        // Ambient Occlusion properties
        void  SetAO( float value );
        float GetAOValue() const
        {
            return m_AOValue;
        }

        // Texture operations
        void SetNewTexture( Assets::TextureAsset::Type type, const Common::Filepath& path );
        void RemoveTexture( Assets::TextureAsset::Type type );
        bool HasTexture( Assets::TextureAsset::Type type ) const;
        std::shared_ptr<Assets::TextureAsset> GetTexture( Assets::TextureAsset::Type type ) const;

        // Combined texture access (checks both instance and base material)
        std::shared_ptr<Graphic::Texture2D> GetFinalTexture( Assets::TextureAsset::Type type ) const;
        bool                                HasFinalTexture( Assets::TextureAsset::Type type ) const;

        // Parameter updates
        void UpdateRenderParameters( const Core::Camera& camera, const glm::mat4& meshTransform,
                                     const glm::vec3&                               directionLight,
                                     const std::optional<Models::PBR::PBRTextures>& pbrTextures );
        bool IsDirty() const
        {
            return m_ParametersDirty;
        }

    private:
        // weak_ptr because AssetManager owns MaterialAsset
        // MaterialPBR only observes the base material
        std::weak_ptr<Assets::MaterialAsset> m_BaseMaterial;

        std::shared_ptr<MaterialExecutor> m_Material;

        // Material properties
        glm::vec3 m_AlbedoColor = glm::vec3( 0.8f );
        float     m_AlbedoBlend = 1.0f;

        float m_MetallicValue = 0.0f;
        float m_MetallicBlend = 1.0f;

        float m_RoughnessValue = 0.5f;
        float m_RoughnessBlend = 1.0f;

        glm::vec3 m_EmissionColor    = glm::vec3( 0.0f );
        float     m_EmissionStrength = 0.0f;

        float m_AOValue = 1.0f;

        bool m_ParametersDirty = false;

        // Helper methods
        void InheritBaseMaterialProperties();
        void MarkDirty()
        {
            m_ParametersDirty = true;
        }

    private:
        std::unique_ptr<Models::LightingData>               m_LightingData;
        std::unique_ptr<Models::GlobalData>                 m_GlobalUB;
        std::unique_ptr<Models::PBR::PBRMaterialTexture>    m_PBRTextures;
        std::unique_ptr<Models::PBR::MaterialPBRProperties> m_MaterialProperties;
    };
} // namespace Desert::Graphic