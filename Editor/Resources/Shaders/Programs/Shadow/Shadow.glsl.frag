#version 450

// Light-space depth written to an R32F colour target (sampled later in PBR). Using a colour target
// instead of a sampled depth-stencil image sidesteps Vulkan depth-aspect sampling caveats.
layout(location = 0) out vec4 o_Depth;

void main()
{
    o_Depth = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
