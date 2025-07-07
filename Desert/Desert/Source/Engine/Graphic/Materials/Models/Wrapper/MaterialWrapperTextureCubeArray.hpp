#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapperTextureCubeArray
    {
    public:
        explicit MaterialWrapperTextureCubeArray( const std::shared_ptr<MaterialExecutor>& baseMaterial,
                                                  const std::vector<std::string>&  uniformNames )
             : m_Material( baseMaterial ), m_UniformNames( uniformNames )
        {
            m_UniformProperties.reserve( uniformNames.size() );
            for ( const auto& name : uniformNames )
            {
                m_UniformProperties.push_back( m_Material->GetTextureCubeProperty( name ) );
            }
        }

        const auto& GetMaterialPBR() const
        {
            return m_Material;
        }

    protected:
        std::shared_ptr<MaterialExecutor>                         m_Material;
        std::vector<std::string>                          m_UniformNames;
        std::vector<std::shared_ptr<TextureCubeProperty>> m_UniformProperties;
    };

} // namespace Desert::Graphic::MaterialHelper