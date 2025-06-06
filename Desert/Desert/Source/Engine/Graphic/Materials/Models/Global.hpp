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
        explicit GlobalData( const std::shared_ptr<UniformBuffer>& uniform,
                                 const std::shared_ptr<Material>&      material )
             : MaterialHelper::MaterialWrapper( material ), m_UniformBuffer( uniform )
        {
        }

        void UpdateUBGlobal( GlobalUB&& ubGlobal )
        {
            m_GlobalUB = std::move( ubGlobal );
            m_Material->AddUniformToOverride( m_UniformBuffer );
        }

    private:
        GlobalUB                             m_GlobalUB;
        const std::shared_ptr<UniformBuffer> m_UniformBuffer;
    };
} // namespace Desert::Graphic