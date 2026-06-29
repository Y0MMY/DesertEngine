#include "ProceduralCharacterAnimations.hpp"

#include <Engine/Animation/Skeleton.hpp>
#include <Engine/Animation/BoneInfo.hpp>
#include <Engine/Animation/AnimationLibrary.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/AnimationAsset.hpp>
#include <Engine/Geometry/ProceduralCharacterFactory.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <functional>
#include <optional>
#include <unordered_map>

namespace Desert::Animation
{
    namespace
    {
        constexpr int   kSamples = 16;          // keyframes per cycle (endpoint included for a seamless loop)
        const glm::vec3 kAxisX( 1.0f, 0.0f, 0.0f ); // sagittal swing axis (character faces +Z)

        // Builds a clip from per-bone angle functions. `angleFns[name] = f(u)` where u in [0,1] is the cycle
        // fraction and f returns the rotation angle (radians) about X. Bones with no entry keep their bind
        // pose. Every authored track also carries the bone's constant bind-local translation.
        AnimationClip Build( const char* name, float duration,
                             const std::unordered_map<std::string, std::function<float( float )>>& angleFns )
        {
            const Skeleton* skel = Geometry::ProceduralCharacterFactory::GetHumanoidSkeleton();
            const auto&     bones = skel->GetBones();

            std::unordered_map<std::string, uint32_t> nameToIdx;
            for ( const auto& b : bones )
                nameToIdx[b.Name] = b.BoneIndex;

            AnimationClip clip;
            clip.AnimationName     = name;
            clip.Duration          = duration;
            clip.TicksPerSecond    = 1.0f; // Time advances in seconds -> keyframe times are seconds
            clip.SkeletonSignature = skel->GetSignature();
            clip.Tracks.resize( bones.size() ); // empty tracks fall back to LocalBindTransform

            for ( const auto& [boneName, fn] : angleFns )
            {
                auto it = nameToIdx.find( boneName );
                if ( it == nameToIdx.end() )
                    continue;
                const uint32_t idx      = it->second;
                const glm::vec3 bindPos = glm::vec3( bones[idx].LocalBindTransform[3] );

                BoneTrack& t = clip.Tracks[idx];
                t.BoneName   = boneName;
                t.BoneIndex  = idx;
                t.PositionKeys.push_back( { 0.0f, bindPos } ); // constant -> keeps the bone at its bind offset
                for ( int i = 0; i <= kSamples; ++i )
                {
                    const float u = static_cast<float>( i ) / kSamples;
                    t.RotationKeys.push_back( { duration * u, glm::angleAxis( fn( u ), kAxisX ) } );
                }
            }
            return clip;
        }

        constexpr float kTau = 6.2831853f;

        std::optional<AnimationClip> s_Idle, s_Walk, s_Run, s_Jump;
    } // namespace

    const AnimationClip& ProceduralCharacterAnimations::Idle()
    {
        if ( !s_Idle )
        {
            // Subtle breathing/sway so a standing character isn't a frozen statue.
            s_Idle = Build( "Idle", 3.0f,
                            { { "Spine", []( float u ) { return 0.04f * std::sin( kTau * u ); } },
                              { "Chest", []( float u ) { return 0.03f * std::sin( kTau * u ); } },
                              { "Shoulder.L", []( float u ) { return 0.05f * std::sin( kTau * u ); } },
                              { "Shoulder.R", []( float u ) { return 0.05f * std::sin( kTau * u ); } },
                              { "Elbow.L", []( float ) { return -0.15f; } },
                              { "Elbow.R", []( float ) { return -0.15f; } } } );
        }
        return *s_Idle;
    }

