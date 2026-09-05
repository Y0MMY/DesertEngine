// SDF text: samples a single-channel signed-distance font atlas and outputs EMISSIVE HDR colour so
// the existing bloom pass picks up bright text for free (EmissiveIntensity > ~1 blooms). Alpha-blended
// over the scene; the edge is antialiased in screen space via fwidth of the distance field.
Shader "TextSDF"
{
    Domain Surface

    Properties Binding(1) TextureBinding(2)
    {
        Color     TextColor         ("Text Color") = (1, 1, 1, 1)
        float     EmissiveIntensity ("Emissive Intensity") = 1.0
        Texture2D u_SDFAtlas        ("SDF Atlas")
    }

    State
    {
        Cull None
        ZTest LEqual
        ZWrite Off
        Blend SrcAlpha OneMinusSrcAlpha
    }

    Vertex
    {
        In(0) vec3 a_Position;
        In(1) vec3 a_Normal;
        In(2) vec3 a_Tangent;
        In(3) vec3 a_Bitangent;
        In(4) vec2 a_TextureCoord;

        #include <Common/CameraUB.glslh>

        // Transform + the material row index, in the one block both stages declare (see the header).
        // Text is the producer that needed the row most: every 3D label in a scene draws through ONE
        // shared TextSDF material, so a per-material colour block gave them all the first label's colour.
        #include <Common/MaterialTransport.glslh>

        Out(0) vec2 v_UV;

        void main()
        {
            v_UV        = a_TextureCoord;
            gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4( a_Position, 1.0 );
        }
    }

    Fragment
    {
        In(0) vec2 v_UV;
        Out(0) vec4 o_Color;

        void main()
        {
            // SDF stored around 0.5 (the baker's on-edge value 128/255). Screen-space AA width from the
            // distance-field gradient keeps edges crisp at any world size / camera distance.
            float dist  = texture( u_SDFAtlas, v_UV ).r;
            float width = max( fwidth( dist ), 0.0001 );
            float alpha = smoothstep( 0.5 - width, 0.5 + width, dist );

            if ( alpha <= 0.0 )
                discard;

            vec3 emissive = u_Material.TextColor.rgb * u_Material.EmissiveIntensity;
            o_Color       = vec4( emissive, alpha * u_Material.TextColor.a );
        }
    }
}
