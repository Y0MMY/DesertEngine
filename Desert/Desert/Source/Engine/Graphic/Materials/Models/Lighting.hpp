#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Graphic::Models
{
    class LightingData final : public MaterialHelper::MaterialWrapper
    {
    public:
        explicit LightingData( const std::shared_ptr<MaterialExecutor>& material )
             : MaterialHelper::MaterialWrapper( material, "LightningUB" ), m_Direction( glm::vec3( 0 ) )
        {
        }

        void UpdateDirection( const glm::vec3& direction )
        {
            m_Direction = direction;
            m_UniformProperty->SetData( &m_Direction[0], sizeof( glm::vec3 ) );
        }

    private:
        glm::vec3 m_Direction;
    };
} // namespace Desert::Graphic::Models