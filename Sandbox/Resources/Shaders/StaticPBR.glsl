#pragma stage : vertex

#version 450 

layout(location = 0) in vec3 aPosition;

layout( push_constant ) uniform constants
{
	mat4 mvp;
} m_PushConstants;


layout(location = 0) out vec3 oPosition ;

void main()
{
	oPosition = aPosition;
	gl_Position = m_PushConstants.mvp * vec4(aPosition, 1.0);
}


#pragma stage : fragment

#version 450 core

layout(location = 0) in vec3 oPosition;

layout(location = 0) out vec4 oColor;

void main()
{
	oColor = vec4(oPosition, 1.0);
}