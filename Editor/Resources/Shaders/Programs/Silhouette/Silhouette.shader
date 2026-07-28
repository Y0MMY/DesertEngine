Shader "Silhouette"
{
    Vertex
    {
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;
        layout(location = 2) in vec3 a_Tangent;
        layout(location = 3) in vec3 a_Bitangent;
        layout(location = 4) in vec2 a_TextureCoord;

        #include <Common/CameraUB.glslh>

        // Transform is supplied automatically by Renderer::RenderMesh as the only push constant.
        layout( push_constant ) uniform constants
        {
            mat4 Transform;
        } m_PushConstants;

        void main()
        {
            gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4(a_Position, 1.0);
        }
    }

    Fragment
    {
        layout(location = 0) out vec4 o_Color;

        void main()
        {
            o_Color = vec4(1.0, 1.0, 1.0, 1.0);
        }
    }
}
