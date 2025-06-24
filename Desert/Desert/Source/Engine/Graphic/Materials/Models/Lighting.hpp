#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapper.hpp>

namespace Desert::Graphic::Models
{
    class LightingData final : public MaterialHelper::MaterialWrapper
    {
    public:
        explicit LightingData( glm::vec3&& direction, const std::shared_ptr<Material>& material )
             : MaterialHelper::MaterialWrapper( material, "LightningUB" ), m_Direction( std::move( direction ) )
        {
        }

        void UpdateDirection( glm::vec3&& direction )
        {
            m_Direction = std::move( direction );
            m_UniformProperty->SetData( &m_Direction[0], sizeof( glm::vec3 ) );
        }

    private:
        glm::vec3 m_Direction;
    };
} // namespace Desert::Graphic::Models