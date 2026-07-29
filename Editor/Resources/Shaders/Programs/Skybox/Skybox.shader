Shader "Skybox"
{
    Fragment
    {
        Out(0) vec4 oColor;

        Uniform(1) samplerCube samplerCubeMap;

        // HDR skybox brightness — driven by SkyboxComponent::Intensity (x holds the multiplier).
        Uniform(2) SkyboxParamsUB
        {
            vec4 u_SkyboxParams;
        };

        In(3) vec3 inUVW;
        In(4) vec3  v_Position;

        void main()
        {
        	oColor = texture(samplerCubeMap, v_Position) * u_SkyboxParams.x;
        }
    }

    Vertex
    {
        #include <Common/QuadPositions.glslh>
        #include <Common/CameraUB.glslh>

        Out(3) vec3 outUVW ;
        Out(4) vec3   v_Position ;

        void main()
        {
            vec4 position = vec4(QUAD_POSITIONS[gl_VertexIndex], 1.0, 1.0);
        	gl_Position = position;

            mat4 inverseVP = inverse(cameraUB.Projection * cameraUB.View);

        	v_Position = ((inverseVP * position).xyz);
        }
    }
}
