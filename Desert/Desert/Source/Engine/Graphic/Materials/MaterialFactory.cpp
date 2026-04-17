#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <Engine/Assets/Mapper.hpp>

#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<Material> MaterialFactory::Create( const std::shared_ptr<Assets::MaterialAsset>& asset )
    {
        switch ( asset->GetMaterialType() )
        {
            case Assets::MaterialAsset::MaterialType::PBR:
            {
                return std::make_shared<StaticMaterialPBR>( asset );
            }

            case Assets::MaterialAsset::MaterialType::Skybox:
            {
              //  return std::make_shared<MaterialSkybox>( asset );
            }

            default:
            {
                return nullptr; // CreateDefault();
            }
        }

        return nullptr;
    }
} // namespace Desert::Graphic