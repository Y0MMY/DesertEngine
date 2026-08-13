#pragma once

#include <glm/glm.hpp>

#include <string>

namespace Desert::Editor
{
    // Command-line SCREENSHOT MODE: load a scene, point the camera, render a fixed number of frames, write
    // the viewport to a PNG and exit.
    //
    // It exists because the alternative is worse. Every cloud defect this engine has shipped — a
    // tonemapper that was the identity, a radiance five thousand times too large, a shadow map marching
    // the far side of the planet — is one that a person looking at the screen catches in a minute, and the
    // people looking at this screen are outnumbered by the parameters. This turns "does it look right" from
    // a question someone has to be present to answer into one command.
    //
    // The frame count matters and is not decoration: the volumetric clouds accumulate over roughly ten
    // frames (Temporal Blend Factor 0.10), so a shot taken on frame one is a picture of the dither.
    struct ShotOptions
    {
        static ShotOptions& Get()
        {
            static ShotOptions s;
            return s;
        }

        std::string Scene;  // --scene <path.desce>, overrides the project's default scene
        std::string Output; // --shot <out.png>; empty = normal interactive editor
        int         Frames = 90;

        bool      HasCamera = false;
        glm::vec3 Position{ 0.0f, 200.0f, 0.0f }; // --camera x,y,z   (world units)
        glm::vec3 Forward{ 0.0f, 0.30f, -1.0f };  // --look   x,y,z   (need not be normalized)

        bool Active() const
        {
            return !Output.empty();
        }
    };
} // namespace Desert::Editor
