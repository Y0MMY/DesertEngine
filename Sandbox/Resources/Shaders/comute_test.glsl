#pragma stage : compute

#version 450

layout (local_size_x = 256) in;

layout(set = 0, binding = 0) buffer BufferA {
    float A[];
};

layout(set = 0, binding = 1) buffer BufferB {
    float B[];
};

layout(set = 0, binding = 2) buffer BufferC {
    float C[];
};

void main() {
    uint index = gl_GlobalInvocationID.x;

    C[index] = A[index] + B[index];
}