#version 450

#include "Common/QuadPositions.glslh"
#include "Common/CameraUB.glslh"

// World-space view ray for this fullscreen pixel (un-normalized; normalized in the fragment).
layout(location = 0) out vec3 v_RayDir;

void main()
{
    vec4 position = vec4(QUAD_POSITIONS[gl_VertexIndex], 1.0, 1.0);
    gl_Position = position;

    // DIRECTION-ONLY world-space view ray (camera rotation only — NO far-plane-worldPos minus cameraPos).
    // That subtraction of two large world-space points loses float precision and, as the camera MOVES, makes
    // the ray direction jitter frame-to-frame → the tiny sun/stars "boil". Unproject to VIEW space, then
    // rotate to world with mat3(invView). Correct for the artistic sky (depends only on direction); the
    // clouds use the camera position separately via the SkyUB.
    vec4 viewH   = inverse(cameraUB.Projection) * position;
    vec3 viewRay = viewH.xyz / viewH.w;
    v_RayDir     = mat3(inverse(cameraUB.View)) * viewRay;
}
