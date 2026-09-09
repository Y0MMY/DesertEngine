#pragma once

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/ResultStr.hpp>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace Desert::Animation
{
    class Skeleton;

    /**
     * @brief THE ONE DEFINITION OF "THIS CLIP DRIVES THIS RIG", and the reason it is a file of its own.
     *
     * There used to be two, and they disagreed. The editor's clip pickers asked TOLERANTLY (are this clip's
     * animated bones mostly present in this skeleton?) while the runtime asked EXACTLY (does the clip's
     * recorded skeleton signature equal this skeleton's?). Both are defensible in isolation, which is why
     * both survived review; their RELATION was the defect. For Mixamo content the two answers differ by
     * construction — a rig exported WITH a skin carries the leaf/end bones the animation-only export leaves
     * out, so the signatures differ while the names still line up — and an artist could therefore pick a clip
     * in the AnimGraph editor that the state machine could never resolve. The state played nothing, silently.
     *
     * Both sides now call the functions below, and `Desert/Tests/Engine/ClipSkeletonMatch` asserts their
     * AGREEMENT rather than each of them separately: every clip the picker offers must be resolvable by name
     * through the runtime path, on a fixture where the tolerant and exact answers provably differ.
     */

    /// Everything the match rule may look at on the clip side. The library records this once, at Register.
    struct ClipRigIdentity
    {
        Common::AssetHandle Handle;
        std::string         ClipName;

        /// 0 = "no rig claimed" (AnimationClip::SkeletonSignature), never matched against a real rig.
        uint64_t SkeletonSignature = 0;

        /// Names of the bones this clip actually animates (tracks with a non-empty bone name).
        std::vector<std::string> AnimatedBones;
    };

    /// The rig side, derived in ONE place so two callers cannot derive it two ways.
    struct RigIdentity
    {
        uint64_t                        Signature = 0;
        std::unordered_set<std::string> BoneNames;
    };

    [[nodiscard]] RigIdentity IdentifyRig( const Skeleton& skeleton );

    /**
     * @brief Does this clip drive this rig? Either kind of evidence is enough, and that union is the fix.
     *
     * - the clip names this exact rig (signature equality), or
     * - a majority of the bones the clip animates exist on this rig by name — which is what playback
     *   actually binds on (Animator::ResolveTrack), so it is the honest general rule. A majority rather
     *   than "all" lets a partial-body or upper-body clip through while still rejecting an unrelated rig.
     *
     * Requiring BOTH is what made the runtime blind to every Mixamo clip; requiring either is the union of
     * the two rules that were already in the tree, so no clip that resolved before stops resolving now.
     */
    [[nodiscard]] bool ClipDrivesRig( const ClipRigIdentity& clip, const RigIdentity& rig );

    /// The PICKER side: every clip that drives this rig, as indices into `clips`.
    [[nodiscard]] std::vector<size_t> SelectClipsForRig( const std::vector<ClipRigIdentity>& clips,
                                                         const RigIdentity&                  rig );

    /**
     * @brief The RUNTIME side: the one clip a state names. Same predicate, and a NAMED refusal when there is
     *        none — the silence was half the defect, because an AnimGraph state whose clip does not resolve
     *        simply plays nothing and looks to an artist like a frozen character.
     */
    [[nodiscard]] Common::ResultStr<size_t> FindClipForRig( const std::vector<ClipRigIdentity>& clips,
                                                            const RigIdentity& rig, const std::string& clipName );
} // namespace Desert::Animation
