#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Depth-only material for the directional shadow pass. Feeds the LIGHT's view/projection into the
    // shared CameraUB (the per-mesh transform is pushed by Renderer::RenderMesh).
    class MaterialShadow : public Material
    {
    public:
        MaterialShadow();

        void SetLightMatrix( const glm::mat4& view, const glm::mat4& projection );

    protected:
        // Lets the instanced variant bind a different shader while reusing SetLightMatrix.
        explicit MaterialShadow( std::string&& shaderName );
    };

    // Instanced depth-only shadow material: bound to the Shadow_Instanced shader, whose vertex reads each
    // caster's model matrix from the InstanceTransforms SSBO (binding 16) by gl_InstanceIndex.
    class MaterialShadowInstanced final : public MaterialShadow
    {
    public:
        MaterialShadowInstanced();
    };
} // namespace Desert::Graphic
