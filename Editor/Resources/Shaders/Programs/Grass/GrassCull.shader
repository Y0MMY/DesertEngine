Shader "GrassCull"
{
    Compute
    {
        // GPU grass culling: one thread per potential clump on the grid. Recomputes the SAME placement +
        // terrain height as Grass.glsl.vert, then frustum- + distance- + flatness/splat-culls. Surviving clumps
        // are appended (atomic) to u_Visible and bump the indirect draw's instanceCount, so the instanced draw
        // processes ONLY visible clumps (no per-instance vertex work for culled ones, no CPU readback).
        //
        // Placement/height functions MUST stay byte-identical to Grass.glsl.vert or blades will pop/mismatch.

        layout( local_size_x = 64, local_size_y = 1, local_size_z = 1 ) in;

        layout( binding = 0 ) uniform sampler2D u_SplatMap; // R = grass mask (only when splatPresent)

        layout( std430, binding = 1 ) buffer GrassVisible
        {
            uint u_Visible[]; // compacted list of visible clump ids
        };

        layout( std430, binding = 2 ) buffer GrassIndirect
        {
            uint u_VertexCount;   // = verts-per-clump (set by CPU each frame)
            uint u_InstanceCount; // = number of visible clumps (we atomicAdd here; CPU resets to 0)
            uint u_FirstVertex;
            uint u_FirstInstance;
        };

        layout( push_constant ) uniform Push
        {
            mat4 u_MVP;     // Proj * View * Model  (terrain-local -> clip)
            vec4 u_Params;  // x=size, y=splatPresent, z=heightScale, w=noiseFrequency
            vec4 u_Params2; // x=seed, y=grid, z=bladeHeight, w=maxDist
        };

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
            float freq = max( u_Params.w, 0.0001 );
            vec2  seed = vec2( u_Params2.x * 0.137, u_Params2.x * 0.911 );
            return ( FBm( xz * freq + seed ) - 0.5 ) * 2.0 * u_Params.z;
        }
        float Rand( float n ) { return fract( sin( n * 78.233 ) * 43758.5453 ); }

        void main()
        {
            uint clumpId = gl_GlobalInvocationID.x;
            int  grid    = max( int( u_Params2.y ), 1 );
            if ( clumpId >= uint( grid * grid ) )
                return;

            float size = u_Params.x;
            float cell = size / float( grid );

            int   gx = int( clumpId ) % grid;
            int   gz = int( clumpId ) / grid;
            // Jitter wider than one cell (x1.7) so neighbouring rows OVERLAP — breaks flat-ground row banding.
            // MUST stay byte-identical to Grass.glsl.vert or blades pop/mismatch the cull.
            float ax = ( float( gx ) + 0.5 + ( Rand( float( clumpId ) * 0.7 ) - 0.5 ) * 1.7 ) * cell - size * 0.5;
            float az = ( float( gz ) + 0.5 + ( Rand( float( clumpId ) * 1.3 ) - 0.5 ) * 1.7 ) * cell - size * 0.5;

            float anchorY = TerrainHeight( vec2( ax, az ) );

            // Slope/flatness (same central-diff slope as the vertex shader).
            float eps  = max( cell, 0.5 );
            float hXp  = TerrainHeight( vec2( ax + eps, az ) );
            float hZp  = TerrainHeight( vec2( ax, az + eps ) );
            vec3  gnrm = normalize( vec3( anchorY - hXp, eps, anchorY - hZp ) );
            float flatness = 1.0 - smoothstep( 0.30, 0.60, clamp( 1.0 - gnrm.y, 0.0, 1.0 ) );

            float mask = ( u_Params.y > 0.5 ) ? texture( u_SplatMap, vec2( ax, az ) / max( size, 0.001 ) + 0.5 ).r : 1.0;
            float density = flatness * max( mask, 0.4 );
            if ( density <= 0.25 )
                return;

            // Frustum + distance cull via a bounding sphere covering the blade (base..top) in terrain-local space.
            float bladeH  = u_Params2.z;
            vec3  center  = vec3( ax, anchorY + bladeH * 0.5, az );
            float radius  = bladeH; // generous: height + sway/width margin (avoids edge popping)

            // Gribb-Hartmann planes from the MVP (GL depth convention [-1,1]: near = row3 + row2).
            vec4 r0 = vec4( u_MVP[0][0], u_MVP[1][0], u_MVP[2][0], u_MVP[3][0] );
            vec4 r1 = vec4( u_MVP[0][1], u_MVP[1][1], u_MVP[2][1], u_MVP[3][1] );
            vec4 r2 = vec4( u_MVP[0][2], u_MVP[1][2], u_MVP[2][2], u_MVP[3][2] );
            vec4 r3 = vec4( u_MVP[0][3], u_MVP[1][3], u_MVP[2][3], u_MVP[3][3] );

            vec4 planes[6];
            planes[0] = r3 + r0; // left
            planes[1] = r3 - r0; // right
            planes[2] = r3 + r1; // bottom
            planes[3] = r3 - r1; // top
            planes[4] = r3 + r2; // near
            planes[5] = r3 - r2; // far

            for ( int i = 0; i < 6; ++i )
            {
                float len = length( planes[i].xyz );
                if ( len < 1e-6 )
                    continue;
                float dist = ( dot( planes[i].xyz, center ) + planes[i].w ) / len;
                if ( dist < -radius )
                    return; // fully outside this plane -> culled
            }

            // Distance cull: clip.w is the forward (view) distance in world units for a standard perspective;
            // it is <= the euclidean distance the vertex shader fades by, so this never culls a still-drawn clump.
            vec4 clip = u_MVP * vec4( ax, anchorY, az, 1.0 );
            if ( clip.w <= 0.0 || clip.w > u_Params2.w )
                return;

            uint slot = atomicAdd( u_InstanceCount, 1u );
            u_Visible[slot] = clumpId;
        }
    }
}
