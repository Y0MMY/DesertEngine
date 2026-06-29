#version 450

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Bitangent;
layout(location = 4) in vec2 a_TextureCoord;

#include "../../Common/CameraUB.glslh"

// Per-instance world transforms — the model matrix comes from here (indexed by gl_InstanceIndex), instead
// of the per-draw push-constant Transform used by the non-instanced Static.glsl.vert. One instanced draw
// (instanceCount = N) renders all N transforms.
// Anonymous block (no instance name) so shader reflection registers it under the BLOCK name
// "InstanceTransforms" — matching MeshRenderer's Get<StorageBufferProperty>("InstanceTransforms").
// (SPIRV-Cross names an SSBO by its instance/variable name when present; the Materials SSBO works the
// same way precisely because it is anonymous.) Members are accessed in global scope: transforms[i].
// Binding 17: PBR.glsl.frag (the shared fragment) already uses binding 16 for SpotLightsUB
// (Spotlight.glslh) — a vertex SSBO at 16 would COLLIDE in the merged descriptor set and the slot would
// resolve to SpotLightsUB, feeding garbage transforms (degenerate, invisible meshes). Keep 17 free.
layout( std430, binding = 17 ) readonly buffer InstanceTransforms
{
	mat4 transforms[];
};

// Kept byte-identical to Static.glsl.vert / PBR.glsl.frag so the reflected push range matches. The
// instanced vertex IGNORES Transform (the instance SSBO supplies the model matrix); MaterialIndex is still
// used by the fragment stage.
layout( push_constant ) uniform PushConstants
{
	mat4 Transform;     // offset 0  — unused here
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
	mat4 model = transforms[gl_InstanceIndex];

	outVertex.WorldPosition  = vec3(model * vec4(a_Position, 1.0));
	outVertex.Texcoord       = vec2(a_TextureCoord.x, 1.0 - a_TextureCoord.y);
	outVertex.CameraPosition = cameraUB.CameraPos;

	mat3 normalMatrix = transpose(inverse(mat3(model)));

	vec3 T = normalize(normalMatrix * a_Tangent);
	vec3 B = normalize(normalMatrix * a_Bitangent);
	vec3 N = normalize(normalMatrix * a_Normal);

	outVertex.Normal = N;
	outVertex.TBN    = mat3(T, B, N);

	gl_Position = cameraUB.Projection * cameraUB.View * model * vec4(a_Position, 1.0);
}
