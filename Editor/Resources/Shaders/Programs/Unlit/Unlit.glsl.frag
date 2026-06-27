#version 450

layout( location = 0 ) in vec2 v_UV;
layout( location = 0 ) out vec4 o_Color;

// Per-material parameter block. `Color` is a real UB field, so DataDrivenMaterial sets it by name from
// the #pragma param schema (binding 1 — binding 0 is the shared CameraUB).
layout( binding = 1 ) uniform MaterialUB
{
    vec4 Color;
}
u_Material;

// Texture param (binding 2). Unset -> backend fallback (white). Set via MaterialComponent texture override.
layout( binding = 2 ) uniform sampler2D u_AlbedoTex;

void main()
{
    o_Color = texture( u_AlbedoTex, v_UV ) * u_Material.Color;
}
