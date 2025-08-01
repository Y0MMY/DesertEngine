#pragma once
#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Graphic/Materials/Models/Mesh/Outline.hpp>
#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    class MaterialOutline
    {
    public:
        MaterialOutline();

        const auto& GetMaterialExecutor() const
        {
            return m_Material;
        }

        void UpdateRenderParameters( const Core::Camera& camera, const glm::mat4& transform, const float width,
                                     const glm::vec3& color );

    private:
        std::shared_ptr<MaterialExecutor>    m_Material;
        std::unique_ptr<Models::OutlineData> m_OutlineData;
    };
} // namespace Desert::Graphic