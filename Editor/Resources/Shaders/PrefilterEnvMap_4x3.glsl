#pragma stage : compute
#version 450

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(set = 0, binding = 1, rgba32f) restrict writeonly uniform image2D outputTexture;

layout(push_constant) uniform PushConstants {
    float roughness;
    uint mipLevel;
} pc;

const float PI = 3.141592653589793;
const float TwoPI = 2.0 * PI;
const float Epsilon = 0.00001;

const uint NumSamples = 1024;
const float InvNumSamples = 1.0 / float(NumSamples);

// Compute Van der Corput radical inverse
float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

// Sample i-th point from Hammersley point set
vec2 sampleHammersley(uint i) {
    return vec2(i * InvNumSamples, radicalInverse_VdC(i));
}

// Importance sample GGX normal distribution function
vec3 sampleGGX(float u1, float u2, float roughness) {
    float alpha = roughness * roughness;
    float cosTheta = sqrt((1.0 - u2) / (1.0 + (alpha*alpha - 1.0) * u2));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
    float phi = TwoPI * u1;
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

// GGX normal distribution function
float ndfGGX(float cosLh, float roughness) {
    float alpha = roughness * roughness;
    float alphaSq = alpha * alpha;
    float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
    return alphaSq / (PI * denom * denom);
}

// Get direction vector for 4x3 cubemap layout
vec3 GetDirection(vec2 uv, int faceIndex) {
    vec2 st = 2.0 * uv - 1.0;
    switch(faceIndex) {
        case 0: return normalize(vec3(1.0, -st.y, -st.x));  // +X (right)
        case 1: return normalize(vec3(-1.0, -st.y, st.x));   // -X (left)
        case 2: return normalize(vec3(st.x, 1.0, st.y));     // +Y (top)
        case 3: return normalize(vec3(st.x, -1.0, -st.y));   // -Y (bottom)
        case 4: return normalize(vec3(st.x, -st.y, 1.0));    // +Z (front)
        case 5: return normalize(vec3(-st.x, -st.y, -1.0));  // -Z (back)
        default: return vec3(0.0);
    }
}

// Compute orthonormal basis for tangent space
void computeBasisVectors(const vec3 N, out vec3 S, out vec3 T) {
    T = cross(N, vec3(0.0, 1.0, 0.0));
    T = mix(cross(N, vec3(1.0, 0.0, 0.0)), T, step(Epsilon, dot(T, T)));
    T = normalize(T);
    S = normalize(cross(N, T));
}

// Convert from tangent space to world space
vec3 tangentToWorld(const vec3 v, const vec3 N, const vec3 S, const vec3 T) {
    return S * v.x + T * v.y + N * v.z;
}

// Sample cubemap face from 4x3 layout texture
vec3 sampleCubemapFace(vec3 direction, float mipLevel) {
    vec3 absDir = abs(direction);
    float maxAxis;
    int faceIndex;
    
    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        maxAxis = absDir.x;
        faceIndex = direction.x > 0.0 ? 0 : 1;
    } else if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
        maxAxis = absDir.y;
        faceIndex = direction.y > 0.0 ? 2 : 3;
    } else {
        maxAxis = absDir.z;
        faceIndex = direction.z > 0.0 ? 4 : 5;
    }
    
    vec2 uv;
    switch (faceIndex) {
        case 0: uv = vec2(-direction.z, -direction.y) / maxAxis; break;
        case 1: uv = vec2(direction.z, -direction.y) / maxAxis; break;
        case 2: uv = vec2(direction.x, direction.z) / maxAxis; break;
        case 3: uv = vec2(direction.x, -direction.z) / maxAxis; break;
        case 4: uv = vec2(direction.x, -direction.y) / maxAxis; break;
        case 5: uv = vec2(-direction.x, -direction.y) / maxAxis; break;
    }
    
    uv = uv * 0.5 + 0.5;
    
    ivec2 faceSize = textureSize(inputTexture, 0) / ivec2(4, 3);
    ivec2 faceOffset;
    
    // 4x3 layout:
    // [ ][+Y][ ][ ]
    // [-X][+Z][+X][-Z]
    // [ ][-Y][ ][ ]
    switch (faceIndex) {
        case 0: faceOffset = ivec2(2, 1); break;  // +X
        case 1: faceOffset = ivec2(0, 1); break;  // -X
        case 2: faceOffset = ivec2(1, 0); break;  // +Y
        case 3: faceOffset = ivec2(1, 2); break;  // -Y
        case 4: faceOffset = ivec2(1, 1); break;  // +Z
        case 5: faceOffset = ivec2(3, 1); break;  // -Z
    }
    
    vec2 texCoord = (vec2(faceOffset) * vec2(faceSize) + uv * vec2(faceSize));
    texCoord /= vec2(textureSize(inputTexture, 0));
    
    return textureLod(inputTexture, texCoord, mipLevel).rgb;
}

void main() {
    ivec2 outputSize = imageSize(outputTexture);
    ivec2 globalCoord = ivec2(gl_GlobalInvocationID.xy);
    
    if (globalCoord.x >= outputSize.x || globalCoord.y >= outputSize.y)
        return;

    ivec2 faceSize = outputSize / ivec2(4, 3);
    ivec2 faceCoord = globalCoord / faceSize;
    
    // Determine face index based on 4x3 layout
    int faceIndex = -1;
    if (faceCoord.y == 0 && faceCoord.x == 1) faceIndex = 2;      // +Y
    else if (faceCoord.y == 1 && faceCoord.x == 0) faceIndex = 1; // -X
    else if (faceCoord.y == 1 && faceCoord.x == 1) faceIndex = 4; // +Z
    else if (faceCoord.y == 1 && faceCoord.x == 2) faceIndex = 0; // +X
    else if (faceCoord.y == 1 && faceCoord.x == 3) faceIndex = 5; // -Z
    else if (faceCoord.y == 2 && faceCoord.x == 1) faceIndex = 3; // -Y

    if (faceIndex == -1) {
        imageStore(outputTexture, globalCoord, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }

    vec2 uv = (vec2(globalCoord) - vec2(faceSize) * vec2(faceCoord)) / vec2(faceSize);
    vec3 N = GetDirection(uv, faceIndex);
    vec3 Lo = N; // View vector equals normal (approximation)
    
    vec3 S, T;
    computeBasisVectors(N, S, T);

    // Solid angle associated with a single cubemap texel at zero mipmap level
    ivec2 inputSize = textureSize(inputTexture, 0);
    float wt = 4.0 * PI / (6 * inputSize.x * inputSize.y);
    
    vec3 color = vec3(0.0);
    float weight = 0.0;

    // Convolve environment map using GGX NDF importance sampling
    for(uint i = 0; i < NumSamples; ++i) {
        vec2 u = sampleHammersley(i);
        vec3 Lh = tangentToWorld(sampleGGX(u.x, u.y, pc.roughness), N, S, T);
        vec3 Li = 2.0 * dot(Lo, Lh) * Lh - Lo;

        float cosLi = dot(N, Li);
        if(cosLi > 0.0) {
            float cosLh = max(dot(N, Lh), 0.0);
            float pdf = ndfGGX(cosLh, pc.roughness) * 0.25;
            float ws = 1.0 / (NumSamples * pdf);
            float mipLevel = max(0.5 * log2(ws / wt) + 1.0, 0.0);

            color += sampleCubemapFace(Li, mipLevel) * cosLi;
            weight += cosLi;
        }
    }
    color /= weight;

    imageStore(outputTexture, globalCoord, vec4(color, 1.0));
}