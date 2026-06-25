#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Graphic/ShaderProtocols/SpotLight.hpp>

namespace Desert::Graphic::Render
{
    struct SpotLightCommand : RenderCommand
    {
        ShaderProtocols::SpotLightPayload Light;

        SpotLightCommand( const ShaderProtocols::SpotLightPayload& light ) : Light( light )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.AddSpotLight( std::move( Light ) );
        }
    };
} // namespace Desert::Graphic::Render
