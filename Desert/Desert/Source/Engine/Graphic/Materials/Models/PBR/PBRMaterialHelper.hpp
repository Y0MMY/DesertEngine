#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

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
        explicit PBRMaterial( const std::shared_ptr<MaterialExecutor>& material )
             : MaterialHelper::MaterialWrapper( material, "PBRData" )
        {
        }

        void UpdatePBR( const PBRUniforms& pbr );

    private:
        PBRUniforms m_PBRUniforms;
    };
} // namespace Desert::Graphic::Models::PBR