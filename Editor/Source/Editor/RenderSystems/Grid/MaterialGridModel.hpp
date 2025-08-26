#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Editor::Render::Model
{
    struct GridMaterialPropertiesUB
    {
        float CellSize;
        float CellScale;
    };

    class MaterialGridProperties final : public Desert::Graphic::MaterialHelper::MaterialWrapper
    {
    public:
        explicit MaterialGridProperties( const std::shared_ptr<Desert::Graphic::MaterialExecutor>& material )
             : MaterialWrapper( material, "GridUniforms" ), m_Data( {} )
        {
        }

        void Update( const GridMaterialPropertiesUB& props )
        {
            // m_Data = props;
            // m_UniformProperty->SetData( &m_Data, sizeof( GridMaterialPropertiesUB ) );
        }

    private:
        GridMaterialPropertiesUB m_Data;
    };
} // namespace Desert::Editor::Render::Model