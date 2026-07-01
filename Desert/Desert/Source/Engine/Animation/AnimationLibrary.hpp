#pragma once

#include <Engine/Assets/Mesh/AnimationAsset.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Desert::Animation
{
    class AnimationLibrary
    {
    public:
        explicit AnimationLibrary( const Assets::AssetManager* assetManager );
        void Register( const Assets::Asset<Assets::AnimationAsset>& animation );
        void Unregister( const Assets::AssetHandle& handle );

        // Exact-signature lookup (e.g. code-generated clips that share a skeleton signature exactly).
        std::vector<Assets::Asset<Assets::AnimationAsset>> GetBySkeleton( uint64_t skeletonSignature ) const;

        // Tolerant lookup by BONE NAME: returns every animation whose animated bones are (mostly) a SUBSET of
        // this skeleton's bones. Needed because the SAME rig exported with a skin (character: includes leaf/end
        // bones) vs without (Mixamo animation: only the animated bones) has different bone SETS and therefore
        // different signatures — but the animation still drives the character (playback binds by name). Pass the
        // character skeleton's bone-name set.
        std::vector<Assets::Asset<Assets::AnimationAsset>>
        GetForSkeletonBones( const std::unordered_set<std::string>& skeletonBones ) const;

        void Clear();

    private:
        const Assets::AssetManager*                                    m_AssetManager;
        std::unordered_map<uint64_t, std::vector<Assets::AssetHandle>> m_Index;

        // Per-animation animated-bone names, for the tolerant GetForSkeletonBones subset match.
        struct AnimBones
        {
            Assets::AssetHandle      Handle;
            std::vector<std::string> Bones;
        };
        std::vector<AnimBones> m_Anims;
    };
} // namespace Desert::Animation