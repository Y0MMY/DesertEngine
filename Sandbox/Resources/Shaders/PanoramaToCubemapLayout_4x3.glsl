#pragma stage : compute

#version 450

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(set = 0, binding = 1, rgba32f) restrict writeonly uniform image2D outputTexture;

const float PI = 3.141592653589793;
const float TWOPI = 2.0 * PI;

vec3 GetDirection(vec2 uv, int faceIndex)
{
    vec2 st = 2.0 * uv - 1.0;
    switch(faceIndex) {
        case 0: return normalize(vec3( 1.0, -st.y, -st.x)); // +X (right)
        case 1: return normalize(vec3(-1.0, -st.y,  st.x)); // -X (left)
        case 2: return normalize(vec3( st.x,  1.0,  st.y)); // +Y (top)
        case 3: return normalize(vec3( st.x, -1.0, -st.y)); // -Y (bottom)
        case 4: return normalize(vec3( st.x, -st.y,  1.0)); // +Z (front)
        case 5: return normalize(vec3(-st.x, -st.y, -1.0)); // -Z (back)
        default: return vec3(0.0);
    }
}

void main() {
    ivec2 outputSize = imageSize(outputTexture);
    ivec2 globalCoord = ivec2(gl_GlobalInvocationID.xy);
    
    ivec2 faceSize = outputSize / ivec2(4, 3);

    if (globalCoord.x >= outputSize.x || globalCoord.y >= outputSize.y)
        return;

    ivec2 faceCoord = globalCoord / faceSize;
    
    // [ ][+Y][ ][ ]
    // [-X][+Z][+X][-Z]
    // [ ][-Y][ ][ ]
    
    int faceIndex = -1;
    
    if (faceCoord.y == 0 && faceCoord.x == 1) faceIndex = 2; // +Y (top)
    else if (faceCoord.y == 1 && faceCoord.x == 0) faceIndex = 1; // -X (left)
    else if (faceCoord.y == 1 && faceCoord.x == 1) faceIndex = 4; // +Z (front)
    else if (faceCoord.y == 1 && faceCoord.x == 2) faceIndex = 0; // +X (right)
    else if (faceCoord.y == 1 && faceCoord.x == 3) faceIndex = 5; // -Z (back)
    else if (faceCoord.y == 2 && faceCoord.x == 1) faceIndex = 3; // -Y (bottom)

    if (faceIndex == -1) {
        imageStore(outputTexture, globalCoord, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }

    vec2 uv = (vec2(globalCoord) - vec2(faceSize) * vec2(faceCoord)) / vec2(faceSize);

    vec3 direction = GetDirection(uv, faceIndex);

    float phi = atan(direction.z, direction.x);
    float theta = acos(direction.y);

    vec2 sampleUV = vec2(phi / TWOPI + 0.5, theta / PI);
    vec4 color = texture(inputTexture, sampleUV);

    imageStore(outputTexture, globalCoord, color);
}