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
    vec4 clipPosition = m_PushConstants.ViewProject * m_PushConstants.Transform * vec4(a_Position, 1.0);
    vec3 clipNormal = normalize(mat3(m_PushConstants.ViewProject) * mat3(m_PushConstants.Transform) * a_Normal);
    float outlineWidth = u_OutlineWidth * 0.005; 
    clipPosition.xyz += clipNormal * outlineWidth * clipPosition.w;
    
    gl_Position = clipPosition;
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