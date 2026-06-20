#version 450

layout(location = 0) in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Seed;

layout(set = 0, binding = 0) uniform sampler2D u_StencilTexture;

void main()
{
    float mask = texture(u_StencilTexture, v_TexCoord).r;

    // The silhouette mask is rendered white (1.0) for selected meshes on top of the
    // framebuffer clear color (~0.1). A 0.5 threshold cleanly separates seed from background.
    if (mask > 0.5)
    {
        ivec2 texSize = textureSize(u_StencilTexture, 0);
        vec2 pixelCoord = v_TexCoord * vec2(texSize);
        o_Seed = vec4(pixelCoord, 0.0, 1.0);
    }
    else
    {
        o_Seed = vec4(-1.0, -1.0, 0.0, 0.0);
    }
}