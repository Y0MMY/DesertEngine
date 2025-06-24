#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapperTexture2D
    {
    public:
        explicit MaterialWrapperTexture2D( const std::shared_ptr<Material>& baseMaterial,
                                           const std::string&               uniformName )
             : m_Material( baseMaterial ), m_UniformName( uniformName )
        {
            m_UniformProperty = m_Material->GetTexture2DProperty( uniformName );
        }

        const auto& GetMaterialInstance() const
        {
            return m_Material;
        }

    protected:
        std::shared_ptr<Material>          m_Material;
        std::string                        m_UniformName;
        std::shared_ptr<Texture2DProperty> m_UniformProperty;
    };

} // namespace Desert::Graphic::MaterialHelper