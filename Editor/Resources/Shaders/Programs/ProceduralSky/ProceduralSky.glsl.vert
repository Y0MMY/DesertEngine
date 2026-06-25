#version 450

#include "Common/QuadPositions.glslh"
#include "Common/CameraUB.glslh"

// World-space view ray for this fullscreen pixel (un-normalized; normalized in the fragment).
layout(location = 0) out vec3 v_RayDir;

void main()
{
    vec4 position = vec4(QUAD_POSITIONS[gl_VertexIndex], 1.0, 1.0);
    gl_Position = position;

    // Reconstruct the world-space view ray for this pixel: unproject the far-plane clip position, do
    // the perspective divide, then subtract the camera position (so the ray actually starts at the
    // camera — required for a correct horizon, unlike the magnitude-agnostic cubemap lookup).
    mat4 inverseVP = inverse(cameraUB.Projection * cameraUB.View);
    vec4 worldH    = inverseVP * position;
    vec3 worldPos  = worldH.xyz / worldH.w;
    v_RayDir       = worldPos - cameraUB.CameraPos;
}
