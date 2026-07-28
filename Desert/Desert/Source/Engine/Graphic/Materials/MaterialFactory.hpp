#pragma once

#include <Engine/Assets/MaterialAsset.hpp>

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Assets
{
    class SurfaceMaterialAsset;
}

namespace Desert::Graphic
{
    class StaticMaterialPBR;
    class DataDrivenMaterial;

    class MaterialFactory
    {
    public:
        // Create material from asset (pure material, no instance data)
        static std::shared_ptr<Material> CreateMaterial( const Assets::MaterialAsset* asset );

        // Copy a PBR asset's reflected data into a live runtime material and (re)bind its textures.
        // Used by CreateMaterial and by the editor for live edit -> viewport sync.
        static void ApplyPBRAsset( StaticMaterialPBR& material, const Assets::SurfaceMaterialAsset& asset );

        // Apply a custom-shader material asset (ShaderName/ShaderParams/ShaderTextures) onto its
        // runtime DataDrivenMaterial. Counterpart of ApplyPBRAsset for the generic path.
        static void ApplyShaderAsset( DataDrivenMaterial& material, const Assets::SurfaceMaterialAsset& asset );

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