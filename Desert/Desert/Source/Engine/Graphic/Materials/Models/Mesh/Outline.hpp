#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperArray.hpp>

#include <Engine/Graphic/Materials/MaterialReflection.hpp>

namespace Desert::Graphic::Models
{
    // clang-format off
    RFL_UB_TYPE(OutlineUB,
        FIELD_VALUEF(Width, "Width")
        FIELD_COLOR3(Color, "Color"))
    // clang-format on

    class OutlineData final : public MaterialHelper::MaterialWrapperArray
    {
    public:
        explicit OutlineData( const std::shared_ptr<MaterialExecutor>& material )
             : MaterialHelper::MaterialWrapperArray( material, { "OutlineUBVertex", "OutlineUBFragment" } ),
               m_OutlineUB( {} )
        {
        }

        void UpdateOutlineUB( const OutlineUB& ubOutline )
        {
            /* m_OutlineUB = ubOutline;
             m_UniformProperties[0]->SetData( &m_OutlineUB.Width, sizeof( float ) );
             m_UniformProperties[1]->SetData( &m_OutlineUB.Color, sizeof( glm::vec3 ) );*/
        }

    private:
        OutlineUB m_OutlineUB;
    };
} // namespace Desert::Graphic::Models