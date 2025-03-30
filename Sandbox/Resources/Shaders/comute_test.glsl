#pragma stage : compute

#version 450

layout (local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout (binding = 1, rgba8) uniform writeonly image2D outputImage;

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    float value = float(gl_GlobalInvocationID.x % 2);
    imageStore(outputImage, pixelCoord, vec4(1.0, 1.0, 1.0, 1.0));
}