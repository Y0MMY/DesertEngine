#version 450

// Grass card fragment. Samples the BAKED grass-clump texture ONCE (alpha + baked root->tip olive color +
// per-blade variation) instead of looping over blades per pixel — keeps FPS high regardless of blade
// detail. Then applies the user Grass Tint, fake AO, world-patch variation, SSS translucency and the
// SCENE directional light.

layout( location = 0 ) in vec2  v_UV;
layout( location = 1 ) in vec3  v_Normal;
layout( location = 2 ) in float v_Rand;
layout( location = 3 ) in vec3  v_WorldPos;

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
    vec4 GrassTint; // rgb = user tint multiplier
}
u;

layout( binding = 2 ) uniform sampler2D u_GrassClump; // baked clump: rgb = color, a = coverage

void main()
{
    vec4 clump = texture( u_GrassClump, v_UV );
    if ( clump.a < 0.35 )
        discard;

    float t    = v_UV.y;            // height fraction along the card
    vec3  base = clump.rgb;         // baked olive gradient + per-blade variation
    base      *= u.GrassTint.rgb;   // user tint (set the hue/shade)

    // Large-scale world patches (sunlit lighter / shaded richer) so the field isn't uniform.
    float patchMix = sin( v_WorldPos.x * 0.13 ) * sin( v_WorldPos.z * 0.11 ) * 0.5 + 0.5;
    base = mix( base * 0.82, base * 1.14, patchMix );
    base *= ( 0.85 + 0.3 * v_Rand ); // per-clump brightness variation

    // Fake AO toward the root for depth.
    float ao = mix( 0.45, 1.0, t );

    // Lighting from the SCENE directional light.
    vec3  N   = normalize( v_Normal );
    vec3  V   = normalize( u.CameraPos.xyz - v_WorldPos );
    vec3  L   = normalize( -u.SunDir.xyz );
    vec3  sun = u.SunColor.rgb * max( u.SunColor.a, 0.0001 );

    float ndl  = max( dot( N, L ), 0.0 ) + 0.30 * max( dot( -N, L ), 0.0 ); // two-sided
    float wrap = ndl * 0.8 + 0.2;

    vec3 ambient = vec3( 0.38, 0.43, 0.38 );
    vec3 lit     = base * ( ambient + sun * wrap ) * ao;

    // Translucency / SSS: sun behind the thin blade glows through toward the viewer (stronger at the tip).
    float trans = pow( max( dot( V, -L ), 0.0 ), 3.0 ) * ( 0.3 + 0.7 * t );
    lit += sun * trans * vec3( 0.22, 0.36, 0.10 ) * u.GrassTint.rgb;

    o_Color = vec4( lit, 1.0 );
}
