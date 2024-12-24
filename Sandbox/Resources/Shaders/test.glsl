#pragma stage : vertex

#version 450 


vec2 pos[3] = vec2[3] (vec2(-0.7, 0.7), vec2(0.7, 0.7), vec2(0.7, -0.7));

void main()
{
   gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
}


#pragma stage : fragment

#version 450 core

layout(location = 0) out vec4 oColor;

void main()
{
	oColor = vec4(0.0, 0.4, 1.0, 1.0);
}