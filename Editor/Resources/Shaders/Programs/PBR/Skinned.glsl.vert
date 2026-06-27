#version 450

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Bitangent;
layout(location = 4) in vec2 a_TextureCoord;
layout(location = 5) in ivec4 a_BoneIndices;
layout(location = 6) in vec4  a_BoneWeights;

#include "../../Common/CameraUB.glslh"

// Must match PBR.glsl.frag / Static.glsl.vert push block.
layout(push_constant) uniform PushConstants
{
    mat4 Transform;     // offset 0
    uint MaterialIndex; // offset 64
} m_PushConstants;

layout(binding = 1) readonly buffer Bones
{
    mat4 BoneMatrices[];
} bones;

layout(location = 0) out Vertex
{
    vec3 WorldPosition;
    vec3 Normal;
    vec2 Texcoord;
    mat3 TBN;
    vec3 CameraPosition;
} outVertex;

void main()
{
    // ------------------------------------------------------------
    // 1. GPU Skinning
    // ------------------------------------------------------------
    mat4 skinMatrix =
          bones.BoneMatrices[a_BoneIndices.x] * a_BoneWeights.x +
          bones.BoneMatrices[a_BoneIndices.y] * a_BoneWeights.y +
          bones.BoneMatrices[a_BoneIndices.z] * a_BoneWeights.z +
          bones.BoneMatrices[a_BoneIndices.w] * a_BoneWeights.w;

    vec4 skinnedPosition = skinMatrix * vec4(a_Position, 1.0);
    vec3 skinnedNormal   = mat3(skinMatrix) * a_Normal;
    vec3 skinnedTangent  = mat3(skinMatrix) * a_Tangent;
    vec3 skinnedBitangent= mat3(skinMatrix) * a_Bitangent;

    // ------------------------------------------------------------
    // 2. World space
    // ------------------------------------------------------------
    mat4 model = m_PushConstants.Transform;
    mat3 normalMatrix = transpose(inverse(mat3(model)));

    vec4 worldPos = model * skinnedPosition;

    outVertex.WorldPosition = worldPos.xyz;
    outVertex.Normal        = normalize(normalMatrix * skinnedNormal);
    outVertex.Texcoord      = vec2(a_TextureCoord.x, 1.0 - a_TextureCoord.y);

    vec3 T = normalize(normalMatrix * skinnedTangent);
    vec3 B = normalize(normalMatrix * skinnedBitangent);
    vec3 N = normalize(normalMatrix * skinnedNormal);

    outVertex.TBN = mat3(T, B, N);
    outVertex.CameraPosition = cameraUB.CameraPos;

    // ------------------------------------------------------------
    // 3. Clip space
    // ------------------------------------------------------------
    gl_Position = cameraUB.Projection * cameraUB.View * worldPos;
}
