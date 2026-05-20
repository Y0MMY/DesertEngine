#pragma once

#include <Engine/Assets/MaterialAsset.hpp>

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    class MaterialFactory
    {
    public:
        // Create material from asset (pure material, no instance data)
        static std::shared_ptr<Material> CreateMaterial( const Assets::MaterialAsset* asset );

        // Create material instance from asset (includes resolved textures)
        static std::shared_ptr<MaterialInstance> CreateMaterialInstance( const Assets::MaterialAsset* asset,
                                                                         const std::string& instanceName = "" );

        // Create default materials
        static std::shared_ptr<Material> CreateDefaultPBRMaterial();

        // Create primitive materials (for editor)
        static std::shared_ptr<MaterialInstance> CreatePrimitiveMaterial( const std::string& name,
                                                                          const glm::vec3&   color,
                                                                          float              metallic  = 0.0f,
                                                                          float              roughness = 0.5f );

        // static std::shared_ptr<Material> Create( const std::shared_ptr<Assets::MaterialAsset>& asset );
    };
} // namespace Desert::Graphic