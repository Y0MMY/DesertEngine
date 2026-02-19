#version 450 core

layout(location = 0) out vec4 oColor;

layout (binding = 1) uniform samplerCube samplerCubeMap;

layout(location = 3) in vec3 inUVW;
layout(location = 4) in vec3  v_Position;

void main()
{
	oColor = texture(samplerCubeMap, v_Position);
}