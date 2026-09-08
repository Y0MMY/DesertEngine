#pragma once

#include <Engine/Assets/Mesh/AnimationAsset.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Desert::Animation
{
    /**
     * @brief WHICH CLIPS FIT WHICH RIG — an index of animation assets by the skeleton they animate.
     *
     * IT HANDS OUT ASSETS WITHOUT LOADING THEM, and that is why eviction had to be taught about it. The
     * index is built at Register time from properties READ OUT OF THE CLIP (its skeleton signature, its
     * animated bone names), and both lookups then resolve a handle straight through the AssetManager. An
     * evicted clip would come back from here as a perfectly valid asset holding an empty track list, and
     * the character would silently T-pose — §1.4's empty successful answer, at the seam where it is
     * hardest to see. Both lookups therefore RELOAD before returning; see GetBySkeleton.
     */
    class AnimationLibrary
    {
    public:
        // NON-CONST because the lookups reload an evicted clip before handing it out, and
        // AssetBase::EnsureLoaded needs a manager it can resolve dependencies against. It was const while
        // nothing ever released an asset.
        explicit AnimationLibrary( Assets::AssetManager* assetManager );
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
        /// The handle -> asset step both lookups share, INCLUDING the reload. One place, so a third lookup
        /// cannot be written that forgets it.
        [[nodiscard]] Assets::Asset<Assets::AnimationAsset> Resolve( const Assets::AssetHandle& handle ) const;

        // Non-owning, and it outlives nothing: the library is destroyed with the editor layer that made
        // it, and the manager with the project. A project switch that replaced the manager without
        // replacing this would leave it dangling — nothing does that today, and nothing checks either.
        Assets::AssetManager*                                          m_AssetManager;
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