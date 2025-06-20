#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapperTextureCubeArray.hpp>

namespace Desert::Graphic::Models::PBR
{
    struct PBRTextures
    {
        std::shared_ptr<ImageCube> IrradianceMap;
        std::shared_ptr<ImageCube> PreFilteredMap;
    };

    class PBRMaterialTexture final : public MaterialHelper::MaterialWrapperTextureCubeArray
    {
    public:
        PBRMaterialTexture( const std::shared_ptr<Material>&                   baseMaterial,
                            const std::shared_ptr<Uniforms::UniformImageCube>& uniformIrradianceMap,
                            const std::shared_ptr<Uniforms::UniformImageCube>& uniformPreFilteredMap );

        void UpdatePBR( PBRTextures&& pbr );

        static const std::string_view GetUniformIrradianceName();
        static const std::string_view GetUniformPreFilteredName();

    private:
        std::shared_ptr<Uniforms::UniformImageCube> m_UniformIrradianceMap;
        std::shared_ptr<Uniforms::UniformImageCube> m_UniformPreFilteredMap;
        PBRTextures                                 m_PBRPBRTextures{};
    };
} // namespace Desert::Graphic::Models::PBR