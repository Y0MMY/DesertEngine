#version 450 

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Bitangent;
layout(location = 4) in vec2 a_TextureCoord;

#include "../../Common/CameraUB.glslh"

// Shared push-constant block. Must be byte-for-byte identical to the one in PBR.glsl.frag so the
// reflected range (offset/size) matches across stages. The vertex stage only reads Transform; the
// per-object material parameters are consumed by the fragment stage. Per-object data lives here
// (not in a uniform buffer) so each draw carries its own values — a shared material UB would be
// overwritten by later objects in the same frame (last-write-wins) before the GPU executes the draws.
// Must match PBR.glsl.frag / Skinned.glsl.vert. Material data lives in a storage buffer (read in the
// fragment); the vertex stage only needs Transform.
layout( push_constant ) uniform PushConstants
{
	mat4 Transform;     // offset 0
	uint MaterialIndex; // offset 64
} m_PushConstants;


layout(location=0) out Vertex
{
	vec3 WorldPosition;
	vec3 Normal;
	vec2 Texcoord;
	mat3 TBN;
	vec3 CameraPosition;
} outVertex;

void main()
{
	outVertex.WorldPosition  = vec3(m_PushConstants.Transform * vec4(a_Position, 1.0));
	outVertex.Texcoord       = vec2(a_TextureCoord.x, 1.0 - a_TextureCoord.y);
	outVertex.CameraPosition = cameraUB.CameraPos;

	mat3 normalMatrix = transpose(inverse(mat3(m_PushConstants.Transform)));

	vec3 T = normalize(normalMatrix * a_Tangent);
	vec3 B = normalize(normalMatrix * a_Bitangent);
	vec3 N = normalize(normalMatrix * a_Normal);

	outVertex.Normal = N;
	outVertex.TBN    = mat3(T, B, N);

	gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4(a_Position, 1.0);
}
