#pragma once

#include <Engine/Graphic/Texture.hpp>

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
        static Environment Create( const Common::Filepath& filepath );
    };
} // namespace Desert::Graphic