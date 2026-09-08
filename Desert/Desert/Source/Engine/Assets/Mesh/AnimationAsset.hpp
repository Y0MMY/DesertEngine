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

        // Injects an in-memory clip (no file backing) — used for code-generated clips such as the procedural
        // character locomotion ([[procedural-character]]). Create the asset with loadAfterCreate=false, then
        // call this so it shows up in the AnimationLibrary / editor clip selector like a cooked clip.
        void SetInMemoryClip( const Animation::AnimationClip& clip )
        {
            m_Clip              = clip;
            m_SkeletonSignature = clip.SkeletonSignature;
            m_HasClip           = true;
            // NO FILE EVER PRODUCED THIS ONE, so nothing can produce it again. See
            // AssetBase::IsReloadableFromFile.
            m_FromMemory = true;
        }

        // WAS A HARDCODED `return true`. That made the type both unloadable and un-re-loadable:
        // `EnsureLoaded` short-circuits on it, so a shell created with `loadAfterCreate = false` — the
        // documented path for procedural clips, two lines above — reported itself ready while holding an
        // empty clip and an UNINITIALISED `m_SkeletonSignature`, which `GetSkeletonSignature()` then
        // handed to the animation system.
        bool IsReadyForUse() const override
        {
            return m_HasClip;
        }

        bool IsReloadableFromFile() const override
        {
            return !m_FromMemory;
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Animation;
        }

    private:
        Animation::AnimationClip m_Clip;
        // WAS UNINITIALISED. `GetSkeletonSignature()` on a shell that had not been loaded returned whatever
        // was on the heap, and the animation system matches rigs on that number.
        uint64_t m_SkeletonSignature = 0;
        bool     m_HasClip           = false;
        bool     m_FromMemory        = false;
    };

} // namespace Desert::Assets