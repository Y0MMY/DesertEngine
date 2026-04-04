#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetEvents.hpp>

#include <Engine/Animation/Skeleton.hpp>

namespace Desert::Assets
{
    class SkeletonAsset : public AssetBase
    {
    public:
        SkeletonAsset( const AssetPriority priority, const Common::Filepath& filepath );

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        virtual bool IsReadyForUse() const override
        {
            return true;
        }

        const Animation::Skeleton* GetSkeleton() const
        {
            return m_Skeleton.get();
        }

        uint64_t GetSignature() const
        {
            return m_Skeleton ? m_Skeleton->GetSignature() : 0;
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Skeleton;
        }

    private:
        std::unique_ptr<Animation::Skeleton> m_Skeleton;
    };

} // namespace Desert::Assets