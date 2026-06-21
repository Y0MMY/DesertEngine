#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Image.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Final Jump Flood pass: composites the outline on top of scene color.
    class MaterialJFAComposite final : public Material
    {
    public:
        MaterialJFAComposite();

        void Bind( const Image2D* jfaSeed, const Image2D* sceneColor,
                   const glm::vec4& outlineColor, float outlineWidth, float smoothness );

        // Typed outline parameters — visible to editor via GetRegisteredProperties()
        MPROPERTY( glm::vec4, OutlineColor, "u_OutlineColor", (glm::vec4( 1.0f, 0.5f, 0.0f, 1.0f )) )
        MPROPERTY( float,     OutlineWidth, "u_OutlineWidth", 4.0f )
        MPROPERTY( float,     Smoothness,   "u_Smoothness",   2.0f )

    private:
        Texture2DProperty* m_JFATexture   = nullptr;
        Texture2DProperty* m_SceneTexture = nullptr;
    };
} // namespace Desert::Graphic
