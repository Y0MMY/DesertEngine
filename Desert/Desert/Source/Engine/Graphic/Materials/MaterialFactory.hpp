#pragma once

#include <Engine/Graphic/Materials/MaterialInstance.hpp>
#include <Engine/Assets/AssetManager.hpp>

namespace Desert::Graphic
{
    class MaterialFactory
    {
    public:
        /*static std::shared_ptr<MaterialInstance>
        CreateFromAsset( const Assets::Asset<Assets::MaterialAsset>& asset );*/

        static std::shared_ptr<MaterialInstance> Create( const std::shared_ptr<Assets::AssetManager>& assetManager,
                                                         const Common::Filepath&                      filepath );
    };
} // namespace Desert::Graphic