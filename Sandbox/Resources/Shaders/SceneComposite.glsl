#pragma stage : vertex

#version 450 

layout(location = 0) in vec3 a_Position;  

layout(location = 0) out vec2 v_TexCoord; 

vec2 textureCoords[4] = 
{
    {0, 1},
    {0, 0},
    {1, 1},
    {1, 0}
};

void main()
{
    v_TexCoord = textureCoords[gl_VertexIndex];
    gl_Position = vec4(a_Position, 1.0);
}

#pragma stage : fragment

#version 450 core

layout(location = 0) in vec2 v_TexCoord; 

layout(binding = 2) uniform sampler2D u_GeometryTexture; 

layout(location = 0) out vec4 oColor; 

void main()
{
    vec4 color = texture(u_GeometryTexture, v_TexCoord);

    color.rgb = pow(color.rgb, vec3(1.0 / 2.2));

    oColor = color;
}