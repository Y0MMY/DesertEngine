#pragma once

#include <Engine/Animation/AnimationClip.hpp>

namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Animation
{
    class AnimationLibrary;

    // Hand-authored-in-code locomotion clips for the procedural humanoid mannequin
    // ([[procedural-character]] / Geometry::ProceduralCharacterFactory). Each clip is built once and cached,
    // carries the humanoid skeleton's signature, and animates the limb joints with sine cycles (every track
    // also stores a constant bind-local position key, or the bone would collapse to the parent origin).
    class ProceduralCharacterAnimations
    {
    public:
        static const AnimationClip& Idle();
        static const AnimationClip& Walk();
        static const AnimationClip& Run();
        static const AnimationClip& Jump(); // airborne hold pose (tucked legs, arms forward)

        // Registers all locomotion clips as in-memory AnimationAssets so they appear in the AnimationLibrary
        // (editor clip selector + AnimationECSSystem auto-play). Engine-level: a runtime/game calls this, not
        // just the editor — locomotion is gameplay, not an editor concern.
        static void RegisterClips( Assets::AssetManager& assets, AnimationLibrary& library );
    };
} // namespace Desert::Animation
