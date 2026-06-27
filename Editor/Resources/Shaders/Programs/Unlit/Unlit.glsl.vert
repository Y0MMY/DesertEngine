#version 450

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
