#pragma once

#include "../RenderCommand.hpp"

#include <Engine/ECS/VolumetricCloudComponent.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic::Render
{
    // Carries this frame's cloud layer from the ECS to the cloud renderer.
    //
    // `Present` is said explicitly instead of being implied by the command's absence: the renderer keeps
    // its settings across frames, so a scene whose cloud component was deleted would otherwise go on
    // marching the last layer it was told about.
    //
    // `WindOffset` rides here rather than being recomputed by the renderer because the accumulator needs
    // the frame's timestep, which only the ECS tick has. Two accumulators — one per side — would drift
    // apart the first time a frame was skipped on one of them, and the symptom would be the sky sliding
    // relative to its own shadows.
    struct VolumetricCloudCommand : RenderCommand
    {
        bool                     Present;
        ECS::VolumetricCloudData Data;
        glm::vec3                WindOffset; // world units, accumulated

        VolumetricCloudCommand( bool present, const ECS::VolumetricCloudData& data, const glm::vec3& windOffset )
             : Present( present ), Data( data ), WindOffset( windOffset )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SetVolumetricClouds( Present, Data, WindOffset );
        }
    };
} // namespace Desert::Graphic::Render
