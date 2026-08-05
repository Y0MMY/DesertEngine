Shader "UIGlass"
{
    // Frosted-glass UI rectangle: fills with the BLURRED scene behind it instead of a texture.
    //
    // The blurred copy is a mip pyramid built by BackdropBlurRenderer right before the UI phase — the UI
    // draws into the scene target, and a shader may not sample the attachment it writes, so it samples the
    // snapshot instead. Higher LOD = blurrier.
    //
    // The rounded-rect mask is an SDF here rather than tessellated corners: this pass already runs per
    // element (each rect carries its own push constants), and an SDF edge antialiases for free.
    Vertex
    {
        In(0) vec2 a_Position;   // pixel coordinates (top-left origin)
        In(1) vec2 a_TexCoord;
        In(2) vec4 a_Color;      // tint; alpha = how much the tint covers the blur

        Out(0) vec2 v_TexCoord;
        Out(1) vec4 v_Color;

        PushConstant constants
        {
            mat4 Projection;   // pixel -> clip, same as UI2D
            vec4 Rect;         // min.xy, max.xy in pixels
            vec4 Params;       // x = corner radius px, y = blur LOD, zw = 1 / viewport size
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

        Uniform(0) sampler2D u_Backdrop;

        PushConstant constants
        {
            mat4 Projection;
            vec4 Rect;
            vec4 Params;
        } m_PushConstants;

        Out(0) vec4 o_Color;

        // Signed distance to a rounded box centred on the origin. Negative inside.
        float RoundedBoxSDF(vec2 p, vec2 halfSize, float radius)
        {
            vec2 q = abs(p) - halfSize + radius;
            return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
        }

        void main()
        {
            // The backdrop is a snapshot of THIS target, so screen position maps to it directly.
            vec2 screenUV = gl_FragCoord.xy * m_PushConstants.Params.zw;
            vec3 blurred  = textureLod(u_Backdrop, screenUV, m_PushConstants.Params.y).rgb;

            // Tint over the blur: alpha 0 = pure blur, 1 = flat colour (a plain panel).
            vec3 color = mix(blurred, v_Color.rgb, v_Color.a);

            vec2  center   = (m_PushConstants.Rect.xy + m_PushConstants.Rect.zw) * 0.5;
            vec2  halfSize = (m_PushConstants.Rect.zw - m_PushConstants.Rect.xy) * 0.5;
            float radius   = min(m_PushConstants.Params.x, min(halfSize.x, halfSize.y));

            float dist = RoundedBoxSDF(gl_FragCoord.xy - center, halfSize, radius);
            // One pixel of feather, so corners are smooth without any geometry.
            float mask = 1.0 - smoothstep(-0.5, 0.5, dist);
            if (mask <= 0.0)
                discard;

            // Opaque inside the mask: the fill already CONTAINS what is behind it, so blending it again
            // would double-count the background.
            o_Color = vec4(color, mask);
        }
    }
}
