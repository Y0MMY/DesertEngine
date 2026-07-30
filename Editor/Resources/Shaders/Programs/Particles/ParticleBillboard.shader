Shader "ParticleBillboard"
{
    Vertex
    {
        // Vertexless billboard draw: 6 vertices per particle, corners generated from gl_VertexIndex. Each
        // particle is read from the simulation's storage buffer by index and expanded into a camera-facing
        // quad in view space. Size + colour + alpha interpolate over the particle's life. Drawn via
        // Renderer::SubmitVertices( pipeline, particleCount * 6, ... ) — no vertex/index buffer.

        #include <Common/CameraUB.glslh>

        struct Particle
        {
            vec4 PosAge;      // xyz = world position, w = age
            vec4 VelLifetime; // xyz = velocity, w = lifetime (<= 0 => dead)
        };

        ReadBuffer(1) Particles
        {
            Particle u_Particles[];
        };

        PushConstant PushConstants
        {
            vec4 u_StartColor; // rgb + start alpha (w)
            vec4 u_EndColor;   // rgb + end alpha (w)
            vec4 u_Sizes;      // x = start size, y = end size
        }
        m_PushConstants;

        Out(0) vec2 v_UV;
        Out(1) vec4 v_Color;

        void main()
        {
            uint particleID = uint( gl_VertexIndex ) / 6u;
            uint corner     = uint( gl_VertexIndex ) % 6u;

            Particle p = u_Particles[particleID];

            // Dead particle: emit an off-screen (clipped) vertex so the quad is discarded.
            if ( p.VelLifetime.w <= 0.0 )
            {
                gl_Position = vec4( 2.0, 2.0, 2.0, 1.0 );
                v_UV        = vec2( 0.0 );
                v_Color     = vec4( 0.0 );
                return;
            }

            float t     = clamp( p.PosAge.w / max( p.VelLifetime.w, 1e-4 ), 0.0, 1.0 );
            float size  = mix( m_PushConstants.u_Sizes.x, m_PushConstants.u_Sizes.y, t );
            vec3  rgb   = mix( m_PushConstants.u_StartColor.rgb, m_PushConstants.u_EndColor.rgb, t );
            float alpha = mix( m_PushConstants.u_StartColor.a, m_PushConstants.u_EndColor.a, t );

            const vec2 corners[6] = vec2[6]( vec2( -1.0, -1.0 ), vec2( 1.0, -1.0 ), vec2( -1.0, 1.0 ),
                                             vec2( 1.0, -1.0 ), vec2( 1.0, 1.0 ), vec2( -1.0, 1.0 ) );
            vec2 c  = corners[corner];
            v_UV    = c * 0.5 + 0.5;
            v_Color = vec4( rgb, alpha );

            // Camera-facing: offset the particle centre in VIEW space so the quad always faces the camera.
            vec3 viewPos = ( cameraUB.View * vec4( p.PosAge.xyz, 1.0 ) ).xyz;
            viewPos.xy += c * ( size * 0.5 );
            gl_Position = cameraUB.Projection * vec4( viewPos, 1.0 );
        }
    }

    Fragment
    {
        In(0) vec2 v_UV;
        In(1) vec4 v_Color;
        Out(0) vec4 o_Color;

        void main()
        {
            // Soft round sprite: radial falloff from the quad centre.
            vec2  d    = v_UV * 2.0 - 1.0;
            float mask = smoothstep( 1.0, 0.0, dot( d, d ) );
            float a    = v_Color.a * mask;
            if ( a <= 0.0 )
                discard;
            o_Color = vec4( v_Color.rgb, a );
        }
    }
}
