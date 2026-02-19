// https://github.com/PacktPublishing/3D-Graphics-Rendering-Cookbook/blob/35bb8b717d6fb52ced60509be1bd4b99ad6d9a01/data/shaders/chapter05/GL01_grid.frag
#version 450

#include "Common/CameraUB.glslh"

layout (location=0) out vec2 uv;
layout (location=1) out vec2 out_camPos;

const vec3 pos[4] = vec3[4](
	vec3(-1.0, 0.0, -1.0),
	vec3( 1.0, 0.0, -1.0),
	vec3( 1.0, 0.0,  1.0),
	vec3(-1.0, 0.0,  1.0)
);

const int indices[6] = int[6](
	0, 1, 2, 2, 3, 0
);

const float gridYOffset = -1.5;

void main()
{
	float gridSize = 100.0;

	mat4 MVP = cameraUB.Projection * cameraUB.View;

	int idx = indices[gl_VertexIndex];
	vec3 position = pos[idx] * gridSize;
	
	position.x += cameraUB.CameraPos.x;
	position.z += cameraUB.CameraPos.z;

	out_camPos = cameraUB.CameraPos.xz;

	position.y += gridYOffset;

	gl_Position = MVP * vec4(position, 1.0);
	uv = position.xz;
}
