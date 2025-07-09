#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Graphic/Materials/Models/ToneMap.hpp>

namespace Desert::Graphic
{
    class MaterialTonemap
    {
    public:
        explicit MaterialTonemap();

        // Parameter updates
        void UpdateRenderParameters( const std::shared_ptr<Image2D>& targetImage );

        const auto& GetMaterialExecutor() const
        {
            return m_Material;
        }

    private:
        std::shared_ptr<MaterialExecutor> m_Material;

    private:
        std::unique_ptr<Models::ToneMap> m_ToneMapModel;
    };
} // namespace Desert::Graphic