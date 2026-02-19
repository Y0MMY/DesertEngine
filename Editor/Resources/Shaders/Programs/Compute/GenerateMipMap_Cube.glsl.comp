#pragma stage : compute
#version 450 core

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(binding = 0) uniform samplerCube u_InputTexture;
layout(binding = 1) uniform writeonly imageCube u_OutputTexture;

vec3 getSamplingVector()
{
    vec2 st = gl_GlobalInvocationID.xy/vec2(imageSize(u_OutputTexture));
    vec2 uv = 2.0 * vec2(st.x, 1.0-st.y) - vec2(1.0);

    vec3 ret;
    // Sadly 'switch' doesn't seem to work, at least on NVIDIA.
    if(gl_GlobalInvocationID.z == 0)      ret = vec3(1.0,  uv.y, -uv.x);
    else if(gl_GlobalInvocationID.z == 1) ret = vec3(-1.0, uv.y,  uv.x);
    else if(gl_GlobalInvocationID.z == 2) ret = vec3(uv.x, 1.0, -uv.y);
    else if(gl_GlobalInvocationID.z == 3) ret = vec3(uv.x, -1.0, uv.y);
    else if(gl_GlobalInvocationID.z == 4) ret = vec3(uv.x, uv.y, 1.0);
    else if(gl_GlobalInvocationID.z == 5) ret = vec3(-uv.x, uv.y, -1.0);
    return normalize(ret);
}

void main() {
    ivec2 globalCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outputSize = imageSize(u_OutputTexture).xy;

    if (globalCoord.x >= outputSize.x || globalCoord.y >= outputSize.y) {
        return;
    }

    vec3 dir = getSamplingVector();

    // Sample 4 corners with offsets
    vec2 texelSize = vec2(1/ outputSize.x, 1/ outputSize.y);
    vec3 offset = vec3(texelSize, 0.0);
    
    vec4 s00 = textureLod(u_InputTexture, dir, 0);
    vec4 s10 = textureLod(u_InputTexture, normalize(dir + offset.zyx), 0);
    vec4 s01 = textureLod(u_InputTexture, normalize(dir + offset.xzy), 0);
    vec4 s11 = textureLod(u_InputTexture, normalize(dir + offset.xyz), 0);

    vec4 color = (s00 + s10 + s01 + s11) * 0.25;
    imageStore(u_OutputTexture, ivec3(gl_GlobalInvocationID.xyz), color);
}