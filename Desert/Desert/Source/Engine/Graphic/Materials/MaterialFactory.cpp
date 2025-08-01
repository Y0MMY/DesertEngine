#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <Engine/Assets/Mapper.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<Desert::Graphic::MaterialPBR>
    MaterialFactory::CreatePBR( const std::shared_ptr<Assets::MaterialAsset>& baseAsset )
    {
        return std::make_shared<MaterialPBR>( baseAsset );
    }

    std::shared_ptr<Desert::Graphic::MaterialSkybox>
    MaterialFactory::CreateSkybox( const std::shared_ptr<Assets::TextureAsset>& baseAsset )
    {
        return {};//std::make_shared<MaterialSkybox>(baseAsset);
    }

} // namespace Desert::Graphic