#pragma once

#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>

namespace Desert::Graphic
{
    // The Reflective Shadow Map pass's material: same StaticMaterialPBR plumbing (Update*/Bind/Materials
    // SSBO), bound to the StaticMeshGBuffer shader — the RSM is literally a G-buffer rasterized from the
    // sun instead of the camera, so it reuses that shader and its pipeline unchanged.
    //
    // The point of it being a SEPARATE material (rather than reusing the objects' own) is per-frame UB
    // ownership: this pass writes a camera UB holding the SUN's matrices. Sharing a material with the
    // opaque passes would mean two writes to the same per-frame uniform buffer in one frame — the
    // double-update hazard the glass pass was split out to avoid.
    class MaterialRSM final : public StaticMaterialPBR
    {
    public:
        MaterialRSM() : StaticMaterialPBR( "RSM", "StaticMeshGBuffer" )
        {
        }
    };
} // namespace Desert::Graphic
