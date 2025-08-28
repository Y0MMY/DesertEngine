#pragma once

#include <Engine/Graphic/Materials/Models/Mesh/Outline.hpp>
#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    struct UpdateMaterialOutlineInfo
    {
        std::shared_ptr<Core::Camera> Camera;
        glm::mat4                     Transform;
        float                         Width;
        glm::vec3                     Color;
    };

    class MaterialOutline final : public Material<UpdateMaterialOutlineInfo>
    {
    public:
        MaterialOutline();

        void Bind( const UpdateMaterialOutlineInfo& data ) override;

    private:
        std::unique_ptr<Models::OutlineData> m_OutlineData;
    };
} // namespace Desert::Graphic