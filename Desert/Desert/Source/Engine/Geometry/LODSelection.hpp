#pragma once

#include <Engine/Geometry/MeshTypes.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Desert::Geometry
{
    // Coarsest level the automatic pick may return. The draw side clamps again to the submesh's real
    // level count, so "3" only ever means "the coarsest one this mesh actually has".
    inline constexpr int kMaxAutoLOD = 3;

    // THE LOD policy, in one place. MeshRenderer draws with it and the editor's Details panel reports
    // it, so the level a user reads next to a mesh cannot drift from the level actually drawn.
    // forcedLOD >= 0 wins outright; lodBias shifts the automatic pick (+coarser, -finer).
    //
    // Takes the submeshes rather than the Mesh so the policy stays pure CPU code (no GPU buffers) —
    // that is what makes it testable and callable from the editor.
    inline uint32_t SelectLOD( const glm::mat4& transform, const std::vector<Submesh>& submeshes,
                               const glm::vec3& cameraPosition, int forcedLOD, int lodBias )
    {
        if ( forcedLOD >= 0 )
            return static_cast<uint32_t>( forcedLOD );

        // World-space bounding radius = mesh AABB half-diagonal * the largest transform scale. Using it
        // (instead of raw distance) makes selection SIZE-AWARE: a large object keeps full detail farther
        // away than a small one.
        glm::vec3 mn( 1.0e30f );
        glm::vec3 mx( -1.0e30f );
        for ( const auto& sm : submeshes )
        {
            mn = glm::min( mn, sm.BoundingBox.Min );
            mx = glm::max( mx, sm.BoundingBox.Max );
        }
        if ( mn.x > mx.x )
            return 0; // empty mesh

        const float scale = glm::max(
             glm::length( glm::vec3( transform[0] ) ),
             glm::max( glm::length( glm::vec3( transform[1] ) ), glm::length( glm::vec3( transform[2] ) ) ) );
        const float radius = glm::length( mx - mn ) * 0.5f * scale;
        const float dist   = glm::length( cameraPosition - glm::vec3( transform[3] ) );

        // Screen-coverage proxy (radius / distance): larger / closer = finer LOD.
        const float coverage = radius / glm::max( dist, 0.001f );
        const int   base     = coverage > 0.20f ? 0 : coverage > 0.08f ? 1 : coverage > 0.03f ? 2 : 3;
        // Per-mesh bias shifts the auto pick (+coarser / -finer).
        return static_cast<uint32_t>( std::clamp( base + lodBias, 0, kMaxAutoLOD ) );
    }
} // namespace Desert::Geometry
