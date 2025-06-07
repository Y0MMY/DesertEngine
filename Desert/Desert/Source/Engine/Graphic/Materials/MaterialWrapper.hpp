#pragma once

#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapper
    {
    public:
        explicit MaterialWrapper( const std::shared_ptr<Material>&      baseMaterial,
                                  const std::shared_ptr<UniformBuffer>& uniform )
             : m_Material( baseMaterial ), m_Uniform( uniform )
        {
        }
        void Bind() const
        {
            m_Material->AddUniformToOverride( m_Uniform );
        }
        const auto& GetMaterialInstance() const
        {
            return m_Material;
        }

    protected:
        std::shared_ptr<Material>      m_Material;
        std::shared_ptr<UniformBuffer> m_Uniform;
    };
} // namespace Desert::Graphic::MaterialHelper