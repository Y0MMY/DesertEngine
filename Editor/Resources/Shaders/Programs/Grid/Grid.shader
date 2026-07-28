Shader "Grid"
{
    Fragment
    {
        // Infinite grid (Marco Giordano / "3D Graphics Rendering Cookbook" style): ray-march the per-pixel
        // world ray onto y=0, draw an analytic, derivative-based multi-scale grid (distance LOD), highlight the
        // X/Z axes, fade to the horizon, and write gl_FragDepth so opaque geometry occludes it.

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

        layout(location = 0) in vec3 v_Near;
        layout(location = 1) in vec3 v_Far;
        layout(location = 0) out vec4 o_Color;

        // Named gridLog10, not log10: MoltenVK translates GLSL to MSL, where log10 is a
        // Metal built-in and a user function with that name makes the call ambiguous.
        float gridLog10( float x ) { return log( x ) / log( 10.0 ); }
        float satf( float x ) { return clamp( x, 0.0, 1.0 ); }
        vec2  satv( vec2 v ) { return clamp( v, vec2( 0.0 ), vec2( 1.0 ) ); }
        float max2( vec2 v ) { return max( v.x, v.y ); }

        vec4 Grid( vec2 P, float baseCell )
        {
            vec2  dudv = vec2( length( vec2( dFdx( P.x ), dFdy( P.x ) ) ), length( vec2( dFdx( P.y ), dFdy( P.y ) ) ) );
            float lod      = max( 0.0, gridLog10( ( length( dudv ) * 2.0 ) / baseCell ) + 1.0 );
            float lodFade  = fract( lod );
            float l0       = baseCell * pow( 10.0, floor( lod ) );
            float l1       = l0 * 10.0;
            float l2       = l1 * 10.0;

            dudv *= 4.0;
            P += dudv * 0.5;

            float a0 = max2( vec2( 1.0 ) - abs( satv( mod( P, l0 ) / dudv ) * 2.0 - vec2( 1.0 ) ) );
            float a1 = max2( vec2( 1.0 ) - abs( satv( mod( P, l1 ) / dudv ) * 2.0 - vec2( 1.0 ) ) );
            float a2 = max2( vec2( 1.0 ) - abs( satv( mod( P, l2 ) / dudv ) * 2.0 - vec2( 1.0 ) ) );

            vec4 c = a2 > 0.0 ? u.ThickColor : a1 > 0.0 ? mix( u.ThickColor, u.ThinColor, lodFade ) : u.ThinColor;
            c.a *= ( a2 > 0.0 ? a2 : a1 > 0.0 ? a1 : a0 * ( 1.0 - lodFade ) );
            return c;
        }

        float DepthOf( vec3 worldPos )
        {
            vec4 clip = u.Projection * u.View * vec4( worldPos, 1.0 );
            return ( clip.z / clip.w ) * 0.5 + 0.5; // GL clip [-1,1] -> Vulkan depth [0,1]
        }

        void main()
        {
            // Intersect the world ray with the y=0 plane.
            float t = -v_Near.y / ( v_Far.y - v_Near.y );
            if ( t <= 0.0 )
                discard; // plane is behind the camera / ray parallel

            vec3 P = v_Near + t * ( v_Far - v_Near );

            vec4 col = Grid( P.xz, u.Params.x );

            // Axis highlight (X = red, Z = blue) ~one cell wide.
            vec2 axisW = vec2( length( vec2( dFdx( P.x ), dFdy( P.x ) ) ), length( vec2( dFdx( P.z ), dFdy( P.z ) ) ) ) * 2.0;
            if ( abs( P.z ) < axisW.y )
                col.rgb = mix( col.rgb, vec3( 0.90, 0.25, 0.25 ), 0.85 );
            if ( abs( P.x ) < axisW.x )
                col.rgb = mix( col.rgb, vec3( 0.25, 0.45, 0.95 ), 0.85 );

            // Fade with distance from the camera (soft horizon).
            float dist = length( P.xz - u.CameraPos.xz );
            col.a *= 1.0 - satf( ( dist - u.Params.y ) / max( u.Params.z - u.Params.y, 1e-3 ) );
            if ( col.a <= 0.001 )
                discard;

            gl_FragDepth = DepthOf( P );
            o_Color      = col;
        }
    }

    Vertex
    {
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
    }
}
