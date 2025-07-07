#pragma once
#include <glm/glm.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Graphic::Models::PBR
{
    struct PBRMaterialPropertiesUB
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

    class MaterialPBRProperties final : public MaterialHelper::MaterialWrapper
    {
    public:
        explicit MaterialPBRProperties( const std::shared_ptr<MaterialExecutor>& material )
             : MaterialWrapper( material, "MaterialProperties" ), m_Data( {} )
        {
        }

        void Update( const PBRMaterialPropertiesUB& props )
        {
            m_Data = props;
            m_UniformProperty->SetData( &m_Data, sizeof( PBRMaterialPropertiesUB ) );
        }

    private:
        PBRMaterialPropertiesUB m_Data;
    };
} // namespace Desert::Graphic::Models