#pragma stage : compute

#version 450 core

const float PI = 3.141592;
const float TWOPI = 2 * PI;

layout(set=0, binding=0) uniform sampler2D inputTexture;
layout(set=0, binding=1, rgba32f) restrict writeonly uniform imageCube outputTexture;

vec3 getSamplingVector()
{
    vec2 st = gl_GlobalInvocationID.xy/vec2(imageSize(outputTexture));
    vec2 uv = 2.0 * vec2(st.x, 1.0-st.y) - vec2(1.0);

    vec3 ret;
	// Select vector based on cubemap face index.
    // Sadly 'switch' doesn't seem to work, at least on NVIDIA.
    if(gl_GlobalInvocationID.z == 0)      ret = vec3(1.0,  uv.y, -uv.x);
    else if(gl_GlobalInvocationID.z == 1) ret = vec3(-1.0, uv.y,  uv.x);
    else if(gl_GlobalInvocationID.z == 2) ret = vec3(uv.x, 1.0, -uv.y);
    else if(gl_GlobalInvocationID.z == 3) ret = vec3(uv.x, -1.0, uv.y);
    else if(gl_GlobalInvocationID.z == 4) ret = vec3(uv.x, uv.y, 1.0);
    else if(gl_GlobalInvocationID.z == 5) ret = vec3(-uv.x, uv.y, -1.0);
    return normalize(ret);
}

layout(local_size_x=32, local_size_y=32, local_size_z=1) in;
void main(void)
{
	vec3 direction = getSamplingVector();

    float phi = atan(direction.z, direction.x);
    float theta = acos(direction.y);

    vec2 sampleUV = vec2(phi / (2.0 * PI) + 0.5, theta / PI);
    vec4 color = texture(inputTexture, sampleUV);

	imageStore(outputTexture, ivec3(gl_GlobalInvocationID), color);
}