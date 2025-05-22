#pragma once

#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MateriaTtechniques
    {
    public:
        explicit MateriaTtechniques( const std::shared_ptr<Material>& baseMaterial ) : m_Material( baseMaterial )
        {
        }
        //  virtual std::vector<std::pair<std::string, std::shared_ptr<Graphic::Image2D>>> GetBindlessData() = 0;

        void Bind() const
        {
            m_Material->ApplyMaterial();
        }

    protected:
        std::shared_ptr<Material> m_Material;
    };
} // namespace Desert::Graphic::MaterialHelper