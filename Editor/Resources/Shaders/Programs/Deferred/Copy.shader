Shader "Copy"
{
    // Full-screen image copy (scene colour snapshot for glass refraction).

    Vertex
    {
        #include <Common/QuadPositions.glslh>
        #include <Common/QuadTextureCoords.glslh>

        layout(location = 0) out vec2 v_TexCoord;

        void main()
        {
        	v_TexCoord  = QUAD_TEXTURE_COORDINATES[gl_VertexIndex];
        	gl_Position = vec4(QUAD_POSITIONS[gl_VertexIndex], 0.0, 1.0);
        }
    }

    Fragment
    {
        // Trivial full-screen copy: samples an input image and writes it out. Used to snapshot the composited scene
        // colour into a separate texture so the glass pass can sample it (refraction) without a read+write feedback
        // loop on the scene target.

        layout(location = 0) in vec2 v_TexCoord;

        layout(binding = 1) uniform sampler2D u_Input;

        layout(location = 0) out vec4 oColor;

        void main()
        {
        	oColor = texture(u_Input, v_TexCoord);
        }
    }
}
