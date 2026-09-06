// THE cubemap-domain program: colour per DIRECTION, geometry synthesized from gl_VertexIndex.
//
// Two consumers, one program:
//   - the engine's skybox pass (MaterialSkybox binds `samplerCubeMap` from a SkyboxComponent's
//     baked environment and fills SkyboxParamsUB from the component's Intensity);
//   - a `.demat` naming this shader — a CUBEMAP MATERIAL. The Properties block below is that
//     material's schema: one cube slot the Material Editor lets an HDR skybox asset be dropped on.
//
// The Domain line makes the second consumer classifiable: the Material Editor previews a
// Skybox-domain material as a cubemap on an orbitable sphere (see Editor's CubemapSphere.shader —
// the direction is the sphere's own surface direction there, where here it is the camera ray).
// Skybox stays OUT of IsUserAssignable() on purpose: no mesh slot can draw a program whose vertex
// stage ignores the vertex buffer, and MeshRenderer::DrawGenericMeshes refuses it by name.
Shader "Skybox"
{
    Domain Skybox

    // NO TextureBinding option: the sampler is declared by hand in the Fragment stage below, at the
    // binding the engine skybox pass has always used, so this block adds schema without moving a
    // single binding under the pass's feet.
    Properties
    {
        TextureCube samplerCubeMap ("Cubemap")
    }

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
