#version 450

// Tessellation evaluation — bilinearly interpolates the tessellated grid vertex across the patch, then
// displaces it along Y by a procedural fBm height field (Stage 2). Normals are derived analytically from
// the same height function via central differences, so the lit relief needs no normal map.
//
// The height source is isolated in TerrainHeight(): Stage 5 (sculpting) swaps the fBm for a sampled,
// editable heightmap texture without touching the displacement/normal math below.

layout( quads, equal_spacing, cw ) in;

layout( binding = 0 ) uniform TerrainUB
{
    mat4 View;
    mat4 Projection;
    mat4 Model;
    vec4 Params;     // x = size, y = gridDim, z = heightScale, w = tessLevel
    vec4 Params2;    // x = noiseFrequency, y = seed, z/w = spare
    vec4 LayerModes; // x = grass, y = rock, z = snow (0=Auto,1=Manual,2=Off), w = grassEnable
    vec4 SunDir;
    vec4 SunColor;
}
u;

layout( location = 0 ) in vec2 tc_WorldXZ[];

layout( location = 0 ) out vec3 v_WorldPos;
layout( location = 1 ) out vec3 v_Normal;
layout( location = 2 ) out float v_Height01; // normalized height [0..1] for slope/height tinting

// --- Value-noise fBm -------------------------------------------------------------------------------

float Hash( vec2 p )
{
    p = fract( p * vec2( 123.34, 456.21 ) );
    p += dot( p, p + 45.32 );
    return fract( p.x * p.y );
}

float ValueNoise( vec2 p )
{
    vec2 i = floor( p );
    vec2 f = fract( p );
    vec2 u = f * f * ( 3.0 - 2.0 * f ); // smoothstep

    float a = Hash( i + vec2( 0.0, 0.0 ) );
    float b = Hash( i + vec2( 1.0, 0.0 ) );
    float c = Hash( i + vec2( 0.0, 1.0 ) );
    float d = Hash( i + vec2( 1.0, 1.0 ) );

    return mix( mix( a, b, u.x ), mix( c, d, u.x ), u.y );
}

float FBm( vec2 p )
{
    float sum = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for ( int i = 0; i < 5; ++i )
    {
        sum += amp * ValueNoise( p * freq );
        freq *= 2.0;
        amp *= 0.5;
    }
    return sum; // ~[0..1]
}

// Returns terrain height (world units) at a world-space XZ position.
float TerrainHeight( vec2 worldXZ )
{
    float freq = max( u.Params2.x, 0.0001 );
    vec2  seed = vec2( u.Params2.y * 0.137, u.Params2.y * 0.911 );
    float h    = FBm( worldXZ * freq + seed );
    return ( h - 0.5 ) * 2.0 * u.Params.z; // center around 0, scale by heightScale
}

void main()
{
    // Corners stored CCW: p0=(0,0) p1=(1,0) p2=(1,1) p3=(0,1). Bilerp over gl_TessCoord.
    vec4 p0 = gl_in[0].gl_Position;
    vec4 p1 = gl_in[1].gl_Position;
    vec4 p2 = gl_in[2].gl_Position;
    vec4 p3 = gl_in[3].gl_Position;

    vec4 a   = mix( p0, p1, gl_TessCoord.x );
    vec4 b   = mix( p3, p2, gl_TessCoord.x );
    vec4 pos = mix( a, b, gl_TessCoord.y );

    // Displace along Y by the height field (Stage 2).
    pos.y = TerrainHeight( pos.xz );

    // Analytic normal via central differences on the height field. Epsilon scales with the grid cell.
    float eps = max( u.Params.x / max( u.Params.y, 1.0 ), 0.01 ) * 0.5;
    float hL  = TerrainHeight( pos.xz - vec2( eps, 0.0 ) );
    float hR  = TerrainHeight( pos.xz + vec2( eps, 0.0 ) );
    float hD  = TerrainHeight( pos.xz - vec2( 0.0, eps ) );
    float hU  = TerrainHeight( pos.xz + vec2( 0.0, eps ) );
    vec3  n   = normalize( vec3( hL - hR, 2.0 * eps, hD - hU ) );

    vec4 worldPos = u.Model * vec4( pos.xyz, 1.0 ); // apply the terrain entity's transform
    v_WorldPos    = worldPos.xyz;
    v_Normal      = normalize( mat3( u.Model ) * n );
    v_Height01    = clamp( pos.y / max( u.Params.z, 0.0001 ) * 0.5 + 0.5, 0.0, 1.0 );

    gl_Position = u.Projection * u.View * worldPos;
}
