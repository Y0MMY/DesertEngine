#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

#include <Engine/Assets/Skybox/SkyboxAsset.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/MaterialBindings/Skybox/Skybox.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    struct UpdateMaterialSkyboxInfo
    {
        Core::Camera* Camera;
    };

    class MaterialSkybox final : public Material
    {
    public:
        explicit MaterialSkybox( const std::shared_ptr<Assets::SkyboxAsset>& baseAsset );

        std::shared_ptr<Assets::SkyboxAsset> GetBaseMaterial() const
        {
            if ( auto material = m_BaseMaterial.lock() )
            {
                return material;
            }
            return nullptr;
        }

        const Environment& GetEnvironment()
        {
            return m_Environment;
        }

        bool IsUsingBaseMaterial() const
        {
            return m_BaseMaterial.lock() != nullptr;
        }

        bool IsReady() const
        {
            if ( const auto& baseSkyboxAsset = m_BaseMaterial.lock() )
            {
                return baseSkyboxAsset->IsReadyForUse();
            }
            return false;
        }

        // Parameter updates
        void Bind( const UpdateMaterialSkyboxInfo& data );

    private:
        // weak_ptr because AssetManager owns MaterialAsset
        // MaterialPBR only observes the base material
        std::weak_ptr<Assets::SkyboxAsset> m_BaseMaterial;
        std::shared_ptr<MaterialExecutor>  m_Material;

        Environment m_Environment;

    private:
        std::unique_ptr<MaterialHelper::SkyboxDataBinding> m_SkyboxBinding;
    };
} // namespace Desert::Graphic