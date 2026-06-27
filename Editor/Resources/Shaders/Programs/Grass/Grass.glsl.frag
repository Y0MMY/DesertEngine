#version 450

// Geometric grass blade fragment. Solid geometry (no alpha texture, no discard) -> no alpha-test moiré.
// Root->tip green gradient computed here, plus tonal sun/shade patches, yellow variation, AO, the SCENE
// directional light and SSS translucency.

layout( location = 0 ) in vec2  v_UV;       // x across blade, y = height fraction
layout( location = 1 ) in vec3  v_Normal;
layout( location = 2 ) in float v_Rand;     // per-blade random
layout( location = 3 ) in vec3  v_WorldPos;
layout( location = 4 ) in float v_Fade;

layout( location = 0 ) out vec4 o_Color;

layout( binding = 0 ) uniform GrassUB
{
    mat4 View;
    mat4 Projection;
    mat4 Model;
    vec4 Params;
    vec4 Params2;
    vec4 Wind;
    vec4 CameraPos;
    vec4 Interactor;
    vec4 SunDir;
    vec4 SunColor;
    vec4 GrassTint;
}
u;

void main()
{
    float t = v_UV.y; // height fraction along the blade

    // Darker, less-saturated root->tip gradient — real grass is a deep olive-green, not acid-green.
    vec3 base = mix( vec3( 0.022, 0.045, 0.015 ), vec3( 0.115, 0.215, 0.075 ), t );
    base *= u.GrassTint.rgb;

    // Tonal sun/shade patches (two octaves) -> the field isn't uniform; sunlit = a touch warmer/lighter,
    // shaded = darker. Kept subtle so it stays natural (not glowing).
    float sunMask = sin( v_WorldPos.x * 0.06 + 1.7 ) * sin( v_WorldPos.z * 0.05 - 0.6 ) * 0.5 + 0.5;
    sunMask += 0.35 * ( sin( v_WorldPos.x * 0.17 - 0.9 ) * sin( v_WorldPos.z * 0.13 + 2.1 ) * 0.5 + 0.5 );
    sunMask   = smoothstep( 0.25, 0.95, sunMask / 1.35 );
    base = mix( base * vec3( 0.80, 0.86, 0.78 ), base * vec3( 1.12, 1.08, 0.82 ), sunMask );

    // Per-blade dry/yellow variation (some blades are sun-dried, browner) — adds realism/variety.
    float dry = smoothstep( 0.5, 1.0, v_Rand );
    base = mix( base, base * vec3( 1.55, 1.20, 0.45 ), dry * 0.5 );

    // Per-blade brightness jitter (darker overall range).
    base *= ( 0.70 + 0.40 * fract( v_Rand * 7.3 ) );

    // AO toward the root for depth.
    float ao = mix( 0.45, 1.0, t );

    // Scene directional light, two-sided (blades are thin single-sided geometry).
    vec3  N   = normalize( v_Normal );
    vec3  V   = normalize( u.CameraPos.xyz - v_WorldPos );
    vec3  L   = normalize( -u.SunDir.xyz );
    vec3  sun = u.SunColor.rgb * max( u.SunColor.a, 0.0001 );

    float ndl  = max( dot( N, L ), 0.0 ) + 0.30 * max( dot( -N, L ), 0.0 );
    float wrap = ndl * 0.8 + 0.2;

    vec3 ambient = vec3( 0.17, 0.24, 0.15 );
    vec3 lit     = base * ( ambient + sun * wrap ) * ao;

    // SSS: sun behind the thin blade glows through toward the viewer (stronger at the tip).
    float trans = pow( max( dot( V, -L ), 0.0 ), 3.0 ) * ( 0.3 + 0.7 * t );
    lit += sun * trans * vec3( 0.22, 0.36, 0.10 ) * u.GrassTint.rgb;

    o_Color = vec4( lit, 1.0 );
}
