#pragma once

#include <Engine/Animation/ClipSkeletonMatch.hpp>
#include <Engine/Assets/Mesh/AnimationAsset.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <string>
#include <vector>

namespace Desert::Animation
{
    /**
     * @brief WHICH CLIPS FIT WHICH RIG — an index of animation assets by the skeleton they animate.
     *
     * IT HANDS OUT ASSETS WITHOUT LOADING THEM, and that is why eviction had to be taught about it. The
     * index is built at Register time from properties READ OUT OF THE CLIP (its skeleton signature, its
     * animated bone names), and the lookup then resolves a handle straight through the AssetManager. An
     * evicted clip would come back from here as a perfectly valid asset holding an empty track list, and
     * the character would silently T-pose — §1.4's empty successful answer, at the seam where it is
     * hardest to see. The lookup therefore RELOADS before returning; see Resolve.
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

        // THE ONLY LOOKUP. It replaced a pair — an exact-signature one used by the runtime and a tolerant
        // bone-name one used by the editor's pickers — whose disagreement is described in
        // ClipSkeletonMatch.hpp. Both of those spellings are gone: with two of them in the tree, a caller
        // chose a semantics by choosing a function name, and nothing made the choices agree.
        std::vector<Assets::Asset<Assets::AnimationAsset>> GetForSkeleton( const Skeleton& skeleton ) const;

        // The single-clip form the runtime needs, over the SAME rule and the same records. It reports WHY it
        // found nothing, naming the clip, the rig and what the rig does have — the caller cannot turn a
        // refusal into a state that quietly plays nothing without discarding a message first.
        [[nodiscard]] Common::ResultStr<Assets::Asset<Assets::AnimationAsset>>
        FindForSkeleton( const Skeleton& skeleton, const std::string& clipName ) const;

        void Clear();

    private:
        /// The handle -> asset step both lookups share, INCLUDING the reload. One place, so a third lookup
        /// cannot be written that forgets it.
        [[nodiscard]] Assets::Asset<Assets::AnimationAsset> Resolve( const Assets::AssetHandle& handle ) const;

        // Non-owning, and it outlives nothing: the library is destroyed with the editor layer that made
        // it, and the manager with the project. A project switch that replaced the manager without
        // replacing this would leave it dangling — nothing does that today, and nothing checks either.
        Assets::AssetManager* m_AssetManager;

        // ONE record per registered clip, holding everything the match rule is allowed to look at. There used
        // to be two indexes of the same clips — a signature map and this list — and Clear() emptied only one
        // of them, so the library went on offering the previous project's clips out of the half nobody
        // remembered. A single container cannot fall out of step with itself.
        std::vector<ClipRigIdentity> m_Clips;
    };
} // namespace Desert::Animation