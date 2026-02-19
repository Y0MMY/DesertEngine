#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

#include <Engine/Graphic/Materials/MaterialBindings/Tonemap/Tonemap.hpp>

namespace Desert::Graphic
{
    class MaterialTonemap final : public Material
    {
    public:
        explicit MaterialTonemap();

        // Parameter updates
        void Bind( const std::shared_ptr<Image2D>& targetImage );

    private:
        std::unique_ptr<MaterialHelper::TonemapBinding> m_TonemapBinding;
    };
} // namespace Desert::Graphic