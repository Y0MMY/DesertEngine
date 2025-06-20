#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Uniforms/UniformManager.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapper
    {
    public:
        explicit MaterialWrapper( const std::shared_ptr<Material>&      baseMaterial,
                                  const std::shared_ptr<Uniforms::UniformBuffer>& uniform )
             : m_Material( baseMaterial ), m_Uniform( uniform )
        {
            if ( m_Uniform )
            {
                m_Material->AddUniformBufferToOverride( m_Uniform );
            }
        }
        const auto& GetMaterialInstance() const
        {
            return m_Material;
        }

    protected:
        std::shared_ptr<Material>      m_Material;
        std::shared_ptr<Uniforms::UniformBuffer> m_Uniform;
    };
} // namespace Desert::Graphic::MaterialHelper