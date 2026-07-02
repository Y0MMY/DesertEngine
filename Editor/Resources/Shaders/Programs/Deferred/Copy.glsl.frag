#version 450 core

// Trivial full-screen copy: samples an input image and writes it out. Used to snapshot the composited scene
// colour into a separate texture so the glass pass can sample it (refraction) without a read+write feedback
// loop on the scene target.

layout(location = 0) in vec2 v_TexCoord;

layout(binding = 1) uniform sampler2D u_Input;

layout(location = 0) out vec4 oColor;

void main()
{
	oColor = texture(u_Input, v_TexCoord);
}
