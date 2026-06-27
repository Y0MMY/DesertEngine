#version 450

// Tessellation control — one 4-vertex quad patch in, distance-based LOD out (Stage 4). Each edge's
// tessellation level is derived from its midpoint distance to the camera in VIEW space (the camera sits
// at the origin in view space, so no camera-position uniform is needed). Adjacent patches share an edge's
// two corner positions, so they compute the same midpoint -> the same edge tess level -> CRACK-FREE.
// Near patches get full detail (Params.w), far patches drop toward minTess.

layout( vertices = 4 ) out;

layout( binding = 0 ) uniform TerrainUB
{
    mat4 View;
    mat4 Projection;
    mat4 Model;
    vec4 Params;     // x = size, y = gridDim, z = heightScale, w = tessLevel (used here as MAX/near tess)
    vec4 Params2;    // x = noiseFrequency, y = seed, z/w = spare
    vec4 LayerModes; // x = grass, y = rock, z = snow (0=Auto,1=Manual,2=Off), w = grassEnable
    vec4 SunDir;
    vec4 SunColor;
}
u;

layout( location = 0 ) in vec2 v_WorldXZ[];
layout( location = 0 ) out vec2 tc_WorldXZ[];

float TessForDistance( float d )
{
    float maxTess = max( u.Params.w, 1.0 );
    float minTess = 2.0;
    // Distance band scales with terrain size so LOD adapts to small and large terrains alike.
    float nearD = max( u.Params.x * 0.05, 2.0 );
    float farD  = max( u.Params.x * 1.5, nearD + 1.0 );
    float t     = clamp( ( farD - d ) / ( farD - nearD ), 0.0, 1.0 );
    return mix( minTess, maxTess, t );
}

// Tessellation level for the edge between two control points, from its midpoint's view-space distance.
float EdgeTess( vec3 viewA, vec3 viewB )
{
    return TessForDistance( length( ( viewA + viewB ) * 0.5 ) );
}

void main()
{
    if ( gl_InvocationID == 0 )
    {
        // Control-point corners in view space (camera at origin). gl_in are the flat y=0 control points;
        // displacement is small vs. the LOD distances, so using the flat positions is fine and stable.
        mat4 mv = u.View * u.Model;
        vec3 c0 = ( mv * gl_in[0].gl_Position ).xyz; // (u,v)=(0,0)
        vec3 c1 = ( mv * gl_in[1].gl_Position ).xyz; // (1,0)
        vec3 c2 = ( mv * gl_in[2].gl_Position ).xyz; // (1,1)
        vec3 c3 = ( mv * gl_in[3].gl_Position ).xyz; // (0,1)

        // Quad edge -> outer-tess mapping: [0]=u0 (c0-c3), [1]=v0 (c0-c1), [2]=u1 (c1-c2), [3]=v1 (c3-c2).
        float e0 = EdgeTess( c0, c3 );
        float e1 = EdgeTess( c0, c1 );
        float e2 = EdgeTess( c1, c2 );
        float e3 = EdgeTess( c3, c2 );

        gl_TessLevelOuter[0] = e0;
        gl_TessLevelOuter[1] = e1;
        gl_TessLevelOuter[2] = e2;
        gl_TessLevelOuter[3] = e3;

        // Inner levels: horizontal (u) from the v=0/v=1 edges, vertical (v) from the u=0/u=1 edges.
        gl_TessLevelInner[0] = max( e1, e3 );
        gl_TessLevelInner[1] = max( e0, e2 );
    }

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tc_WorldXZ[gl_InvocationID]         = v_WorldXZ[gl_InvocationID];
}
