// Fully data-driven surface shader in the Desert Shader Language (single file: properties,
// render state and all stages together). The Properties block both drives the Details UI and
// (via Binding/TextureBinding) auto-generates the MaterialUB uniform block + samplers in the
// fragment stage — the parameter is declared exactly once.
Shader "Unlit"
{
    Domain Surface

    Properties Binding(1) TextureBinding(2)
    {
        Color     Color       ("Color")  = (0.8, 0.4, 0.1, 1)
        Texture2D u_AlbedoTex ("Albedo")
    }

    State
    {
        Cull Back
        ZTest LEqual
        ZWrite On
    }

    Vertex
    {
        layout( location = 0 ) in vec3 a_Position;
        layout( location = 1 ) in vec3 a_Normal;
        layout( location = 2 ) in vec3 a_Tangent;
        layout( location = 3 ) in vec3 a_Bitangent;
        layout( location = 4 ) in vec2 a_TextureCoord;

        #include "../../Common/CameraUB.glslh"

        layout( push_constant ) uniform PushConstants
        {
            mat4 Transform;
        }
        m_PushConstants;

        layout( location = 0 ) out vec2 v_UV;

        void main()
        {
            v_UV        = a_TextureCoord;
            gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4( a_Position, 1.0 );
        }
    }

    Fragment
    {
        layout( location = 0 ) in vec2 v_UV;
        layout( location = 0 ) out vec4 o_Color;

        void main()
        {
            o_Color = texture( u_AlbedoTex, v_UV ) * u_Material.Color;
        }
    }

    // Depth-only variant (vertex-only program, front-face culling against peter-panning).
    // A separate program registered as "Unlit/Depth" — fetch it with GetByName("Unlit/Depth").
    Pass "Depth"
    {
        State
        {
            Cull Front
        }

        Vertex
        {
            layout( location = 0 ) in vec3 a_Position;
            layout( location = 1 ) in vec3 a_Normal;
            layout( location = 2 ) in vec3 a_Tangent;
            layout( location = 3 ) in vec3 a_Bitangent;
            layout( location = 4 ) in vec2 a_TextureCoord;

            #include "../../Common/CameraUB.glslh"

            layout( push_constant ) uniform PushConstants
            {
                mat4 Transform;
            }
            m_PushConstants;

            void main()
            {
                gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4( a_Position, 1.0 );
            }
        }
    }
}

