#pragma stage : compute

#version 450 core
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(binding = 0) uniform sampler2D u_InputTexture;
layout(binding = 1) uniform writeonly image2D u_OutputTexture;


void main() {
    ivec2 globalCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outputSize = imageSize(u_OutputTexture);

    if (globalCoord.x >= outputSize.x || globalCoord.y >= outputSize.y) {
        return;
    }

    ivec2 srcCoord = globalCoord * 2;

    vec4 s00 = texelFetch(u_InputTexture, srcCoord + ivec2(0, 0), 0);
    vec4 s10 = texelFetch(u_InputTexture, srcCoord + ivec2(1, 0), 0);
    vec4 s01 = texelFetch(u_InputTexture, srcCoord + ivec2(0, 1), 0);
    vec4 s11 = texelFetch(u_InputTexture, srcCoord + ivec2(1, 1), 0);

    vec4 color = (s00 + s10 + s01 + s11) * 0.25;
    imageStore(u_OutputTexture, globalCoord, color);
}