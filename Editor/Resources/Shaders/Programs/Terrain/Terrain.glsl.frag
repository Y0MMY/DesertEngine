#version 450

// Stage 3 + 3a: textured terrain splatting with per-layer modes. Three layers (grass / rock / snow) are
// triplanar-mapped in world space. Each layer's weight comes from its mode: Auto = height/slope rules,
// Manual = the painted splat map channel (brush, Stage 3b), Off = 0. Each layer is modulated by a base
// tint so that with UNASSIGNED textures (white backend fallback) the result matches the Stage 2 relief.
// Lit with a directional key light using the analytic normal from the TES.

layout( location = 0 ) in vec3 v_WorldPos;
layout( location = 1 ) in vec3 v_Normal;
layout( location = 2 ) in float v_Height01;

layout( location = 0 ) out vec4 o_Color;

// Engine-filled terrain UB (binding 0) — used here for the per-layer modes.
layout( binding = 0 ) uniform TerrainUB
{
    mat4 View;
    mat4 Projection;
    mat4 Model;
    vec4 Params;
    vec4 Params2;
    vec4 LayerModes; // x = grass, y = rock, z = snow (0=Auto,1=Manual,2=Off), w = grassEnable
    vec4 SunDir;     // xyz = normalized light direction (scene directional light)
    vec4 SunColor;   // rgb = color, a = intensity
}
u;

// Data-driven material params (binding 1). Driven by MaterialComponent overrides.
layout( binding = 1 ) uniform MaterialUB
{
    vec4  Tint;
    float DetailTiling; // world-space size (meters) of one texture tile
}
u_Mat;

// Splat layers (texture2D #pragma params; unassigned => white fallback). Bindings follow MaterialUB.
layout( binding = 2 ) uniform sampler2D u_GrassTex;
layout( binding = 3 ) uniform sampler2D u_RockTex;
layout( binding = 4 ) uniform sampler2D u_SnowTex;
// Per-terrain splat map (R=grass, G=rock, B=snow weights), painted by the brush (Stage 3b). Engine-bound;
// unassigned => white fallback (Manual layers show everywhere until painted).
layout( binding = 5 ) uniform sampler2D u_SplatMap;

// Triplanar sample: project onto the three world planes and blend by the (squared) normal so steep faces
// don't stretch. scale = tiles per world unit.
vec3 Triplanar( sampler2D tex, vec3 wpos, vec3 n, float scale )
{
    vec3 bw = abs( n );
    bw      = pow( bw, vec3( 4.0 ) );
    bw     /= max( bw.x + bw.y + bw.z, 0.0001 );

    vec3 xa = texture( tex, wpos.yz * scale ).rgb;
    vec3 ya = texture( tex, wpos.xz * scale ).rgb;
    vec3 za = texture( tex, wpos.xy * scale ).rgb;
    return xa * bw.x + ya * bw.y + za * bw.z;
}

// Resolve a layer's weight from its mode: 0=Auto (rule), 1=Manual (splat channel), 2=Off (0).
float LayerWeight( float mode, float autoWeight, float splatChannel )
{
    if ( mode > 1.5 ) return 0.0;          // Off
    if ( mode > 0.5 ) return splatChannel; // Manual
    return autoWeight;                      // Auto
}

void main()
{
    vec3 N = normalize( v_Normal );

    // Slope = how far the normal tilts from straight up (0 = flat, 1 = vertical cliff).
    float slope = clamp( 1.0 - N.y, 0.0, 1.0 );

    // Base tints (placeholder until Stage 6 PBR textures). Under grass the ground is GREEN (a darker grass
    // tone) so it fills the gaps between blades and the field reads as a continuous lawn (the instanced
    // blades add the 3D fuzz on top). Non-grass ground is rock.
    vec3 grassCol = vec3( 0.13, 0.26, 0.09 ); // green ground under the grass blades
    vec3 rockCol  = vec3( 0.40, 0.37, 0.33 ); // bare rock (default base)
    vec3 snowCol  = vec3( 0.86, 0.88, 0.92 );

    float scale  = 1.0 / max( u_Mat.DetailTiling, 0.001 );
    vec3  grassT = Triplanar( u_GrassTex, v_WorldPos, N, scale ) * grassCol;
    vec3  rockT  = Triplanar( u_RockTex, v_WorldPos, N, scale ) * rockCol;
    vec3  snowT  = Triplanar( u_SnowTex, v_WorldPos, N, scale ) * snowCol;

    // Splat-map weights. UV is terrain-local (subtract the Model translation) so painting matches
    // regardless of where the terrain entity sits in the world.
    vec2  splatUV  = ( v_WorldPos.xz - u.Model[3].xz ) / max( u.Params.x, 0.001 ) + 0.5;
    vec4  splat    = texture( u_SplatMap, splatUV );
    float rockAuto  = smoothstep( 0.25, 0.55, slope );
    float grassAuto = 1.0 - rockAuto; // grass only on flat-ish ground
    float snowAuto  = smoothstep( 0.75, 0.95, v_Height01 ) * ( 1.0 - smoothstep( 0.4, 0.7, slope ) );

    // ROCK is the default ground. Where grass is enabled (LayerModes.w) on flat ground, the ground turns
    // GREEN — a continuous lawn base that fills the gaps between the instanced blades. Full field (not
    // splat-gated) so the user gets a lawn without painting; the splat only trims it (floor 0.4).
    float wGrass = u.LayerModes.w * grassAuto * max( splat.r, 0.4 );
    float wSnow  = LayerWeight( u.LayerModes.z, snowAuto, splat.b );

    vec3 albedo = rockT;
    albedo      = mix( albedo, grassT, clamp( wGrass, 0.0, 1.0 ) );
    albedo      = mix( albedo, snowT, clamp( wSnow, 0.0, 1.0 ) );

    // PBR-ish lighting using the SCENE directional light (dir/color/intensity). Camera position recovered
    // from the inverse view matrix for the specular view vector.
    vec3 camPos = inverse( u.View )[3].xyz;
    vec3 V      = normalize( camPos - v_WorldPos );
    vec3 L      = normalize( -u.SunDir.xyz );          // to-light (engine stores travel direction)
    vec3 sun    = u.SunColor.rgb * max( u.SunColor.a, 0.0001 );
    vec3 H      = normalize( L + V );

    float ndl = max( dot( N, L ), 0.0 );

    // Soft sky/ground ambient (hemisphere): brighter from above, cooler/darker from below.
    vec3  skyCol  = vec3( 0.45, 0.52, 0.62 );
    vec3  gndCol  = vec3( 0.20, 0.18, 0.14 );
    vec3  ambient = mix( gndCol, skyCol, N.y * 0.5 + 0.5 ) * 0.5;

    // Per-layer roughness: rock/grass matte, snow has a subtle sheen.
    float gloss = mix( 0.0, 0.5, clamp( wSnow, 0.0, 1.0 ) );
    float spec  = pow( max( dot( N, H ), 0.0 ), mix( 16.0, 90.0, gloss ) ) * gloss;

    vec3 lit = albedo * ( ambient + sun * ndl ) + sun * spec * ndl;

    o_Color = vec4( lit * u_Mat.Tint.rgb, 1.0 );
}
