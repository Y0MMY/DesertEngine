#pragma once

#include <Engine/Graphic/Materials/Models/Mesh/Outline.hpp>
#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    class MaterialOutline final : public Material
    {
    public:
        MaterialOutline();

        void UpdateRenderParameters( const Core::Camera& camera, const glm::mat4& transform, const float width,
                                     const glm::vec3& color );

    private:
        std::unique_ptr<Models::OutlineData> m_OutlineData;
    };
} // namespace Desert::Graphic