#pragma once

#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapper
    {
    public:
        explicit MaterialWrapper( const std::shared_ptr<Material>& baseMaterial ) : m_Material( baseMaterial )
        {
        }
        //  virtual std::vector<std::pair<std::string, std::shared_ptr<Graphic::Image2D>>> GetBindlessData() = 0;

        void Bind() const
        {
            //TODO: apply only this material
            
            //m_Material->ApplyMaterial();
        }
        const auto& GetMaterialInstance() const
        {
            return m_Material;
        }

    protected:
        std::shared_ptr<Material> m_Material;
    };
} // namespace Desert::Graphic::MaterialHelper