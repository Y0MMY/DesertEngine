#pragma once

#include <Engine/Graphic/Texture.hpp>
#include <Engine/Runtime/ImageHandle.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>

namespace Desert::Graphic
{
    struct Environment
    {
        Common::Filepath     Filepath; // TODO: Asset Env
        Runtime::ImageHandle RadianceMap;
        Runtime::ImageHandle IrradianceMap;
        Runtime::ImageHandle PreFilteredMap;

        operator bool() const
        {
            return RadianceMap.IsValid() && IrradianceMap.IsValid() && PreFilteredMap.IsValid();
        }
    };

    class EnvironmentManager
    {
    public:
        static Environment Create( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAsset );

    private:
        static std::shared_ptr<ImageCube>
        ConvertPanoramaToCubemapCross( const std::shared_ptr<Texture2D>& texturePanorama );

        static std::shared_ptr<ImageCube>
        CreateDiffuseIrradiance( const std::shared_ptr<Texture2D>& texturePanorama );

        static std::shared_ptr<ImageCube>
        CreatePrefilteredMap( const std::shared_ptr<Texture2D>& texturePanorama );
    };
} // namespace Desert::Graphic