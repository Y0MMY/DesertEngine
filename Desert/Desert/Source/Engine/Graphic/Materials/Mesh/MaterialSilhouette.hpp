#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    // Renders selected meshes as a flat white mask. The mask is consumed by the Jump Flood
    // outline pipeline (see JumpFloodOutlineRenderer) to build the object outline.
    //
    // The per-mesh transform is pushed automatically by Renderer::RenderMesh; this material
    // only feeds the shared camera uniform buffer.
    class MaterialSilhouette final : public Material
    {
    public:
        MaterialSilhouette();

        void UpdateCamera( const Core::Camera* camera );
    };
} // namespace Desert::Graphic
