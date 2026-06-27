#version 450

// GPU terrain — vertexless patch grid. A draw of (gridDim*gridDim*4) vertices synthesizes a grid of
// quad patches purely from gl_VertexIndex (no vertex buffer). This stage emits each patch corner as a
// world-space control point on the y=0 plane; the TES projects + (later) displaces it.

layout( binding = 0 ) uniform TerrainUB
{
    mat4 View;
    mat4 Projection;
    mat4 Model;
    vec4 Params;     // x = world size (m), y = gridDim (patches/side), z = heightScale, w = tessLevel
    vec4 Params2;    // x = noiseFrequency, y = seed, z/w = spare
    vec4 LayerModes; // x = grass, y = rock, z = snow (0=Auto,1=Manual,2=Off), w = grassEnable
    vec4 SunDir;     // xyz = normalized light direction (scene directional light)
    vec4 SunColor;   // rgb = color, a = intensity
}
u;

layout( location = 0 ) out vec2 v_WorldXZ;

void main()
{
    int gridDim = int( u.Params.y );
    int patchId = gl_VertexIndex / 4;
    int corner  = gl_VertexIndex % 4;

    int gx = patchId % gridDim;
    int gz = patchId / gridDim;

    // Unit-quad corner offsets in CCW order: (0,0) (1,0) (1,1) (0,1).
    vec2 off = vec2( ( corner == 1 || corner == 2 ) ? 1.0 : 0.0, ( corner == 2 || corner == 3 ) ? 1.0 : 0.0 );

    float size = u.Params.x;
    float cell = size / float( gridDim );
    float x    = ( float( gx ) + off.x ) * cell - size * 0.5;
    float z    = ( float( gz ) + off.y ) * cell - size * 0.5;

    v_WorldXZ   = vec2( x, z );
    gl_Position = vec4( x, 0.0, z, 1.0 ); // world-space control point (projection happens in the TES)
}
