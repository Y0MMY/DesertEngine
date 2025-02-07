#pragma stage : vertex

#version 450 

layout(location = 0) in vec3 aPosition;

layout(binding = 0) uniform camera {
    mat4  mvp;
} ubo;

layout(location =3) out vec2 vTexCoord;

void main()
{
vTexCoord = (aPosition.xy + 1.0) * 0.5; 
   gl_Position = ubo.mvp * vec4(aPosition, 1.0);
}


#pragma stage : fragment

#version 450 core

layout(location = 0) out vec4 oColor;

layout(binding = 2) uniform sampler2D texSampler;

layout(location = 3) in vec2 vTexCoord;

void main()
{
	oColor = texture(texSampler, vTexCoord);
}