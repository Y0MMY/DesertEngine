#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapperTextureCube
    {
    public:
        explicit MaterialWrapperTextureCube( const std::shared_ptr<MaterialExecutor>& baseMaterial,
                                             const std::string&               uniformName )
             : m_Material( baseMaterial ), m_UniformName( uniformName )

        {
            m_UniformProperty = m_Material->GetTextureCubeProperty( uniformName );
        }

        const auto& GetMaterialPBR() const
        {
            return m_Material;
        }

    protected:
        std::shared_ptr<MaterialExecutor>            m_Material;
        std::string                          m_UniformName;
        std::shared_ptr<TextureCubeProperty> m_UniformProperty;
    };

} // namespace Desert::Graphic::MaterialHelper