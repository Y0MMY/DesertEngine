#version 450 core

// Bloom bright-pass: extract pixels brighter than the threshold from the HDR scene color.

layout(location = 0) in  vec2 v_TexCoord;
layout(set = 0, binding = 0) uniform sampler2D u_InputTexture;
layout(location = 0) out vec4 oColor;

layout(push_constant) uniform PushConstants
{
    float u_Threshold;
};

void main()
{
    // Emissive-driven global bloom: extract pixels brighter than the threshold (UE-style). Objects glow
    // by being emissive (PBRMaterialData EmissionColor/EmissiveIntensity), not via a per-object flag.
    vec3  c    = texture( u_InputTexture, v_TexCoord ).rgb;
    float luma = dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );

    // Soft knee: keep the colour, scaled by how far its luma is above the threshold.
    float contribution = max( 0.0, luma - u_Threshold ) / max( luma, 1e-5 );
    oColor = vec4( c * contribution, 1.0 );
}
