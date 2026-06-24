#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    class MaterialTonemap final : public Material
    {
    public:
        explicit MaterialTonemap();

        void Bind( const std::shared_ptr<Image2D>& targetImage );

    private:
        Texture2DProperty* m_GeometryTexture = nullptr;
    };
} // namespace Desert::Graphic
