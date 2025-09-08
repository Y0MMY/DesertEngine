#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

#include <Engine/Graphic/Materials/Models/ToneMap.hpp>

namespace Desert::Graphic
{
    class MaterialTonemap final : public Material
    {
    public:
        explicit MaterialTonemap();

        // Parameter updates
        void Bind( const std::shared_ptr<Image2D>& targetImage );

    private:
        std::unique_ptr<Models::ToneMap> m_ToneMapModel;
    };
} // namespace Desert::Graphic