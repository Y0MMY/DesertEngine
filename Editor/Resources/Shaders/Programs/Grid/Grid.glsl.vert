#version 450

// Infinite ground-plane grid. A fullscreen quad emits a world-space ray per pixel (near->far,
// reconstructed via the inverse view-projection); the fragment shader intersects it with the y=0 plane.

layout(binding = 0) uniform GridUB
{
    mat4 Projection;
    mat4 View;
    mat4 InvProjection;
    mat4 InvView;
    vec4 CameraPos; // xyz
    vec4 ThinColor;
    vec4 ThickColor;
    vec4 Params; // x = base cell size (m), y = fade start, z = fade end
} u;

layout(location = 0) out vec3 v_Near;
layout(location = 1) out vec3 v_Far;

vec3 Unproject( vec2 ndc, float z )
{
    vec4 p = u.InvView * u.InvProjection * vec4( ndc, z, 1.0 );
    return p.xyz / p.w;
}

void main()
{
    // Two triangles covering NDC. Drawn via Renderer::SubmitFullscreenQuad (vkCmdDraw(6)).
    const vec2 verts[6] = vec2[6](
        vec2( -1.0, -1.0 ), vec2( 1.0, -1.0 ), vec2( 1.0, 1.0 ),
        vec2( 1.0, 1.0 ), vec2( -1.0, 1.0 ), vec2( -1.0, -1.0 ) );

    vec2 ndc = verts[gl_VertexIndex];

    // OpenGL-style clip depth ([-1,1]) — matches the engine's perspective convention.
    v_Near = Unproject( ndc, -1.0 );
    v_Far  = Unproject( ndc, 1.0 );

    gl_Position = vec4( ndc, 0.0, 1.0 );
}
