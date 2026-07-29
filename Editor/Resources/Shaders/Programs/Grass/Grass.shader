Shader "Grass"
{
    // GPU-driven instanced grass — triangle-list blades. Double-sided (cull none) so blades are visible from
    // both faces; depth-tested + depth-write so they sort against the terrain and each other.

    Domain Terrain

    State
    {
        Topology Triangles
        Cull None
        ZTest Less
        ZWrite On
    }

    Vertex
    {
        // GPU-driven instanced grass — REAL GEOMETRY blades (no billboard, no alpha texture, so no alpha-test
        // moiré/horizon bands). Each visible clump instance spawns kBlades multi-segment tapered blades that
        // arc forward and sway. Placement is byte-identical to GrassCull.glsl.comp (the indirect draw's
        // instanceCount comes from that compute compaction). Lit by the SCENE directional light.

        Uniform(0) GrassUB
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

        Uniform(1) sampler2D u_SplatMap; // R = grass mask (optional reducer)

        ReadBuffer(3) GrassVisible
        {
            uint u_Visible[];
        };

        Out(0) vec2  v_UV;       // x across blade [0..1], y up [0..1]
        Out(1) vec3  v_Normal;
        Out(2) float v_Rand;     // per-blade random
        Out(3) vec3  v_WorldPos;
        Out(4) float v_Fade;     // distance LOD

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
    }

    Fragment
    {
        // Geometric grass blade fragment. Solid geometry (no alpha texture, no discard) -> no alpha-test moiré.
        // Root->tip green gradient computed here, plus tonal sun/shade patches, yellow variation, AO, the SCENE
        // directional light and SSS translucency.

        In(0) vec2  v_UV;       // x across blade, y = height fraction
        In(1) vec3  v_Normal;
        In(2) float v_Rand;     // per-blade random
        In(3) vec3  v_WorldPos;
        In(4) float v_Fade;

        Out(0) vec4 o_Color;

        Uniform(0) GrassUB
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
    }
}
