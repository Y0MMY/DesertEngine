#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Where the sun lands on screen, and how much of a light-shaft source it still is there — the pure
    // maths of the light-shaft pass, in a header the tests can compile without a renderer.
    //
    // The sun is a DIRECTION, so it is projected as one: clip = viewProjection * (dir, 0). A direction
    // has no position for the near plane to cull, but it does have a side — clip.w <= 0 is the sun
    // behind the camera, and a radial blur toward a sun behind the camera streaks AWAY from where the
    // light comes from, which reads as a black hole in the sky. Fade, not pop: the source is windowed
    // off over a margin past the screen edge, exactly as UE does, so turning the camera never snaps the
    // streaks on or off.
    struct SunScreen
    {
        glm::vec2 Uv;   // [0,1] screen UV, y down — the convention every screen pass here samples with
        float     Fade; // 1 fully on screen, 0 behind the camera or far past the edge
    };

    // The fade for a sun at @p ndc (clip.xy / clip.w). 1 inside the screen, falling to 0 by the time
    // the sun is @p kEdgeMargin past the edge in either axis. Separate and pure so the window's shape
    // is a tested fact rather than a magic expression inside a dispatch.
    inline float LightShaftEdgeFade( const glm::vec2& ndc )
    {
        constexpr float kEdgeMargin = 0.6f; // how far past the edge (in NDC) the source survives
        const float     reach       = glm::max( glm::abs( ndc.x ), glm::abs( ndc.y ) );
        return 1.0f - glm::smoothstep( 1.0f, 1.0f + kEdgeMargin, reach );
    }

    inline SunScreen ComputeSunScreen( const glm::mat4& viewProjection, const glm::vec3& sunDirection )
    {
        const glm::vec4 clip = viewProjection * glm::vec4( sunDirection, 0.0f );
        if ( clip.w <= 1e-6f )
            return SunScreen{ glm::vec2( 0.5f ), 0.0f };

        const glm::vec2 ndc( clip.x / clip.w, clip.y / clip.w );

        // The inverse of the compute passes' own uv->ndc mapping (ndc = (2u-1, 1-2v)), so the
        // sun the shafts stream from is the sun the sky pass drew.
        const glm::vec2 uv( ndc.x * 0.5f + 0.5f, ( 1.0f - ndc.y ) * 0.5f );

        return SunScreen{ uv, LightShaftEdgeFade( ndc ) };
    }
} // namespace Desert::Graphic
