#pragma stage : compute

#version 450

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(set = 0, binding = 1, rgba32f) restrict writeonly uniform image2D outputTexture;

const float PI = 3.141592653589793;
const float TWOPI = 2.0 * PI;
const float EPSILON = 0.00001;
const uint SAMPLE_COUNT = 32 * 1024; // Reduced for performance, can be increased for better quality
const float INV_SAMPLE_COUNT = 1.0 / float(SAMPLE_COUNT);

// Hammersley sequence for quasi-random sampling
float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 sampleHammersley(uint i) {
    return vec2(i * INV_SAMPLE_COUNT, radicalInverse_VdC(i));
}

// Cosine-weighted hemisphere sampling for better importance sampling
vec3 sampleHemisphere(float u1, float u2) {
    float phi = u2 * TWOPI;
    float cosTheta = sqrt(1.0 - u1);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 GetDirection(vec2 uv, int faceIndex) {
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

// Compute orthonormal basis for tangent space
void computeBasisVectors(const vec3 N, out vec3 S, out vec3 T) {
    // Branchless select non-degenerate T
    T = cross(N, vec3(0.0, 1.0, 0.0));
    T = mix(cross(N, vec3(1.0, 0.0, 0.0)), T, step(EPSILON, dot(T, T)));
    T = normalize(T);
    S = normalize(cross(N, T));
}

// Convert from tangent space to world space
vec3 tangentToWorld(const vec3 v, const vec3 N, const vec3 S, const vec3 T) {
    return S * v.x + T * v.y + N * v.z;
}

// Sample cubemap face from 4x3 layout texture
vec3 sampleCubemapFace(vec3 direction) {
    // Determine which face to sample
    vec3 absDir = abs(direction);
    float maxAxis;
    int faceIndex;
    
    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        maxAxis = absDir.x;
        faceIndex = direction.x > 0.0 ? 0 : 1; // +X or -X
    } else if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
        maxAxis = absDir.y;
        faceIndex = direction.y > 0.0 ? 2 : 3; // +Y or -Y
    } else {
        maxAxis = absDir.z;
        faceIndex = direction.z > 0.0 ? 4 : 5; // +Z or -Z
    }
    
    // Project the direction onto the selected face and calculate UVs
    vec2 uv;
    switch (faceIndex) {
        case 0: // +X
            uv = vec2(-direction.z, -direction.y) / maxAxis;
            break;
        case 1: // -X
            uv = vec2(direction.z, -direction.y) / maxAxis;
            break;
        case 2: // +Y
            uv = vec2(direction.x, direction.z) / maxAxis;
            break;
        case 3: // -Y
            uv = vec2(direction.x, -direction.z) / maxAxis;
            break;
        case 4: // +Z
            uv = vec2(direction.x, -direction.y) / maxAxis;
            break;
        case 5: // -Z
            uv = vec2(-direction.x, -direction.y) / maxAxis;
            break;
    }
    
    // Transform UVs to [0,1] range
    uv = uv * 0.5 + 0.5;
    
    // Calculate the face position in the 4x3 layout
    ivec2 faceSize = textureSize(inputTexture, 0) / ivec2(4, 3);
    ivec2 faceOffset;
    
    // [ ][+Y][ ][ ]
    // [-X][+Z][+X][-Z]
    // [ ][-Y][ ][ ]
    switch (faceIndex) {
        case 0: // +X (right)
            faceOffset = ivec2(2, 1);
            break;
        case 1: // -X (left)
            faceOffset = ivec2(0, 1);
            break;
        case 2: // +Y (top)
            faceOffset = ivec2(1, 0);
            break;
        case 3: // -Y (bottom)
            faceOffset = ivec2(1, 2);
            break;
        case 4: // +Z (front)
            faceOffset = ivec2(1, 1);
            break;
        case 5: // -Z (back)
            faceOffset = ivec2(3, 1);
            break;
    }
    
    // Calculate final texture coordinates
    vec2 texCoord = (vec2(faceOffset) * vec2(faceSize) + uv * vec2(faceSize));
    texCoord /= vec2(textureSize(inputTexture, 0));
    
    return texture(inputTexture, texCoord).rgb;
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
    vec3 N = GetDirection(uv, faceIndex);
    
    // Compute tangent space basis vectors
    vec3 S, T;
    computeBasisVectors(N, S, T);
    
    // Monte Carlo integration for diffuse irradiance
    vec3 irradiance = vec3(0.0);
    for(uint i = 0; i < SAMPLE_COUNT; ++i) {
        vec2 u = sampleHammersley(i);
        vec3 Li = tangentToWorld(sampleHemisphere(u.x, u.y), N, S, T);
        float cosTheta = max(0.0, dot(Li, N));
        
        // Sample the cubemap texture
        irradiance += sampleCubemapFace(Li) * cosTheta;
    }
    
    // Scale by 2π as we're integrating over a hemisphere
    irradiance = irradiance * 2 / float(SAMPLE_COUNT);
    
    imageStore(outputTexture, globalCoord, vec4(irradiance, 1.0));
}