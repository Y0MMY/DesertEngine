#include "AssimpAnimationImporter.hpp"

#include <Engine/Animation/AnimationClip.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Desert::Animation::Import
{
    static std::shared_ptr<AnimationClip> ImportClip( const aiAnimation* anim )
    {
        auto clip = std::make_shared<AnimationClip>();

        clip->Duration       = static_cast<float>( anim->mDuration );
        clip->TicksPerSecond = anim->mTicksPerSecond > 0.0 ? static_cast<float>( anim->mTicksPerSecond ) : 25.0f;

        for ( uint32_t c = 0; c < anim->mNumChannels; ++c )
        {
            const aiNodeAnim* channel  = anim->mChannels[c];
            const std::string boneName = channel->mNodeName.C_Str();

            BoneTrack track;

            // Positions
            for ( uint32_t i = 0; i < channel->mNumPositionKeys; ++i )
            {
                const auto& key = channel->mPositionKeys[i];

                track.PositionKeys.push_back(
                     { static_cast<float>( key.mTime ), glm::vec3( key.mValue.x, key.mValue.y, key.mValue.z ) } );
            }

            // Rotations
            for ( uint32_t i = 0; i < channel->mNumRotationKeys; ++i )
            {
                const auto& key = channel->mRotationKeys[i];

                track.RotationKeys.push_back(
                     { static_cast<float>( key.mTime ),
                       glm::quat( key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z ) } );
            }

            // Scales
            for ( uint32_t i = 0; i < channel->mNumScalingKeys; ++i )
            {
                const auto& key = channel->mScalingKeys[i];

                track.ScaleKeys.push_back(
                     { static_cast<float>( key.mTime ), glm::vec3( key.mValue.x, key.mValue.y, key.mValue.z ) } );
            }

            clip->Tracks.emplace( boneName, std::move( track ) );
        }

        return clip;
    }

    void AssimpAnimationImporter::ImportAnimations( const aiScene* scene, Skeleton& skeleton )
    {
        if ( !scene || !scene->HasAnimations() )
            return;

        for ( uint32_t i = 0; i < scene->mNumAnimations; ++i )
        {
            const aiAnimation* anim = scene->mAnimations[i];

            std::string name = anim->mName.length > 0 ? anim->mName.C_Str() : "Animation_" + std::to_string( i );

            auto clip = ImportClip( anim );
            skeleton.AddClip( name, std::move( clip ) );
        }
    }
} // namespace Desert::Animation::Import
