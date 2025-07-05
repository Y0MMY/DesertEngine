#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Graphic::Models
{
    struct GlobalUB
    {
        glm::vec3 CameraPosition;
    };

    class GlobalData final : public MaterialHelper::MaterialWrapper
    {
    public:
        explicit GlobalData( const std::shared_ptr<Material>& material )
             : MaterialHelper::MaterialWrapper( material, "GlobalUB" ), m_GlobalUB( {} )
        {
        }

        void UpdateUBGlobal( const GlobalUB& ubGlobal )
        {
            m_GlobalUB = ubGlobal;
            m_UniformProperty->SetData( &m_GlobalUB, sizeof( GlobalUB ) );
        }

    private:
        GlobalUB m_GlobalUB;
    };
} // namespace Desert::Graphic::Models