    const AnimationClip& ProceduralCharacterAnimations::Walk()
    {
        if ( !s_Walk )
        {
            constexpr float A_hip = 0.5f, A_knee = 0.7f, A_arm = 0.4f;
            s_Walk = Build(
                 "Walk", 1.0f,
                 { // legs swing opposite; arms counter-swing the legs
                   { "Hip.L", []( float u ) { return A_hip * std::sin( kTau * u ); } },
                   { "Hip.R", []( float u ) { return A_hip * std::sin( kTau * u + kTau * 0.5f ); } },
                   { "Knee.L", []( float u ) { return A_knee * ( 0.5f - 0.5f * std::cos( kTau * u - kTau * 0.25f ) ); } },
                   { "Knee.R", []( float u ) { return A_knee * ( 0.5f - 0.5f * std::cos( kTau * u + kTau * 0.25f ) ); } },
                   { "Shoulder.L", []( float u ) { return A_arm * std::sin( kTau * u + kTau * 0.5f ); } },
                   { "Shoulder.R", []( float u ) { return A_arm * std::sin( kTau * u ); } },
                   { "Elbow.L", []( float ) { return -0.25f; } },
                   { "Elbow.R", []( float ) { return -0.25f; } } } );
        }
        return *s_Walk;
    }

    const AnimationClip& ProceduralCharacterAnimations::Run()
    {
        if ( !s_Run )
        {
            constexpr float A_hip = 0.85f, A_knee = 1.1f, A_arm = 0.7f;
            s_Run = Build(
                 "Run", 0.6f,
                 { { "Spine", []( float ) { return 0.20f; } }, // forward lean
                   { "Hip.L", []( float u ) { return A_hip * std::sin( kTau * u ); } },
                   { "Hip.R", []( float u ) { return A_hip * std::sin( kTau * u + kTau * 0.5f ); } },
                   { "Knee.L", []( float u ) { return A_knee * ( 0.5f - 0.5f * std::cos( kTau * u - kTau * 0.25f ) ); } },
                   { "Knee.R", []( float u ) { return A_knee * ( 0.5f - 0.5f * std::cos( kTau * u + kTau * 0.25f ) ); } },
                   { "Shoulder.L", []( float u ) { return A_arm * std::sin( kTau * u + kTau * 0.5f ); } },
                   { "Shoulder.R", []( float u ) { return A_arm * std::sin( kTau * u ); } },
                   { "Elbow.L", []( float ) { return -0.6f; } },
                   { "Elbow.R", []( float ) { return -0.6f; } } } );
        }
        return *s_Run;
    }

    const AnimationClip& ProceduralCharacterAnimations::Jump()
    {
        if ( !s_Jump )
        {
            // A held airborne pose: knees tucked, slight forward lean, arms raised forward. Constant (it just
            // holds while the character is off the ground); loops trivially.
            s_Jump = Build( "Jump", 0.5f,
                            { { "Spine", []( float ) { return 0.12f; } },
                              { "Hip.L", []( float ) { return -0.25f; } },
                              { "Hip.R", []( float ) { return -0.25f; } },
                              { "Knee.L", []( float ) { return 0.9f; } },
                              { "Knee.R", []( float ) { return 0.9f; } },
                              { "Shoulder.L", []( float ) { return -0.45f; } },
                              { "Shoulder.R", []( float ) { return -0.45f; } },
                              { "Elbow.L", []( float ) { return -0.6f; } },
                              { "Elbow.R", []( float ) { return -0.6f; } } } );
        }
        return *s_Jump;
    }

    void ProceduralCharacterAnimations::RegisterClips( Assets::AssetManager& assets, AnimationLibrary& library )
    {
        const AnimationClip* clips[] = { &Idle(), &Walk(), &Run(), &Jump() };
        for ( const AnimationClip* clip : clips )
        {
            auto asset = assets.CreateAsset<Assets::AnimationAsset>(
                 Assets::AssetPriority::Medium, Common::Filepath( "procedural://humanoid/" + clip->AnimationName ),
                 false );
            if ( asset )
            {
                asset->SetInMemoryClip( *clip );
                library.Register( asset );
            }
        }
    }
} // namespace Desert::Animation
