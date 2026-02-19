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