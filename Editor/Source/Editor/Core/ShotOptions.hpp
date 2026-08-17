#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <string>

namespace Desert::Editor
{
    // Command-line SCREENSHOT MODE: load a scene, point the camera, render a fixed number of frames, write
    // the viewport to a PNG and exit.
    //
    // It exists because the alternative is worse. Every cloud defect this engine has shipped — a
    // tonemapper that was the identity, a radiance five thousand times too large, a shadow map marching
    // the far side of the planet — is one that a person looking at the screen catches in a minute, and the
    // people looking at this screen are outnumbered by the parameters. This turns "does it look right" from
    // a question someone has to be present to answer into one command.
    //
    // The frame count matters and is not decoration: the volumetric clouds accumulate over roughly ten
    // frames (Temporal Blend Factor 0.10), so a shot taken on frame one is a picture of the dither.
    //
    // MOTION, and why a still is not enough. Under a FIXED camera the cloud temporal resolve's
    // reprojection is exact by construction — every pixel finds its own history at its own texel — so a
    // still can never exercise the parts of that stage that exist for a moving one: the disocclusion
    // fallback, the neighbourhood clamp, the Ultra tier's checkerboard, the second layer's shell-parallax
    // error. `--camera-to` / `--look-to` interpolate the camera across the warm-up frames and
    // `--shot-sequence` writes the frames out instead of only the last, which is what turns those into
    // things that can be measured rather than predicted.

    // A camera pose along the shot path: where the eye is and where it points. Not normalized — the
    // consumer normalizes, exactly as it does for the `--look` it replaces.
    struct ShotCamera
    {
        glm::vec3 Position;
        glm::vec3 Forward;
    };

    // Interpolate two view DIRECTIONS along the shortest arc at constant angular velocity.
    //
    // Constant angular velocity is the point, not a flourish. The thing these paths are built to measure is
    // frame-to-frame difference under smooth motion; a component-wise lerp of two directions sweeps fastest
    // in the middle of the turn and slowest at its ends, which would put a hump in the middle of every
    // measurement that belongs to the interpolator rather than to the renderer.
    //
    // The result is a DIRECTION and its length is not part of the contract — exactly like the `--look` it
    // interpolates, which the documentation has always said need not be normalized. What IS part of the
    // contract: at the endpoints, and wherever the two directions coincide, the input comes back as the
    // same float it went in as.
    inline glm::vec3 ShotSlerpDirection( const glm::vec3& from, const glm::vec3& to, float t )
    {
        // The endpoints come back EXACTLY as they were given, ahead of any arithmetic that could move them
        // in the last bit. This is not tidiness: a still shot places its camera at t = 0, and `--look`
        // must reach `SnapToDirection` bit for bit as it did before this path existed. A shot is not
        // bit-reproducible for other reasons — the timestep is wall-clock, so the wind has advanced by a
        // different amount by frame 90 on every run (Docs/Clouds/Shots/README.md measures that noise) —
        // and the pose is the one part of it that CAN be held exact, so it is.
        if ( t <= 0.0f )
            return from;
        if ( t >= 1.0f )
            return to;

        const float fromLength = glm::length( from );
        const float toLength   = glm::length( to );
        // A zero direction is not a direction. Nothing sensible can be interpolated from or to it, so the
        // one that IS a direction is returned unchanged rather than producing a NaN pose.
        if ( fromLength < 1e-6f )
            return to;
        if ( toLength < 1e-6f )
            return from;

        const glm::vec3 a = from / fromLength;
        const glm::vec3 b = to / toLength;

        const float cosAngle = glm::clamp( glm::dot( a, b ), -1.0f, 1.0f );

        // Nearly parallel: the arc is shorter than the precision of the axis that would describe it, and
        // the slerp denominator sin(angle) is on its way to zero. `from` UNCHANGED, not its normalized
        // form — `--camera-to` with no `--look-to` interpolates a position along a fixed aim, and that aim
        // has to survive the trip as the same float it arrived as, or a pure translation quietly becomes a
        // translation plus a hundredth of a degree of pan.
        if ( cosAngle > 0.999999f )
            return from;

        // Exactly opposed. There is no shortest arc — every half-turn is as short as every other — so one
        // is chosen rather than left to a cross product that is the zero vector here. Rodrigues about an
        // axis perpendicular to `a`, with the perpendicular taken from whichever world axis `a` leans on
        // least so the cross product is never degenerate.
        if ( cosAngle < -0.999999f )
        {
            const glm::vec3 leastAligned =
                 ( std::fabs( a.x ) < 0.9f ) ? glm::vec3( 1.0f, 0.0f, 0.0f ) : glm::vec3( 0.0f, 1.0f, 0.0f );
            const glm::vec3 axis  = glm::normalize( glm::cross( a, leastAligned ) );
            const float     angle = 3.14159265358979323846f * t;
            return a * std::cos( angle ) + glm::cross( axis, a ) * std::sin( angle );
        }

        const float angle    = std::acos( cosAngle );
        const float sinAngle = std::sin( angle );
        return ( a * std::sin( ( 1.0f - t ) * angle ) + b * std::sin( t * angle ) ) / sinAngle;
    }

