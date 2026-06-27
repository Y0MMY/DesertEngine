#version 450

// GPU-driven instanced grass — CROSS-CARDS with a procedural alpha grass texture (frag). Each instance is a
// clump = kCards quads in a star (different orientations) so the tuft has real volume from any angle while
// staying cheap (kCards*6 verts). The fragment paints many fine tapered blades per card via alpha-discard.
// Full field on flat ground (no painting). Lit by the SCENE directional light (GrassUB.SunDir/SunColor).

layout( binding = 0 ) uniform GrassUB
{
    mat4 View;
    mat4 Projection;
    mat4 Model;
    vec4 Params;      // x = terrain size, y = widthScale, z = heightScale, w = noiseFrequency
    vec4 Params2;     // x = seed, y = grassGridDim, z = bladeHeight, w = maxDist
    vec4 Wind;        // xy = dir, z = strength, w = time
    vec4 CameraPos;   // xyz
    vec4 Interactor;  // xyz = pos, w = radius (0 = none)
    vec4 SunDir;      // xyz = normalized light direction (scene directional light)
    vec4 SunColor;    // rgb = color, a = intensity
    vec4 GrassTint;   // rgb = user tint multiplier
}
u;

layout( binding = 1 ) uniform sampler2D u_SplatMap; // R = grass mask (optional reducer)

layout( location = 0 ) out vec2  v_UV;       // x across the card [0..1], y up [0..1]
layout( location = 1 ) out vec3  v_Normal;
layout( location = 2 ) out float v_Rand;     // per-clump random
layout( location = 3 ) out vec3  v_WorldPos;

const int kCards = 3; // quads per clump (star) -> kCards*6 verts

float Hash( vec2 p ) { p = fract( p * vec2( 123.34, 456.21 ) ); p += dot( p, p + 45.32 ); return fract( p.x * p.y ); }
float ValueNoise( vec2 p )
{
    vec2 i = floor( p ); vec2 f = fract( p ); vec2 w = f * f * ( 3.0 - 2.0 * f );
    float a = Hash( i ); float b = Hash( i + vec2( 1, 0 ) ); float c = Hash( i + vec2( 0, 1 ) ); float d = Hash( i + vec2( 1, 1 ) );
    return mix( mix( a, b, w.x ), mix( c, d, w.x ), w.y );
}
float FBm( vec2 p ) { float s = 0.0, a = 0.5, f = 1.0; for ( int i = 0; i < 5; ++i ) { s += a * ValueNoise( p * f ); f *= 2.0; a *= 0.5; } return s; }
float TerrainHeight( vec2 xz )
{
    float freq = max( u.Params.w, 0.0001 );
    vec2  seed = vec2( u.Params2.x * 0.137, u.Params2.x * 0.911 );
    return ( FBm( xz * freq + seed ) - 0.5 ) * 2.0 * u.Params.z;
}
float Rand( float n ) { return fract( sin( n * 78.233 ) * 43758.5453 ); }

