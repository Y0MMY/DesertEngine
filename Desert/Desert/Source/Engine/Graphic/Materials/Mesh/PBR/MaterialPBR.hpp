#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

#include <Engine/Assets/Mesh/MaterialAsset.hpp>

#include <Engine/Graphic/Materials/Models/Common/Camera.hpp>
#include <Engine/Graphic/Materials/Models/Mesh/PBR/PBRTextures.hpp>
#include <Engine/Graphic/Materials/Models/Mesh/PBR/MaterialPBRUB.hpp>
#include <Engine/Graphic/Materials/Models/Light/DirectionLight.hpp>
#include <Engine/Graphic/Materials/Models/Light/PointLight.hpp>
#include <Engine/Graphic/Materials/Models/Light/LightMetaData.hpp>

#include <Engine/Graphic/Models/PointLight.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    struct UpdateMaterialPBRInfo
    {
        std::shared_ptr<Core::Camera>           Camera;
        glm::mat4                               MeshTransform;
        std::vector<DirectionLight>             DirectionLights;
        std::optional<Models::PBR::PBRTextures> PbrTextures;
        std::vector<PointLight>                 PointLights;
    };

    // TODO: should override the base class, as we may want to support not only PBR
    class MaterialPBR final : public Material
    {
    public:
        MaterialPBR() = default;

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

        // Albedo properties
        void             SetAlbedo( const glm::vec3& color, float textureBlend = 1.0f );
        const glm::vec3& GetAlbedoColor() const
        {
            return m_MaterialProperties->AlbedoColor;
        }
        float GetAlbedoBlend() const
        {
            return m_MaterialProperties->AlbedoBlend;
        }

        // Metallic properties
        void  SetMetallic( float value, float textureBlend = 1.0f );
        float GetMetallicValue() const
        {
            return 0.0;
        }
        float GetMetallicBlend() const
        {
            return 0.0;
        }

        // Roughness properties
        void  SetRoughness( float value, float textureBlend = 1.0f );
        float GetRoughnessValue() const
        {
            return 0.0;
        }
        float GetRoughnessBlend() const
        {
            return 0.0;
        }

        // Emission properties
        void             SetEmission( const glm::vec3& color, float strength = 1.0f );
        const glm::vec3& GetEmissionColor() const
        {
            static glm::vec3 dummy{};
            return dummy;
        }
        float GetEmissionStrength() const
        {
            return 0.0;
        }

        // Ambient Occlusion properties
        void  SetAO( float value );
        float GetAOValue() const
        {
            return 0.0;
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
        void Bind( const UpdateMaterialPBRInfo& data );
        bool IsDirty() const
        {
            return m_ParametersDirty;
        }

       template <typename Visitor> void VisitUniformFields(Visitor&& visitor) const {
            auto visit_impl = [&](const auto& field_ptr, const char* field_name) { if (field_ptr) {
                field_ptr->for_each_field([&](const auto& display_name, auto& value) { visitor(field_name, display_name, value); });
            } }; visit_impl(m_MaterialProperties, "m_MaterialProperties");
        };

    private:
        void InitializeUniforms()
        {
            InitializeUniformBuffer<Models::Light::PointLightsUB>();
            InitializeUniformBuffer<Models::Light::DirectionLightsUB>();
            InitializeUniformBuffer<Models::Light::LightsMetadata>();
            InitializeUniformBuffer<Models::CameraDataUB>();
            InitializeUniformBuffer<Models::PBR::PBRTextures>();
            InitializeUniformBuffer<Models::PBR::PBRMaterialPropertiesUB>();
        }

    private:
        // weak_ptr because AssetManager owns MaterialAsset
        // MaterialPBR only observes the base material
        std::weak_ptr<Assets::MaterialAsset> m_BaseMaterial;
    
        // Helper methods
        void InheritBaseMaterialProperties();
        void MarkDirty()
        {
            m_ParametersDirty = true;
        }

    private:
        void UpdatePointLight( const std::vector<PointLight>& pointLights );
        void UpdateCamera( const Core::Camera* pointLights );
        void UpdateDirectionLight( const std::vector<DirectionLight>& directionLights );
        void UpdateLightsMetadata( const std::vector<PointLight>&     pointLights,
                                   const std::vector<DirectionLight>& directionLights );

        void UpdatePBRTextures( const std::optional<Models::PBR::PBRTextures>& pbrTextures );

    private:
        std::unique_ptr<Models::Light::PointLightsUB>         m_PointLightUB;
        std::unique_ptr<Models::Light::DirectionLightsUB>     m_DirectionLightUB;
        std::unique_ptr<Models::Light::LightsMetadata>        m_LightsMetadataUB;
        std::unique_ptr<Models::CameraDataUB>                 m_CameraData;
        std::unique_ptr<Models::PBR::PBRTextures>             m_PBRTextures;
        std::unique_ptr<Models::PBR::PBRMaterialPropertiesUB> m_MaterialProperties;
    };
} // namespace Desert::Graphic