#pragma once

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Graphic/Materials/Models/Skybox/Camera.hpp>
#include <Engine/Graphic/Materials/Models/Skybox/Skybox.hpp>

#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

namespace Desert::Graphic
{
    class MaterialSkybox
    {
    public:
        explicit MaterialSkybox( const std::shared_ptr<Assets::TextureAsset>& baseAsset );

        std::shared_ptr<Assets::TextureAsset> GetBaseMaterial() const
        {
            if ( auto material = m_BaseMaterial.lock() )
            {
                return material;
            }
            return nullptr;
        }

        const Environment& GetEnvironment( )
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
        void UpdateRenderParameters( const Core::Camera& camera );

        const auto& GetMaterialExecutor() const
        {
            return m_Material;
        }

    private:
        // weak_ptr because AssetManager owns MaterialAsset
        // MaterialPBR only observes the base material
        std::weak_ptr<Assets::TextureAsset> m_BaseMaterial;
        std::shared_ptr<MaterialExecutor>           m_Material;

        Environment m_Environment;

    private:
        std::unique_ptr<Models::CameraData> m_CameraModel;
        std::unique_ptr<Models::SkyboxData> m_SkyboxModel;
    };
} // namespace Desert::Graphic