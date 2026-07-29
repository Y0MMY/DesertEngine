Shader "Skybox"
{
    Fragment
    {
        layout(location = 0) out vec4 oColor;

        layout (binding = 1) uniform samplerCube samplerCubeMap;

        // HDR skybox brightness — driven by SkyboxComponent::Intensity (x holds the multiplier).
        layout(std140, binding = 2) uniform SkyboxParamsUB
        {
            vec4 u_SkyboxParams;
        };

        layout(location = 3) in vec3 inUVW;
        layout(location = 4) in vec3  v_Position;

        void main()
        {
        	oColor = texture(samplerCubeMap, v_Position) * u_SkyboxParams.x;
        }
    }

    Vertex
    {
        #include <Common/QuadPositions.glslh>
        #include <Common/CameraUB.glslh>

        layout(location =3) out vec3 outUVW ;
        layout(location =4) out vec3   v_Position ;

        void main()
        {
            vec4 position = vec4(QUAD_POSITIONS[gl_VertexIndex], 1.0, 1.0);
        	gl_Position = position;

            mat4 inverseVP = inverse(cameraUB.Projection * cameraUB.View);

        	v_Position = ((inverseVP * position).xyz);
        }
    }
}
