#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Depth-only material for the directional shadow pass. Feeds the LIGHT's view/projection into the
    // shared CameraUB (the per-mesh transform is pushed by Renderer::RenderMesh).
    class MaterialShadow final : public Material
    {
    public:
        MaterialShadow();

        void SetLightMatrix( const glm::mat4& view, const glm::mat4& projection );
    };
} // namespace Desert::Graphic
