#pragma stage : vertex

#version 450 

layout(location = 0) in vec3 aPosition;

// vec2 pos[3] = vec2[3] (vec2(-0.7, 0.7), vec2(0.7, 0.7), vec2(0.7, -0.7));

void main()
{
   gl_Position = vec4(aPosition, 1.0);
}


#pragma stage : fragment

#version 450 core

layout(location = 0) out vec4 oColor;

void main()
{
	oColor = vec4(0.0, 0.4, 1.0, 1.0);
}