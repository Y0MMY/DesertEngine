#pragma once

#include "../RenderCommand.hpp"

#include <Engine/ECS/VolumetricCloudsComponent.hpp>
#include <Engine/Graphic/Clouds/CloudVolumePlacement.hpp>

#include <utility>

namespace Desert::Graphic::Render
{
    // Carries this frame's volumetric-cloud settings — and the hero clouds placed in the scene — from the
    // ECS to the cloud renderer.
    //
    // `Present` is said explicitly instead of being implied by the command's absence: the renderer keeps
    // its settings across frames, so a scene whose cloud component was deleted would otherwise go on
    // marching the last cloudscape it was told about. Exactly the reason ProceduralSkyCommand is emitted
    // with Enabled = false rather than not emitted at all.
    //
    // `Volumes` rides in the SAME command, and is sent whether or not it is empty, for that same reason:
    // an empty list is the instruction to release every atlas tile, and a scene that lost its last hero
    // cloud has to be able to say so. It is deliberately independent of `Present` — a Cloud Volume entity
    // is its own thing, and a scene with hero clouds but no cloud layer component simply does not march.
    struct VolumetricCloudsCommand : RenderCommand
    {
        bool                     Present;
        ECS::VolumetricCloudData Data;
        CloudVolumePlacements    Volumes;

        VolumetricCloudsCommand( bool present, const ECS::VolumetricCloudData& data,
                                 CloudVolumePlacements volumes )
             : Present( present ), Data( data ), Volumes( std::move( volumes ) )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SetVolumetricClouds( Present, Data, Volumes );
        }
    };
} // namespace Desert::Graphic::Render
