#version 450

// GPU-driven instanced grass — REAL GEOMETRY blades (no billboard, no alpha texture, so no alpha-test
// moiré/horizon bands). Each visible clump instance spawns kBlades multi-segment tapered blades that
// arc forward and sway. Placement is byte-identical to GrassCull.glsl.comp (the indirect draw's
// instanceCount comes from that compute compaction). Lit by the SCENE directional light.

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

layout( std430, binding = 3 ) readonly buffer GrassVisible
{
    uint u_Visible[];
};

layout( location = 0 ) out vec2  v_UV;       // x across blade [0..1], y up [0..1]
layout( location = 1 ) out vec3  v_Normal;
layout( location = 2 ) out float v_Rand;     // per-blade random
layout( location = 3 ) out vec3  v_WorldPos;
layout( location = 4 ) out float v_Fade;     // distance LOD

const int kSegments = 4; // segments per blade -> kSegments*6 = 24 verts/blade. Blade COUNT per clump is
                         // dynamic: the CPU sets the indirect vertex count = bladesPerClump*24, so bladeId
                         // = gl_VertexIndex/24 auto-ranges over however many blades were drawn.

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
    int   clumpId = int( u_Visible[gl_InstanceIndex] );
    float cell    = size / float( grid );

    int   gx = clumpId % grid;
    int   gz = clumpId / grid;
    // Jitter wider than one cell (x1.7) so rows overlap — MUST match GrassCull.glsl.comp.
    float ax = ( float( gx ) + 0.5 + ( Rand( float( clumpId ) * 0.7 ) - 0.5 ) * 1.7 ) * cell - size * 0.5;
    float az = ( float( gz ) + 0.5 + ( Rand( float( clumpId ) * 1.3 ) - 0.5 ) * 1.7 ) * cell - size * 0.5;

    // Decompose the vertex index: which blade, which segment, which quad corner.
    int vpb     = kSegments * 6;
    int bladeId = gl_VertexIndex / vpb;
    int v       = gl_VertexIndex % vpb;
    int seg     = v / 6;
    int corner  = v % 6;
    float sx    = ( corner == 1 || corner == 2 || corner == 4 ) ? 1.0 : 0.0; // right(1)/left(0)
    float ty    = ( corner == 2 || corner == 4 || corner == 5 ) ? 1.0 : 0.0; // top(1)/bottom(0)
    float side  = sx * 2.0 - 1.0;                                            // -1 / +1
    float t     = ( float( seg ) + ty ) / float( kSegments );                // height fraction 0..1

    // Per-blade randoms.
    float rb  = Rand( float( clumpId ) * 13.1 + float( bladeId ) * 7.3 );
    float rb2 = Rand( float( clumpId ) * 4.7 + float( bladeId ) * 9.1 );
    float rb3 = Rand( float( clumpId ) * 2.3 + float( bladeId ) * 5.7 );
    float rb4 = Rand( float( clumpId ) * 8.9 + float( bladeId ) * 3.3 );

    // Blade ARCHETYPE for variety/fullness: 0 = tall thin straight, 1 = wide arching, 2 = short bushy.
    int   btype      = int( rb4 * 3.0 );
    float typeHeight = ( btype == 0 ) ? 1.30 : ( btype == 2 ) ? 0.65 : 1.0;
    float typeWidth  = ( btype == 0 ) ? 0.55 : ( btype == 2 ) ? 1.05 : 0.85;
    float typeLean   = ( btype == 0 ) ? 0.45 : ( btype == 2 ) ? 1.80 : 1.0;

    // Scatter the blade base on a disk that spans the FULL cell and overlaps neighbours (rad up to ~1.1
    // cell) so blades fill the area evenly instead of clustering at the clump centre and leaving gaps —
    // this is what makes a DENSE lawn (combined with Density/Blades Per Clump).
    float ang  = rb * 6.2831853;
    float rad  = ( 0.20 + 0.90 * rb2 ) * cell;
    float bx   = ax + cos( ang ) * rad;
    float bz   = az + sin( ang ) * rad;
    float baseY = TerrainHeight( vec2( bx, bz ) );
    vec3  base  = ( u.Model * vec4( bx, baseY, bz, 1.0 ) ).xyz;

    float dist = distance( u.CameraPos.xyz, base );
    float fade = 1.0 - smoothstep( u.Params2.w * 0.25, u.Params2.w * 0.45, dist );

    // Geometry fade = shrink height (a short blade just sinks toward the ground; no horizontal sliver
    // like a collapsed billboard). Hard cap keeps the aliasing-prone far field out entirely.
    float height = u.Params2.z * ( 0.6 + 0.8 * rb3 ) * fade * typeHeight;
    // Width has a CONSTANT floor (not purely height-scaled) so SHORT grass (city/lawn) stays wide enough
    // to close gaps; the height term widens tall grass. * Grass Width * per-type * tip taper.
    float halfW  = ( 0.009 + height * 0.038 ) * clamp( u.Params.y, 0.1, 3.0 ) * typeWidth * ( 1.0 - 0.85 * t );
    // Screen-space minimum width: far blades widen so they never fall below ~1px and shimmer/blur when
    // the camera zooms out (the geometric equivalent of mip coverage — keeps distant grass sampleable).
    halfW = max( halfW, dist * 0.0007 );

    // Blade yaw -> forward & side axes.
    float yaw      = rb * 6.2831853;
    vec3  bladeDir = vec3( cos( yaw ), 0.0, sin( yaw ) );
    vec3  sideDir  = vec3( -sin( yaw ), 0.0, cos( yaw ) );

    // Forward arc (lean) + soft coherent wind, both growing toward the tip.
    float lean    = ( 0.12 + 0.45 * rb2 ) * typeLean;
    vec2  windDir = ( length( u.Wind.xy ) > 0.001 ) ? normalize( u.Wind.xy ) : vec2( 1.0, 0.0 );
    float gust    = sin( u.Wind.w * 0.7 + dot( vec2( bx, bz ), windDir ) * 0.18 ) * 0.65 +
                 sin( u.Wind.w * 0.35 ) * 0.35;
    vec3  forward = bladeDir * lean + vec3( windDir.x, 0.0, windDir.y ) * ( gust * u.Wind.z );

    vec3 pos = base;
    pos += vec3( 0.0, 1.0, 0.0 ) * ( t * height );  // up the blade
    pos += forward * ( t * t * height );            // quadratic forward arc
    pos += sideDir * ( side * halfW );              // blade width

    if ( u.Interactor.w > 0.0 )
    {
        vec2  toB = base.xz - u.Interactor.xz;
        float d2  = length( toB );
        if ( d2 < u.Interactor.w )
            pos.xz += normalize( toB + 1e-4 ) * ( 1.0 - d2 / u.Interactor.w ) * t * height;
    }

    // Rounded cross-section normal so the blade shades like a cylinder (volume), not a flat strip.
    vec3 tangent = normalize( vec3( 0.0, 1.0, 0.0 ) + forward * ( 2.0 * t ) );
    vec3 n       = normalize( cross( tangent, sideDir ) );
    n            = normalize( n + sideDir * side * 0.45 );

    v_UV       = vec2( sx, t );
    v_Normal   = n;
    v_Rand     = rb;
    v_WorldPos = pos;
    v_Fade     = fade;

    gl_Position = u.Projection * u.View * vec4( pos, 1.0 );
}
