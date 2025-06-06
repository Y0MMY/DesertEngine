#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapper.hpp>

namespace Desert::Graphic::Models
{
    class LightingData final : public MaterialHelper::MaterialWrapper
    {
    public:
        explicit LightingData( const std::shared_ptr<UniformBuffer>& uniform,
                               const std::shared_ptr<Material>&      material )
             : MaterialHelper::MaterialWrapper( material ), m_UniformBuffer( uniform )
        {
        }

        void UpdateDirection( glm::vec3&& direction )
        {
            m_Direction = std::move( direction );
            m_UniformBuffer->SetData( &m_Direction[0], sizeof( glm::vec3 ) );
            m_Material->AddUniformToOverride( m_UniformBuffer );
        }

    private:
        glm::vec3                            m_Direction;
        const std::shared_ptr<UniformBuffer> m_UniformBuffer;
    };
} // namespace Desert::Graphic::Models