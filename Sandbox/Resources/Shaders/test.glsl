#pragma stage : vertex

#version 450 

layout(location = 0) in vec3 aPosition;

layout(binding = 0) uniform camera {
    mat4  mvp;
} ubo;

void main()
{
   gl_Position = ubo.mvp * vec4(aPosition, 1.0);
}


#pragma stage : fragment

#version 450 core

layout(location = 0) out vec4 oColor;

void main()
{
	oColor = vec4(1.0, 1.0, 1.0, 1.0);
}