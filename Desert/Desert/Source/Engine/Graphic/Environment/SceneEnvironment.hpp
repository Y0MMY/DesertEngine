#pragma once

#include <Engine/Graphic/Texture.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>

namespace Desert::Graphic
{
    struct Environment
    {
        Common::Filepath           Filepath;
        std::shared_ptr<ImageCube> RadianceMap;
        std::shared_ptr<ImageCube> IrradianceMap;
        std::shared_ptr<ImageCube> PreFilteredMap;

        operator bool() const
        {
            return RadianceMap && IrradianceMap && PreFilteredMap;
        }
    };

    class EnvironmentManager
    {
    public:
        static Environment Create( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAsset );
    };
} // namespace Desert::Graphic