    struct ShotOptions
    {
        static ShotOptions& Get()
        {
            static ShotOptions s;
            return s;
        }

        std::string Scene;  // --scene <path.desce>, overrides the project's default scene
        std::string Output; // --shot <out.png>; empty = no single final frame
        int         Frames = 90;

        bool      HasCamera = false;
        glm::vec3 Position{ 0.0f, 200.0f, 0.0f }; // --camera x,y,z   (world units)
        glm::vec3 Forward{ 0.0f, 0.30f, -1.0f };  // --look   x,y,z   (need not be normalized)

        // The far end of the path. Held with their own flags rather than defaulted to Position/Forward at
        // parse time because the flags may arrive in any order: `--camera-to` before `--camera` must still
        // end at the position `--camera` names, not at the built-in default it was holding when parsed.
        bool      HasPositionTo = false;
        glm::vec3 PositionTo{ 0.0f, 0.0f, 0.0f }; // --camera-to x,y,z
        bool      HasForwardTo = false;
        glm::vec3 ForwardTo{ 0.0f, 0.0f, -1.0f }; // --look-to   x,y,z

        std::string Sequence;          // --shot-sequence <dir>; every Nth frame written there
        int         SequenceEvery = 1; // --shot-every N

        // Headless capture mode at all — either flavour of output activates it. `--shot-sequence` alone is
        // a legitimate run: a motion study wants the frames and has no use for a designated last one.
        bool Active() const
        {
            return !Output.empty() || !Sequence.empty();
        }

        // Whether the camera MOVES. False is the whole of the existing behaviour: the pose is placed once
        // and never touched again, which is what keeps an old `--camera`/`--look` shot identical.
        bool HasMotion() const
        {
            return HasPositionTo || HasForwardTo;
        }

        // Where along the path frame @p frame sits, in [0, 1]. The first rendered frame is at 0 and the
        // last at 1, so `--camera-to` names the pose the captured frame is actually taken from.
        float Parameter( int frame ) const
        {
            if ( Frames <= 1 )
                return 0.0f;
            return glm::clamp( static_cast<float>( frame ) / static_cast<float>( Frames - 1 ), 0.0f, 1.0f );
        }

        // The pose at normalized time @p t. With no motion flags this is the constant (Position, Forward)
        // for every t, on the exact float — the relation that makes an existing still shot the same shot
        // it was, and asserted by test rather than argued for.
        ShotCamera CameraAt( float t ) const
        {
            const glm::vec3 positionEnd = HasPositionTo ? PositionTo : Position;
            const glm::vec3 forwardEnd  = HasForwardTo ? ForwardTo : Forward;
            return ShotCamera{ glm::mix( Position, positionEnd, t ),
                               ShotSlerpDirection( Forward, forwardEnd, t ) };
        }
    };
} // namespace Desert::Editor
