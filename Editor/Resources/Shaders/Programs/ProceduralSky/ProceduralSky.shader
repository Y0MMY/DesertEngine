Shader "ProceduralSky"
{
    Fragment
    {
        // The engine-generated sky: an artistic gradient evaluated from the view ray and the sun direction
        // (no HDR texture). Output is LINEAR HDR — the post-process tonemap pass applies exposure/gamma
        // downstream, exactly like the old skybox did.
        //
        // The model lives in Common/Atmosphere.glslh and is shared with the IBL bake
        // (Compute/BakeProceduralSky) so the visible sky and the light it casts are identical.

        #include <Common/Atmosphere.glslh>

        In(0) vec3 v_RayDir;
        Out(0) vec4 oColor;

        // The sky parameter block. A std430 STORAGE buffer rather than a uniform block, because the
        // volumetric cloud pass evaluates the same sky from a compute shader and ComputePipeline has no
        // SetUniformBuffer — its binding surface is inputs, outputs, storage buffers and push constants.
        // One buffer, one layout, three consumers.
        //
        // The binding is written explicitly as (1) and must stay equal to Graphic::kSkyPayloadBinding: the
        // graphics descriptor write uses the buffer's OWN binding number while a compute dispatch passes
        // one as an argument, and when those disagree the buffer quietly lands on another resource's slot.
        ReadBuffer(1) SkyBuffer
        {
            vec4 u_SkyPacked[SKY_PACKED_VEC4_COUNT];
        };

        void main()
        {
            SkyPacked s;
            for (int i = 0; i < SKY_PACKED_VEC4_COUNT; ++i)
                s.v[i] = u_SkyPacked[i];

            vec3 color = EvaluateSky(normalize(v_RayDir), UnpackSunDirection(s), UnpackSunIntensity(s),
                                     UnpackSunAngularRadius(s), UnpackSkyConfig(s));

            // (Dithering is done at the FINAL 8-bit output in the composite/tonemap pass — doing it here in linear
            // HDR is lost through Reinhard+gamma. See SceneComposite.glsl.frag.)
            oColor = vec4(color, 1.0);
        }
    }

    Vertex
    {
        #include <Common/QuadPositions.glslh>
        #include <Common/CameraUB.glslh>

        // World-space view ray for this fullscreen pixel (un-normalized; normalized in the fragment).
        Out(0) vec3 v_RayDir;

        void main()
        {
            vec4 position = vec4(QUAD_POSITIONS[gl_VertexIndex], 1.0, 1.0);
            gl_Position = position;

            // DIRECTION-ONLY world-space view ray (camera rotation only — NO far-plane-worldPos minus cameraPos).
            // That subtraction of two large world-space points loses float precision and, as the camera MOVES, makes
            // the ray direction jitter frame-to-frame → the tiny sun/stars "boil". Unproject to VIEW space, then
            // rotate to world with mat3(invView). Correct for this sky, which depends only on direction.
            vec4 viewH   = inverse(cameraUB.Projection) * position;
            vec3 viewRay = viewH.xyz / viewH.w;
            v_RayDir     = mat3(inverse(cameraUB.View)) * viewRay;
        }
    }
}
