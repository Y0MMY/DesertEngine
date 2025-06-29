#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <Engine/Assets/Mapper.hpp>

namespace Desert::Graphic
{
    std::unique_ptr<Desert::Graphic::MaterialInstance>
    MaterialFactory::Create( const std::shared_ptr<Assets::MaterialAsset>& baseAsset )
    {
        return std::make_unique<MaterialInstance>( baseAsset );
    }

} // namespace Desert::Graphic