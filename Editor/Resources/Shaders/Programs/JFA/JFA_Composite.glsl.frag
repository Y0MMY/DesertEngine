#version 450

layout(location = 0) in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Color;

layout(set = 0, binding = 0) uniform sampler2D u_OutlineResult;

void main()
{
    o_Color = texture(u_OutlineResult, v_TexCoord);
}
