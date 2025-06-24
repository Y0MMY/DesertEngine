#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapper.hpp>

namespace Desert::Graphic::Models::PBR
{
    struct alignas( 16 ) PBRUniforms
    {
        glm::vec3 Albedo{ 1.0f };
        float     Metallic  = 0.0f;
        float     Roughness = 0.0f;
    };

    class PBRMaterial : public MaterialHelper::MaterialWrapper
    {
    public:
        explicit PBRMaterial( const std::shared_ptr<Material>& material )
             : MaterialHelper::MaterialWrapper( material, "PBRData")
        {
        }

        void UpdatePBR(  PBRUniforms&& pbr );
    private:
        PBRUniforms m_PBRUniforms;
    };
} // namespace Desert::Graphic::Models::PBR