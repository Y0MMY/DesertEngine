#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Uniforms/UniformManager.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapperTextureCubeArray
    {
    public:
        explicit MaterialWrapperTextureCubeArray(
             const std::shared_ptr<Material>&                                baseMaterial,
             const std::vector<std::shared_ptr<Uniforms::UniformImageCube>>& uniforms )
             : m_Material( baseMaterial ), m_Uniforms( uniforms )
        {
        }
        void Bind() const
        {
            for ( const auto& uniform : m_Uniforms )
            {
                m_Material->AddUniformCubeToOverride( uniform );
            }
        }
        const auto& GetMaterialInstance() const
        {
            return m_Material;
        }

    protected:
        std::shared_ptr<Material>                                m_Material;
        std::vector<std::shared_ptr<Uniforms::UniformImageCube>> m_Uniforms;
    };

} // namespace Desert::Graphic::MaterialHelper