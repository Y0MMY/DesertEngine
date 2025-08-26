#pragma stage : vertex

#version 450

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Bitangent;
layout(location = 4) in vec2 a_TextureCoord;

layout( push_constant ) uniform constants
{
    mat4 ViewProject;
    mat4 Transform;
} m_PushConstants;

layout(std140, binding = 0) uniform OutlineUBVertex
{
    float u_OutlineWidth;
};

void main()
{
	gl_Position = m_PushConstants.ViewProject * m_PushConstants.Transform * vec4(a_Position * u_OutlineWidth, 1.0) ;
}

#pragma stage : fragment
#version 450

layout(location = 0) out vec4 o_Color;

layout(std140, binding = 1) uniform OutlineUBFragment
{
	vec3 u_OutlineColor;
};

void main()
{
    o_Color = vec4(u_OutlineColor, 1.0);
}