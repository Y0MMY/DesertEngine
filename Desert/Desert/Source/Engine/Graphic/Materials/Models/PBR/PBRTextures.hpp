#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapperTextureCubeArray.hpp>
#include <Engine/Graphic/Materials/MaterialWrapperTexture2D.hpp>

namespace Desert::Graphic::Models::PBR
{
    struct PBRTextures
    {
        std::shared_ptr<ImageCube> IrradianceMap;
        std::shared_ptr<ImageCube> PreFilteredMap;
    };

    class PBRMaterialTexture final : public MaterialHelper::MaterialWrapperTextureCubeArray,
                                     MaterialHelper::MaterialWrapperTexture2D
    {
    public:
        PBRMaterialTexture( const std::shared_ptr<Material>& baseMaterial );

        void UpdatePBR( PBRTextures&& pbr );

        static const std::string_view GetUniformIrradianceName();
        static const std::string_view GetUniformPreFilteredName();
        static const std::string_view GetUniformBRDFLutName();

    private:
        PBRTextures m_PBRPBRTextures{};
    };
} // namespace Desert::Graphic::Models::PBR