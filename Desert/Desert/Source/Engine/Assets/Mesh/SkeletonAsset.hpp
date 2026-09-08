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

        // WAS A HARDCODED `return true`, WHICH MADE THIS TYPE UNLOADABLE AND UNLOADED AT ONCE.
        //
        // `AssetBase::EnsureLoaded` opens with `if ( IsReadyForUse() ) return BOOLSUCCESS;`, so a constant
        // true meant a skeleton shell registered with `loadAfterCreate = false` could NEVER be parsed:
        // `m_Skeleton` stayed null, `GetSkeleton()` answered nullptr and `GetSignature()` answered 0. That
        // zero is the number `SkinnedMeshAsset::ResolveDependencies` matches rigs on, so an unloaded
        // skeleton silently failed to match the mesh that names it — the same never-recovers shape that
        // file's own comment warns about, from the other side.
        //
        // The skeleton IS the readiness, so there is no flag to keep in step with it.
        virtual bool IsReadyForUse() const override
        {
            return m_Skeleton != nullptr;
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