Shader "HeightFog"
{
    Compute
    {
        // The exponential height fog's evaluation pass: one closed-form integral per pixel, no march.
        //
        // Writes RGBA16F: .rgb = the fog's in-scattered radiance, PREMULTIPLIED, linear HDR;
        // .a = the transmittance of the fog between the camera and that pixel's geometry. The apply
        // pass (HeightFogApply.shader) then needs no more than `scene = fog.rgb + scene * fog.a`.
        //
        // LINEAR HDR ONLY — no tonemap, no gamma, no exposure; the engine's tonemap owns the curve and
        // runs later in the frame.
        //
        // WHY COMPUTE AND NOT A FRAGMENT PASS: the fog needs the scene depth, and the fullscreen apply
        // draws into a framebuffer that has the depth attachment BOUND — sampling a bound attachment is
        // a feedback loop. The depth read therefore happens here, outside any render pass, with
        // ComputeImageBeginRead handling the layout round-trip. One path for Forward and Deferred: both
        // write this depth attachment, only Deferred has a G-buffer (teamlead decision Q5,
        // Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md section 5).
        //
        // SKY PIXELS ARE FOGGED. Depth clears to 1.0 where nothing was drawn, and the reconstruction
        // below turns that into the far-plane point — a kilometre of fog toward the horizon, thinning
        // toward the zenith as the closed form's height term says, attenuated by StartDistance and
        // MaxOpacity. That is UE's behaviour (fog applies to skybox pixels through their far-plane
        // depth), and it is what produces the horizon veil.
        //
        // WHAT IS WHERE. The maths is Common/HeightFog.glslh, compiled as C++ by the HeightFog unit
        // tests from this same text; the parameter block is Common/FogParams.glslh, mirrored offset for
        // offset by Graphic::FogGpuPayload. What is left here is reconstruction and a store.

        #include <Common/HeightFog.glslh>
        #include <Common/FogParams.glslh>

        // rgba16f: radiance is pre-tonemap HDR and transmittance is in [0,1]; half precision carries
        // three decimal digits, an order more than an over-operator needs.
        layout(binding = 0, rgba16f) restrict writeonly uniform image2D u_FogApply;

        // The scene depth attachment, presented to this dispatch by ComputeImageBeginRead and handed
        // back afterwards. Point-sampled with texelFetch, never filtered: a filtered depth across a
        // silhouette averages foreground and background into a distance where nothing is.
        Uniform(2) sampler2D u_SceneDepth;

        PushConstant FogPush
        {
            mat4 u_InverseViewProjection;
            vec4 u_CameraPosition; // xyz = camera position in world units, w unused
        };

        LocalSize(8, 8, 1);

        void main()
        {
            ivec2 size  = imageSize(u_FogApply);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
            if (coord.x >= size.x || coord.y >= size.y)
                return;

            float deviceDepth = texelFetch(u_SceneDepth, coord, 0).r;

            vec2 uv  = (vec2(coord) + vec2(0.5f, 0.5f)) / vec2(size);
            vec2 ndc = vec2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

            // The world position from the camera's own inverse view-projection at the stored depth —
            // it inherits whatever projection and depth convention the frame was drawn with, and a sky
            // pixel (depth 1.0) lands on the far plane, which is exactly the distance UE fogs it at.
            vec4 worldH   = u_InverseViewProjection * vec4(ndc.x, ndc.y, deviceDepth, 1.0f);
            vec3 worldPos = worldH.xyz / max(worldH.w, 1e-9f);

            vec3 cameraKm = u_CameraPosition.xyz * (1.0f / HEIGHT_FOG_WORLD_UNITS_PER_KM);
            vec3 worldKm  = worldPos * (1.0f / HEIGHT_FOG_WORLD_UNITS_PER_KM);

            HeightFogResult fog = HeightFogEvaluate(FogUnpackParams(), cameraKm, worldKm);

            imageStore(u_FogApply, coord, vec4(fog.Inscattering, fog.Transmittance));
        }
    }
}
