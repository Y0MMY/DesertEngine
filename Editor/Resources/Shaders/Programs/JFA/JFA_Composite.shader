Shader "JFA_Composite"
{
    Vertex
    {
        #include <Common/QuadPositions.glslh>
        #include <Common/QuadTextureCoords.glslh>

        layout(location = 0) out vec2 v_TexCoord;

        void main()
        {
            v_TexCoord = QUAD_TEXTURE_COORDINATES[gl_VertexIndex];
            gl_Position = vec4(QUAD_POSITIONS[gl_VertexIndex], 0.0, 1.0);
        }
    }

    Fragment
    {
        layout(location = 0) in vec2 v_TexCoord;

        layout(location = 0) out vec4 o_Color;

        layout(set = 0, binding = 0) uniform sampler2D u_OutlineResult;

        void main()
        {
            o_Color = texture(u_OutlineResult, v_TexCoord);
        }
    }
}
