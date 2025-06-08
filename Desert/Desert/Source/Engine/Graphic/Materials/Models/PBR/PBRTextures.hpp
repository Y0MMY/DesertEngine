#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapper.hpp>

namespace Desert::Graphic::Models::PBR
{
    struct PBRTextures
    {
        std::shared_ptr<ImageCube> IrradianceMap;
        std::shared_ptr<ImageCube> PreFilteredMap;
    };

    class PBRMaterialTexture : public MaterialHelper::MaterialWrapper
    {
    public:
        using MaterialWrapper::MaterialWrapper;

        void UpdatePBR( PBRTextures&& pbr );

    private:
        PBRTextures m_PBRPBRTextures;
    };
} // namespace Desert::Graphic::Models::PBR