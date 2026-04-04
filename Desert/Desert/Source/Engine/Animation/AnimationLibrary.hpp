#pragma once

#include <Engine/Assets/Mesh/AnimationAsset.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <unordered_map>
#include <vector>

namespace Desert::Animation
{
    class AnimationLibrary
    {
    public:
        explicit AnimationLibrary( const Assets::AssetManager* assetManager );
        void Register( const Assets::Asset<Assets::AnimationAsset>& animation );
        void Unregister( const Assets::AssetHandle& handle );

        std::vector<Assets::Asset<Assets::AnimationAsset>> GetBySkeleton( uint64_t skeletonSignature ) const;

        void Clear();

    private:
        const Assets::AssetManager*                                    m_AssetManager;
        std::unordered_map<uint64_t, std::vector<Assets::AssetHandle>> m_Index;
    };
} // namespace Desert::Animation