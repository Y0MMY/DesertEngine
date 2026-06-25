#version 450

layout(location = 0) in  vec4 v_Color;
layout(location = 0) out vec4 oColor;

void main()
{
    oColor = v_Color;
}
