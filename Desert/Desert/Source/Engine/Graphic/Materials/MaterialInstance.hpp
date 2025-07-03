#pragma once

#include <Engine/Assets/Mesh/MaterialAsset.hpp>
#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    class MaterialInstance
    {
    public:
        explicit MaterialInstance( std::shared_ptr<Assets::MaterialAsset> baseAsset );

        // Base material operations
        void                                   SetBaseMaterial( std::shared_ptr<Assets::MaterialAsset> asset );
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
        void SetTexture( Assets::TextureAsset::Type type, std::shared_ptr<Assets::TextureAsset> texture );
        void RemoveTexture( Assets::TextureAsset::Type type );
        bool HasTexture( Assets::TextureAsset::Type type ) const;
        std::shared_ptr<Assets::TextureAsset> GetTexture( Assets::TextureAsset::Type type ) const;
        bool                                  HasAnyTexture() const
        {
            return !m_Textures.empty();
        }

        // Combined texture access (checks both instance and base material)
        std::shared_ptr<Graphic::Texture2D> GetFinalTexture( Assets::TextureAsset::Type type ) const;
        bool                                HasFinalTexture( Assets::TextureAsset::Type type ) const;

        // Parameter updates
        void UpdateRenderParameters( bool forceUpdate = false );
        bool IsDirty() const
        {
            return m_ParametersDirty;
        }

    private:
        // weak_ptr because AssetManager owns MaterialAsset
        // MaterialInstance only observes the base material
        std::weak_ptr<Assets::MaterialAsset> m_BaseMaterial;
        std::unordered_map<Assets::TextureAsset::Type, std::shared_ptr<Assets::TextureAsset>>
             m_Textures; // TODO: Move TextureAsset::Type to models

        std::shared_ptr<Material> m_Material;

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

        bool m_ParametersDirty = true;

        // Helper methods
        void InheritBaseMaterialProperties();
        void MarkDirty()
        {
            m_ParametersDirty = true;
        }
    };
} // namespace Desert::Graphic