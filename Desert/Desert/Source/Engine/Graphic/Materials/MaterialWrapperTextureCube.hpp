#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Uniforms/UniformManager.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapperTextureCube
    {
    public:
        explicit MaterialWrapperTextureCube( const std::shared_ptr<Material>&                   baseMaterial,
                                             const std::shared_ptr<Uniforms::UniformImageCube>& uniform )
             : m_Material( baseMaterial ), m_Uniform( uniform )
        {
        }
        void Bind() const
        {
            if ( m_Uniform )
            {
                m_Material->AddUniformCubeToOverride( m_Uniform );
            }
        }
        const auto& GetMaterialInstance() const
        {
            return m_Material;
        }

    protected:
        std::shared_ptr<Material>                   m_Material;
        std::shared_ptr<Uniforms::UniformImageCube> m_Uniform;
    };

} // namespace Desert::Graphic::MaterialHelper