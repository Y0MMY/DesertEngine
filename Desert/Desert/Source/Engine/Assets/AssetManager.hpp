#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Assets
{
    class AssetManager final
    {
    public:
        template <typename AssetType>
        Asset<AssetType> CreateAsset( const AssetPriority priority, const Common::Filepath& filepath )
        {
            return std::make_shared<AssetType>( priority, filepath );
        }
    };
} // namespace Desert::Assets

#define ASSET_MANAGER_ACCESS() friend class Desert::Assets::AssetManager;