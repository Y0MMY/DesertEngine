// MaterialProperties.h
#pragma once
#include <glm/glm.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Graphic::Models::PBR
{
    struct MaterialPropertiesUB
    {
        glm::vec3 AlbedoColor;
        float     AlbedoBlend;
        float     MetallicValue;
        float     MetallicBlend;
        float     RoughnessValue;
        float     RoughnessBlend;
        glm::vec3 EmissionColor;
        float     EmissionStrength;
        float     AOValue;
    };

    class MaterialProperties final : public MaterialHelper::MaterialWrapper
    {
    public:
        explicit MaterialProperties( const std::shared_ptr<Material>& material )
             : MaterialWrapper( material, "MaterialProperties" ), m_Data( {} )
        {
        }

        void Update( const MaterialPropertiesUB& props )
        {
            m_Data = props;
            m_UniformProperty->SetData( &m_Data, sizeof( MaterialPropertiesUB ) );
        }

    private:
        MaterialPropertiesUB m_Data;
    };
} // namespace Desert::Graphic::Models