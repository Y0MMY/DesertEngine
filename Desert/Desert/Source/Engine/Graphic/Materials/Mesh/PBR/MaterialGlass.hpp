#pragma once

#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>

namespace Desert::Graphic
{
    // The glass pass's material: reuses ALL of StaticMaterialPBR's Update*/Bind/Materials-SSBO plumbing, but is
    // bound to the StaticMeshGlass shader (same pattern as StaticMeshPBR_Instanced). This makes its descriptor
    // layout match the glass pipeline AND lets the glass shader carry the extra u_SceneColor (a copy of the
    // composited scene, for screen-space refraction) WITHOUT adding that binding to the opaque PBR shaders.
    class MaterialGlass final : public StaticMaterialPBR
    {
    public:
        MaterialGlass() : StaticMaterialPBR( "Glass", "StaticMeshGlass" )
        {
        }
    };
} // namespace Desert::Graphic
