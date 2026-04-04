#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetEvents.hpp>

#include <Engine/Animation/AnimationClip.hpp>

namespace Desert::Assets
{
    class AnimationAsset : public AssetBase
    {
    public:
        AnimationAsset( const AssetPriority priority, const Common::Filepath& filepath );

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        const Animation::AnimationClip& GetClip() const
        {
            return m_Clip;
        }

        uint64_t GetSkeletonSignature() const
        {
            return m_SkeletonSignature;
        }

        virtual bool IsReadyForUse() const
        {
            return true;
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Animation;
        }

    private:
        Animation::AnimationClip m_Clip;
        uint64_t                 m_SkeletonSignature;
    };

} // namespace Desert::Assets