#version 450

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Bitangent;
layout(location = 4) in vec2 a_TextureCoord;
layout(location = 5) in ivec4 a_BoneIndices;
layout(location = 6) in vec4  a_BoneWeights;

#include "../../Common/CameraUB.glslh"

layout( push_constant ) uniform constants
{
    mat4 Transform;
} m_PushConstants;

// Same bone matrices the skinned mesh is rendered with, so the silhouette (hence the outline) matches the
// posed/animated mesh exactly. Binding 1 mirrors Skinned.glsl.vert.
layout(binding = 1) readonly buffer Bones
{
    mat4 BoneMatrices[];
} bones;

void main()
{
    mat4 skin = bones.BoneMatrices[a_BoneIndices.x] * a_BoneWeights.x +
                bones.BoneMatrices[a_BoneIndices.y] * a_BoneWeights.y +
                bones.BoneMatrices[a_BoneIndices.z] * a_BoneWeights.z +
                bones.BoneMatrices[a_BoneIndices.w] * a_BoneWeights.w;

    gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * ( skin * vec4(a_Position, 1.0) );
}
