#pragma once

#include <Engine/Geometry/MeshTypes.hpp>

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

// Automatic skin weighting: turn a STATIC mesh + a placed skeleton into skinned vertices, so a static model
// can be rigged in-editor without an imported skeleton. Pure CPU (glm + stdlib only) so it is unit-testable
// in isolation — the editor's "Convert to Skinned" action builds the RigBones from the skeleton and feeds
// the mesh vertices here, then constructs a SkinnedMesh from the result.
namespace Desert::Geometry
{
    // One bone for weighting, in MESH space. Head = the bone's joint position; the bone's VOLUME is the
    // segment from its parent's head to its own head (so a limb bone claims the vertices along it, not just
    // around the joint). Parent < 0 marks a root (weighted by its head point alone).
    struct RigBone
    {
        glm::vec3 Head{ 0.0f };
        int       Parent = -1;
    };

    // Shortest distance from point p to the segment [a,b] (degenerate a==b -> distance to the point).
    float DistancePointToSegment( const glm::vec3& p, const glm::vec3& a, const glm::vec3& b );

    // Assigns each vertex to its nearest bones by distance to the bone segment: weight ~ 1 / dist^falloff,
    // the top maxInfluences kept and normalized to sum 1. A vertex with no usable bone (or an empty rig)
    // falls back to full weight on bone 0. Returns skinned vertices carrying the original static data.
    std::vector<SkinnedVertex> AutoSkinVertices( const std::vector<Vertex>&  vertices,
                                                 const std::vector<RigBone>& bones, float falloff = 2.0f,
                                                 uint32_t maxInfluences = SkinnedVertex::MAX_BONE_INFLUENCES );
} // namespace Desert::Geometry
