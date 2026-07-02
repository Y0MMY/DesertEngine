#version 450

#include "Common/QuadPositions.glslh"
#include "Common/QuadTextureCoords.glslh"

layout(location = 0) out vec2 v_TexCoord;

void main()
{
	v_TexCoord  = QUAD_TEXTURE_COORDINATES[gl_VertexIndex];
	gl_Position = vec4(QUAD_POSITIONS[gl_VertexIndex], 0.0, 1.0);
}
