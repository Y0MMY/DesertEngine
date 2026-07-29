#include "AnimationAsset.hpp"

#include <Engine/Assets/Serialization/Animation.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    AnimationAsset::AnimationAsset( const AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, GetTypeID() )
    {
    }

    Common::BoolResultStr AnimationAsset::Load()
    {
        auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        // DefaultIfMissing: clips cooked before a field existed (e.g. Notifies) still load — the missing
        // field takes its default (empty) instead of failing the whole read.
        const auto dataReflected =
             rfl::json::read<Serialization::AnimationAssetData, rfl::DefaultIfMissing>( raw );
        if ( !dataReflected.has_value() )
        {
            return Common::MakeError( dataReflected.error().what() );
        }

        const auto data = dataReflected.value();

        m_SkeletonSignature = data.SkeletonSignature;

        m_Clip.Duration       = data.Duration;
        m_Clip.AnimationName  = data.Name;
        m_Clip.TicksPerSecond = data.TicksPerSecond;
        m_Clip.Tracks.clear();
        uint32_t maxBoneIndex = 0;

        for ( const auto& channel : data.Channels )
        {
            maxBoneIndex = std::max( maxBoneIndex, channel.BoneIndex );
        }

        m_Clip.Tracks.resize( maxBoneIndex + 1 );

        for ( const auto& channel : data.Channels )
        {
            Animation::BoneTrack track;

            track.BoneName  = channel.BoneName;
            track.BoneIndex = channel.BoneIndex;

            // Positions
            track.PositionKeys.reserve( channel.Positions.size() );
            for ( const auto& p : channel.Positions )
            {
                Animation::PositionKeyFrame key;
                key.Time     = p.Time;
                key.Position = p.Value;

                track.PositionKeys.push_back( key );
            }

            // Rotations
            track.RotationKeys.reserve( channel.Rotations.size() );
            for ( const auto& r : channel.Rotations )
            {
                Animation::RotationKeyFrame key;
                key.Time     = r.Time;
                key.Rotation = r.Value;

                track.RotationKeys.push_back( key );
            }

            // Scales
            track.ScaleKeys.reserve( channel.Scales.size() );
            for ( const auto& s : channel.Scales )
            {
                Animation::ScaleKeyFrame key;
                key.Time  = s.Time;
                key.Scale = s.Value;

                track.ScaleKeys.push_back( key );
            }

            m_Clip.Tracks[track.BoneIndex] = std::move( track );
        }

        // Notifies (sorted by time so the Animator's crossing test is a simple ordered scan).
        m_Clip.Notifies.clear();
        m_Clip.Notifies.reserve( data.Notifies.size() );
        for ( const auto& n : data.Notifies )
            m_Clip.Notifies.push_back( Animation::AnimationNotify{ n.Name, n.Time } );
        std::sort( m_Clip.Notifies.begin(), m_Clip.Notifies.end(),
                   []( const auto& a, const auto& b ) { return a.Time < b.Time; } );

        m_Clip.SkeletonSignature = m_SkeletonSignature;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr AnimationAsset::Unload()
    {
        return BOOLSUCCESS;
    }

} // namespace Desert::Assets