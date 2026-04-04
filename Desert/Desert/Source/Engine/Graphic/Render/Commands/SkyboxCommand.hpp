#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>

namespace Desert::Graphic::Render
{
    struct SkyboxCommand : RenderCommand
    {
        std::shared_ptr<MaterialSkybox> Skybox;

        SkyboxCommand( const std::shared_ptr<MaterialSkybox>& skybox ) : Skybox( skybox )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SetEnvironment( Skybox );
        }
    };
} // namespace Desert::Graphic::Render