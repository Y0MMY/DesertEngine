#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>

namespace Desert::Graphic::Render
{
    struct PointLightCommand : RenderCommand
    {
        ShaderProtocols::PointLightPayload Light;

        PointLightCommand( const ShaderProtocols::PointLightPayload& light ) : Light( light )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.AddPointLight( std::move( Light ) );
        }
    };
} // namespace Desert::Graphic::Render