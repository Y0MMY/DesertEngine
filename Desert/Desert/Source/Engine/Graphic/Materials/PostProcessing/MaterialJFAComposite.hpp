#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Image.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    struct JFACompositeParams
    {
        glm::vec3 OutlineColor{ 1.0f, 0.5f, 0.0f };
        float     OutlineWidth = 4.0f; // in pixels
        float     Smoothness   = 2.0f; // edge softness in pixels
    };

    // Final Jump Flood pass: composites the outline (derived from the flooded distance field)
    // on top of the scene color. Uses the JFA_Final shader.
    class MaterialJFAComposite final : public Material
    {
    public:
        MaterialJFAComposite();

        void Bind( const Image2D* jfaSeed, const Image2D* sceneColor, const JFACompositeParams& params );

    private:
        // Matches std140 `JFAFinalUB { vec4 u_OutlineColor; float u_OutlineWidth; float u_Smoothness; }`.
        struct JFAFinalUB
        {
            glm::vec4 OutlineColor;
            float     OutlineWidth;
            float     Smoothness;
        };

        Texture2DProperty* m_JFATexture   = nullptr;
        Texture2DProperty* m_SceneTexture = nullptr;
    };
} // namespace Desert::Graphic
