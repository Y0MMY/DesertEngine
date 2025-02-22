#pragma stage : vertex

#version 450 

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

layout(binding = 0) uniform camera {
    mat4 projection;
    mat4 view;
} ubo;

layout(location =3) out vec3 outUVW ;
layout(location =4) out vec3   v_Position ;

void main()
{
    vec4 position = vec4(aPosition.xy, 1.0, 1.0);
	gl_Position = position;

    mat4 inverseVP = inverse(ubo.projection * ubo.view);

	v_Position = ((inverseVP * position).xyz);
    v_Position.xy *= -1.0;

}

#pragma stage : fragment

#version 450 core

layout(location = 0) out vec4 oColor;

layout (binding = 1) uniform samplerCube samplerCubeMap;

layout(location = 3) in vec3 inUVW;
layout(location = 4) in vec3  v_Position;

void main()
{
	oColor = texture(samplerCubeMap, v_Position);
}