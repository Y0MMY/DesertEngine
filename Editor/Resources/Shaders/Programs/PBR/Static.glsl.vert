#version 450 

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Bitangent;
layout(location = 4) in vec2 a_TextureCoord;

#include "../../Common/CameraUB.glslh"

layout( push_constant ) uniform constants
{
	mat4 Transform;
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
	outVertex.WorldPosition = vec3(m_PushConstants.Transform * vec4(a_Position, 1.0));
	outVertex.Texcoord = vec2(a_TextureCoord.x, 1.0 - a_TextureCoord.y);

	mat3 modelRotation =  transpose(inverse(mat3(m_PushConstants.Transform)));
	outVertex.Normal = 	mat3(m_PushConstants.Transform) * a_Normal;
	outVertex.TBN = modelRotation * mat3(a_Tangent, a_Bitangent, a_Normal);
	outVertex.CameraPosition = cameraUB.CameraPos;

	gl_Position =  cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4(a_Position, 1.0);
}
