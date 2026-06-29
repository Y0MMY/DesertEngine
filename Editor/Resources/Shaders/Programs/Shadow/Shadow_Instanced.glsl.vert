#version 450

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Bitangent;
layout(location = 4) in vec2 a_TextureCoord;

// The shared CameraUB is fed the LIGHT's view/projection by MaterialShadowInstanced (not the camera's).
#include "../../Common/CameraUB.glslh"

// Per-instance world transforms (binding 17), indexed by gl_InstanceIndex. Anonymous block so reflection
// registers it under the BLOCK name "InstanceTransforms" (matches Static_Instanced.glsl.vert and the C++
// Get<StorageBufferProperty>("InstanceTransforms")). One instanced draw renders all N shadow casters.
// Binding 17 (not 16) to stay consistent with the PBR instanced vertex, where 16 collides with SpotLightsUB.
layout( std430, binding = 17 ) readonly buffer InstanceTransforms
{
    mat4 transforms[];
};

void main()
{
    mat4 model  = transforms[gl_InstanceIndex];
    gl_Position = cameraUB.Projection * cameraUB.View * model * vec4(a_Position, 1.0);
}
