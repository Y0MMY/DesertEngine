#include "AnimationClipBuild.hpp"

#include <algorithm>
#include <unordered_set>

namespace Desert::Assets::Serialization
{
    Common::ResultStr<Animation::AnimationClip> BuildClipFromAssetData( const AnimationAssetData& data )
    {
        Animation::AnimationClip clip;
        clip.AnimationName     = data.Name;
        clip.Duration          = data.Duration;
        clip.TicksPerSecond    = data.TicksPerSecond;
        clip.SkeletonSignature = data.SkeletonSignature;
        clip.Tracks.reserve( data.Channels.size() );

        std::unordered_set<std::string> claimed;
        claimed.reserve( data.Channels.size() );

        for ( size_t i = 0; i < data.Channels.size(); ++i )
        {
            const auto& channel = data.Channels[i];

            if ( channel.BoneName.empty() )
            {
                return Common::MakeFormattedError<Animation::AnimationClip>(
                     "clip '{}': channel {} of {} names no bone. The bone name is the only key playback binds "
                     "on, so these {} position / {} rotation / {} scale keys could never reach a skeleton.",
                     data.Name, i, data.Channels.size(), channel.Positions.size(), channel.Rotations.size(),
                     channel.Scales.size() );
            }

            if ( !claimed.insert( channel.BoneName ).second )
            {
                return Common::MakeFormattedError<Animation::AnimationClip>(
                     "clip '{}': channel {} claims bone '{}', which an earlier channel already claims. "
                     "Playback resolves a bone to ONE track, so one of the two would be dropped without a "
                     "word.",
                     data.Name, i, channel.BoneName );
            }

            Animation::BoneTrack track;
            track.BoneName = channel.BoneName;

            track.PositionKeys.reserve( channel.Positions.size() );
            for ( const auto& p : channel.Positions )
                track.PositionKeys.push_back( Animation::PositionKeyFrame{ p.Time, p.Value } );

            track.RotationKeys.reserve( channel.Rotations.size() );
            for ( const auto& r : channel.Rotations )
                track.RotationKeys.push_back( Animation::RotationKeyFrame{ r.Time, r.Value } );

            track.ScaleKeys.reserve( channel.Scales.size() );
            for ( const auto& s : channel.Scales )
                track.ScaleKeys.push_back( Animation::ScaleKeyFrame{ s.Time, s.Value } );

            clip.Tracks.push_back( std::move( track ) );
        }

        // Notifies sorted by time so the Animator's crossing test is a simple ordered scan.
        clip.Notifies.reserve( data.Notifies.size() );
        for ( const auto& n : data.Notifies )
            clip.Notifies.push_back( Animation::AnimationNotify{ n.Name, n.Time } );
        std::sort( clip.Notifies.begin(), clip.Notifies.end(),
                   []( const Animation::AnimationNotify& a, const Animation::AnimationNotify& b )
                   { return a.Time < b.Time; } );

        return Common::MakeSuccess( std::move( clip ) );
    }
} // namespace Desert::Assets::Serialization
