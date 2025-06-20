#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapper.hpp>

namespace Desert::Graphic::Models
{
    struct GlobalUB
    {
        glm::vec3 CameraPosition;
    };

    class GlobalData final : public MaterialHelper::MaterialWrapper
    {
    public:
        explicit GlobalData( GlobalUB&& data, const std::shared_ptr<Uniforms::UniformBuffer>& uniform,
                             const std::shared_ptr<Material>& material )
             : MaterialHelper::MaterialWrapper( material, uniform ), m_GlobalUB( std::move( data ) )
        {
        }

        void UpdateUBGlobal( GlobalUB&& ubGlobal )
        {
            m_GlobalUB = std::move( ubGlobal );
            m_Uniform->RT_SetData( &m_GlobalUB, sizeof(GlobalUB));
        }

    private:
        GlobalUB m_GlobalUB;
    };
} // namespace Desert::Graphic::Models