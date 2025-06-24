#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapper
    {
    public:
        explicit MaterialWrapper( const std::shared_ptr<Material>& baseMaterial, const std::string& uniformName )
             : m_Material( baseMaterial ), m_UniformName( uniformName )
        {
            m_UniformProperty = m_Material->GetUniformBufferProperty( uniformName );
        }

        const auto& GetMaterialInstance() const
        {
            return m_Material;
        }

    protected:
        std::shared_ptr<Material>              m_Material;
        std::string                            m_UniformName;
        std::shared_ptr<UniformBufferProperty> m_UniformProperty;
    };
} // namespace Desert::Graphic::MaterialHelper