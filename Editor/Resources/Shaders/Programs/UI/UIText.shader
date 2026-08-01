Shader "UIText"
{
    // Screen-space SDF text for the 2D batcher. Same vertex layout as UI2D (pos/uv/colour, ortho push
    // constant), but the fragment reads a single-channel signed-distance glyph atlas (FontService) and
    // antialiases the edge in screen space via fwidth — crisp at any font size. Tinted by the vertex colour.
    Vertex
    {
        In(0) vec2 a_Position;
        In(1) vec2 a_TexCoord;
        In(2) vec4 a_Color;

        Out(0) vec2 v_TexCoord;
        Out(1) vec4 v_Color;

        PushConstant constants
        {
            mat4 Projection;
        } m_PushConstants;

        void main()
        {
            v_TexCoord  = a_TexCoord;
            v_Color     = a_Color;
            gl_Position = m_PushConstants.Projection * vec4(a_Position, 0.0, 1.0);
        }
    }

    Fragment
    {
        In(0) vec2 v_TexCoord;
        In(1) vec4 v_Color;

        Uniform(0) sampler2D u_SDFAtlas;

        Out(0) vec4 o_Color;

        void main()
        {
            // SDF stored around 0.5 (the baker's on-edge value 128/255). Screen-space AA width from the
            // distance-field gradient keeps edges crisp at any size.
            float dist  = texture(u_SDFAtlas, v_TexCoord).r;
            float width = max(fwidth(dist), 0.0001);
            float alpha = smoothstep(0.5 - width, 0.5 + width, dist);

            if (alpha <= 0.0)
                discard;

            o_Color = vec4(v_Color.rgb, v_Color.a * alpha);
        }
    }
}
