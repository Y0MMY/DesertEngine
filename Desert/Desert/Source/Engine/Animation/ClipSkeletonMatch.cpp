#include "ClipSkeletonMatch.hpp"

#include <Engine/Animation/Skeleton.hpp>

namespace Desert::Animation
{
    RigIdentity IdentifyRig( const Skeleton& skeleton )
    {
        RigIdentity rig;
        rig.Signature = skeleton.GetSignature();
        rig.BoneNames.reserve( skeleton.GetBones().size() );
        for ( const auto& bone : skeleton.GetBones() )
            rig.BoneNames.insert( bone.Name );
        return rig;
    }

    bool ClipDrivesRig( const ClipRigIdentity& clip, const RigIdentity& rig )
    {
        if ( clip.SkeletonSignature != 0 && clip.SkeletonSignature == rig.Signature )
            return true;

        if ( clip.AnimatedBones.empty() )
            return false;

        size_t present = 0;
        for ( const auto& bone : clip.AnimatedBones )
            if ( rig.BoneNames.count( bone ) != 0 )
                ++present;

        return present * 2 >= clip.AnimatedBones.size();
    }

    std::vector<size_t> SelectClipsForRig( const std::vector<ClipRigIdentity>& clips, const RigIdentity& rig )
    {
        std::vector<size_t> selected;
        for ( size_t i = 0; i < clips.size(); ++i )
            if ( ClipDrivesRig( clips[i], rig ) )
                selected.push_back( i );
        return selected;
    }

    Common::ResultStr<size_t> FindClipForRig( const std::vector<ClipRigIdentity>& clips, const RigIdentity& rig,
                                              const std::string& clipName )
    {
        if ( clipName.empty() )
        {
            return Common::MakeError<size_t>(
                 "no clip name was asked for; an unnamed clip cannot resolve to anything." );
        }

        // Split the two ways this fails, because they need different fixes: the clip is not in the library at
        // all (cook / import problem), or it IS there but was authored for a different rig (asset problem).
        bool        nameExists = false;
        std::string offeredForThisRig;
        for ( size_t i = 0; i < clips.size(); ++i )
        {
            const ClipRigIdentity& clip = clips[i];
            const bool             fits = ClipDrivesRig( clip, rig );

            if ( clip.ClipName == clipName )
            {
                if ( fits )
                    return Common::MakeSuccess( static_cast<size_t>( i ) );
                nameExists = true;
            }
            else if ( fits )
            {
                offeredForThisRig += offeredForThisRig.empty() ? "" : ", ";
                offeredForThisRig += clip.ClipName;
            }
        }

        if ( nameExists )
        {
            return Common::MakeFormattedError<size_t>(
                 "clip '{}' exists but does not drive this rig (rig signature {}, {} bones). Clips that do: "
                 "[{}].",
                 clipName, rig.Signature, rig.BoneNames.size(),
                 offeredForThisRig.empty() ? "none" : offeredForThisRig );
        }

        return Common::MakeFormattedError<size_t>(
             "no clip named '{}' is registered ({} clip(s) known, of which [{}] drive this rig).", clipName,
             clips.size(), offeredForThisRig.empty() ? "none" : offeredForThisRig );
    }
} // namespace Desert::Animation
