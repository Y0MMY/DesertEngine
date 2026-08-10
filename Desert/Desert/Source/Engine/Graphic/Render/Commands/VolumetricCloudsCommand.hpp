#pragma once

#include "../RenderCommand.hpp"

#include <Engine/ECS/VolumetricCloudsComponent.hpp>

namespace Desert::Graphic::Render
{
    // Carries this frame's volumetric-cloud settings from the ECS to the cloud renderer.
    //
    // `Present` is said explicitly instead of being implied by the command's absence: the renderer keeps
    // its settings across frames, so a scene whose cloud component was deleted would otherwise go on
    // marching the last cloudscape it was told about. Exactly the reason ProceduralSkyCommand is emitted
    // with Enabled = false rather than not emitted at all.
    struct VolumetricCloudsCommand : RenderCommand
    {
        bool                     Present;
        ECS::VolumetricCloudData Data;

        VolumetricCloudsCommand( bool present, const ECS::VolumetricCloudData& data )
             : Present( present ), Data( data )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SetVolumetricClouds( Present, Data );
        }
    };
} // namespace Desert::Graphic::Render
