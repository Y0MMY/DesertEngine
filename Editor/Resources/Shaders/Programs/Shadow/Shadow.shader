Shader "Shadow"
{
    Fragment
    {
        // Light-space depth written to an R32F colour target (sampled later in PBR). Using a colour target
        // instead of a sampled depth-stencil image sidesteps Vulkan depth-aspect sampling caveats.
        layout(location = 0) out vec4 o_Depth;

        void main()
        {
            o_Depth = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
        }
    }

    Vertex
    {
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Normal;
        layout(location = 2) in vec3 a_Tangent;
        layout(location = 3) in vec3 a_Bitangent;
        layout(location = 4) in vec2 a_TextureCoord;

        // The shared CameraUB is fed the LIGHT's view/projection by MaterialShadow (not the camera's).
        #include <Common/CameraUB.glslh>

        // Per-mesh transform, pushed automatically by Renderer::RenderMesh.
        layout( push_constant ) uniform constants
        {
            mat4 Transform;
        } m_PushConstants;

        void main()
        {
            gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4(a_Position, 1.0);
        }
    }
}
