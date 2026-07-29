#pragma once

#include "MeshTypes.hpp"

#include <algorithm>
#include <vector>

namespace Desert::Geometry
{
    // CPU blendshape evaluation: out = base + Σ(weight_k · target_k), for positions and (when a target
    // carries them) normals. weights[k] pairs with targets[k]; missing weights are treated as 0, so passing
    // fewer weights than targets simply leaves the trailing targets inactive. Zero-weight targets are
    // skipped. Morphed normals are renormalized. `out` is overwritten (sized to base).
    //
    // This is the correctness-first CPU path — a per-entity blend the runtime can push into a dynamic vertex
    // buffer. The GPU path (deltas as an SSBO summed in the vertex shader) is a later optimization that
    // reuses the exact same target data.
    inline void ApplyMorphTargets( const std::vector<Vertex>& base, const std::vector<MorphTarget>& targets,
                                   const std::vector<float>& weights, std::vector<Vertex>& out )
    {
        out = base;

        bool touchedNormals = false;
        for ( size_t k = 0; k < targets.size(); ++k )
        {
            const float w = k < weights.size() ? weights[k] : 0.0f;
            if ( w == 0.0f )
                continue;

            const MorphTarget& t = targets[k];

            const size_t nPos = std::min( out.size(), t.DeltaPositions.size() );
            for ( size_t i = 0; i < nPos; ++i )
                out[i].Position += w * t.DeltaPositions[i];

            const size_t nNrm = std::min( out.size(), t.DeltaNormals.size() );
            for ( size_t i = 0; i < nNrm; ++i )
                out[i].Normal += w * t.DeltaNormals[i];
            touchedNormals |= ( nNrm > 0 );
        }

        if ( touchedNormals )
        {
            for ( Vertex& v : out )
            {
                const float len2 = glm::dot( v.Normal, v.Normal );
                if ( len2 > 1e-12f )
                    v.Normal = v.Normal * glm::inversesqrt( len2 );
            }
        }
    }
} // namespace Desert::Geometry