void main()
{
    int   grid    = max( int( u.Params2.y ), 1 );
    float size    = u.Params.x;
    int   clumpId = gl_InstanceIndex;
    float cell    = size / float( grid );

    int   gx = clumpId % grid;
    int   gz = clumpId / grid;
    float ax = ( float( gx ) + 0.5 + ( Rand( float( clumpId ) * 0.7 ) - 0.5 ) ) * cell - size * 0.5;
    float az = ( float( gz ) + 0.5 + ( Rand( float( clumpId ) * 1.3 ) - 0.5 ) ) * cell - size * 0.5;

    float anchorY     = TerrainHeight( vec2( ax, az ) );
    vec3  anchorWorld = ( u.Model * vec4( ax, anchorY, az, 1.0 ) ).xyz;

    float eps      = max( cell, 0.5 );
    float hXp      = TerrainHeight( vec2( ax + eps, az ) );
    float hZp      = TerrainHeight( vec2( ax, az + eps ) );
    vec3  gnrm     = normalize( vec3( anchorY - hXp, eps, anchorY - hZp ) );
    float flatness = 1.0 - smoothstep( 0.30, 0.60, clamp( 1.0 - gnrm.y, 0.0, 1.0 ) );

    float mask    = texture( u_SplatMap, vec2( ax, az ) / max( size, 0.001 ) + 0.5 ).r;
    float density = flatness * max( mask, 0.4 );
    float dist    = distance( u.CameraPos.xyz, anchorWorld );

    if ( !( density > 0.25 && dist < u.Params2.w ) )
    {
        gl_Position = vec4( 2.0, 2.0, 2.0, 1.0 );
        v_UV = vec2( 0.0 ); v_Normal = vec3( 0, 1, 0 ); v_Rand = 0.0; v_WorldPos = anchorWorld;
        return;
    }

    int  card = gl_VertexIndex / 6;
    int  vtx  = gl_VertexIndex % 6;
    vec2 quad;
    if ( vtx == 0 ) quad = vec2( 0, 0 );
    else if ( vtx == 1 ) quad = vec2( 1, 0 );
    else if ( vtx == 2 ) quad = vec2( 1, 1 );
    else if ( vtx == 3 ) quad = vec2( 0, 0 );
    else if ( vtx == 4 ) quad = vec2( 1, 1 );
    else quad = vec2( 0, 1 );

    float clumpRand   = Rand( float( clumpId ) * 8.1 );
    float bladeHeight = u.Params2.z * ( 0.75 + 0.5 * Rand( float( clumpId ) * 5.3 ) );
    float widthScale  = clamp( u.Params.y, 0.1, 5.0 );
    float cardWidth   = bladeHeight * 0.95 * ( 0.6 + widthScale );

    // Star orientation per card + per-clump random twist.
    float ang     = ( float( card ) / float( kCards ) ) * 3.14159265 + clumpRand * 6.2831853;
    vec2  cardDir = vec2( cos( ang ), sin( ang ) );

    // Build the card vertex, centered on the clump anchor, standing up. Small per-card center offset so the
    // three cards don't all share the exact center plane (avoids coplanar z-fighting / flicker).
    vec2 perp = vec2( -cardDir.y, cardDir.x );
    vec3 local = vec3( ax, anchorY, az );
    local.xz  += perp * ( ( float( card ) - 1.0 ) * cardWidth * 0.12 );
    local.xz  += cardDir * ( ( quad.x - 0.5 ) * cardWidth );
    local.y   += quad.y * bladeHeight;

    // Soft, spatially-coherent wind on the card top.
    vec2  windDir = ( length( u.Wind.xy ) > 0.001 ) ? normalize( u.Wind.xy ) : vec2( 1.0, 0.0 );
    float phase   = dot( vec2( ax, az ), windDir ) * 0.18;
    float gust    = sin( u.Wind.w * 0.7 + phase ) * 0.65 + sin( u.Wind.w * 0.35 + phase * 0.5 ) * 0.35;
    local.xz     += windDir * ( gust * u.Wind.z ) * bladeHeight * quad.y * quad.y;

    if ( u.Interactor.w > 0.0 )
    {
        vec2  toClump = anchorWorld.xz - u.Interactor.xz;
        float d2      = length( toClump );
        if ( d2 < u.Interactor.w )
            local.xz += normalize( toClump + 1e-4 ) * ( 1.0 - d2 / u.Interactor.w ) * quad.y * bladeHeight;
    }

    vec4 world  = u.Model * vec4( local, 1.0 );
    v_UV        = quad;
    v_Rand      = clumpRand;
    v_WorldPos  = world.xyz;
    // Card normal faces out (perpendicular to the card), biased up — each card faces a different way, so the
    // crossed cards give the tuft multi-directional shading (volume).
    v_Normal    = normalize( vec3( perp.x * 0.7, 1.0, perp.y * 0.7 ) );

    gl_Position = u.Projection * u.View * world;
}
