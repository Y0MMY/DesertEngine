Shader "SceneComposite"
{
    Fragment
    {
        In(0) vec2 v_TexCoord;
        Uniform(2) sampler2D u_GeometryTexture;
        Uniform(3) sampler2D u_BloomTexture;
        Uniform(4) sampler2D u_AvgLuminance;      // 1x1 adapted luminance (eye adaptation)
        Uniform(5) sampler2D u_LightShaftTexture; // radial sun streaks (LightShaftRenderer), half res
        Uniform(6) sampler2D u_LensFlareTexture;  // ghosts/halo/streak (LensFlareRenderer), quarter res
        Out(0) vec4 oColor;

        Uniform(0) TonemapUB
        {
            float u_Exposure;
            float u_Gamma;
            float u_BloomIntensity;
            float u_ExposureKey;          // middle-grey target for auto-exposure
            float u_AutoExposureEnabled;  // > 0.5 -> use measured luminance instead of manual exposure
            float u_ChromaticBloom;       // lens dispersion strength on the bloom halo (0 = off)
            float u_WhitePoint;           // REINHARD ONLY: the luminance that maps to pure white (see below)
            float u_TonemapOperator;      // Core::TonemapOperator: < 0.5 = ACES, otherwise extended Reinhard
            vec4  u_LightShaftTintIntensity; // rgb = the sun light's Bloom Tint, a = Bloom Scale x screen fade
            vec4  u_LensFlareTintIntensity;  // rgb = the lens's Tint, a = Intensity x screen fade
        };

        // ACES, as Unreal grades. This is the DEFAULT operator (decision D-10): the reference frame the
        // sky programme measures itself against was captured through UE's ACES-derived film curve, so
        // while the two sides used different operators every number in Docs/Clouds/CALIBRATION.md was
        // reporting the gap between two TONEMAPPERS rather than between two skies.
        //
        // WHICH FIT, AND WHY THIS ONE. Two approximations are in common use. Narkowicz's is a single
        // rational curve applied per channel in sRGB — one line, and visibly not the same operator: it
        // rolls each channel off independently, so a bright SATURATED colour shifts hue on its way to
        // white instead of desaturating toward it. Stephen Hill's fit (BakingLab/ACES.hlsl, MIT) keeps
        // the structure of the real thing: into AP1, the RRT+ODT shoulder there, back out. That matters
        // here specifically, because mean SATURATION is one of the five numbers this programme
        // calibrates with — an operator that gets saturation wrong corrupts the very measurement the
        // operator was changed to make honest. The price is two 3x3 products in one fullscreen pass,
        // which is nothing next to the cloud march it is grading.
        //
        // The constants are the published matrices TRANSPOSED: the HLSL original lists ROWS, GLSL's
        // mat3(...) fills COLUMNS. Copying them across verbatim gives a colour rotation that looks
        // plausible and is wrong, which is the kind of defect this programme has already paid for once.
        vec3 TonemapACES(vec3 color)
        {
            const mat3 ACESInput = mat3(
                0.59719, 0.07600, 0.02840,
                0.35458, 0.90834, 0.13383,
                0.04823, 0.01566, 0.83777);
            const mat3 ACESOutput = mat3(
                 1.60475, -0.10208, -0.00327,
                -0.53108,  1.10813, -0.07276,
                -0.07367, -0.00605,  1.07602);

            vec3 v = ACESInput * color;
            vec3 a = v * (v + 0.0245786) - 0.000090537;
            // No epsilon and none needed: 0.983729x^2 + 0.432951x + 0.238081 has a negative
            // discriminant, so it has no real root and cannot be zero for any v the matrix can produce
            // (including the negatives a saturated colour picks up on the way into AP1).
            vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
            // The clamp is the ODT's range, not a safety net: AP1 holds colours sRGB cannot, and those
            // come back out of the second matrix slightly outside [0,1].
            return clamp(ACESOutput * (a / b), 0.0, 1.0);
        }

        // Extended Reinhard, on luminance, preserving the colour's ratio to it.
        // see: "Photographic Tone Reproduction for Digital Images", eq. 4
        //
        // pureWhite is a UNIFORM and not a constant, and that is the whole point of it:
        //     L' = L * (1 + L / W^2) / (1 + L)
        // at W = 1 reduces ALGEBRAICALLY to L' = L. The operator was once hard-coded to W = 1, so this
        // pass tonemapped nothing — every luminance above 1 walked through untouched and was clipped by
        // the 8-bit store, which is what flattened bright content into paper silhouettes: a highlight lit
        // to 2.0 and one lit to 6.0 both wrote 255 and lost every gradient between them.
        vec3 TonemapReinhard(vec3 color, float whitePoint)
        {
            float pureWhite       = max(whitePoint, 1.0);
            float luminance       = dot(color, vec3(0.2126, 0.7152, 0.0722));
            float mappedLuminance = (luminance * (1.0 + luminance / (pureWhite * pureWhite))) / (1.0 + luminance);
            return (mappedLuminance / (luminance + 0.0001)) * color;
        }

        void main()
        {
            // Auto-exposure: scale so the adapted average luminance maps to the key value; else manual exposure.
            float exposure = u_Exposure;
            if (u_AutoExposureEnabled > 0.5)
            {
                float adaptedLum = texture(u_AvgLuminance, vec2(0.5)).r;
                exposure = u_ExposureKey / max(adaptedLum, 1e-4);
            }

            vec3 scene = texture(u_GeometryTexture, v_TexCoord).rgb;

            // Bloom. With lens dispersion on, sample the bloom per-channel along a radial offset (R pushed outward,
            // B inward), proportional to distance from the screen centre -> a rainbow fringe / glare around bright
            // sources. The global sampler is REPEAT, so clamp the offset UVs to [0,1] (else bright content wraps).
            vec3 bloom;
            if (u_ChromaticBloom > 0.0001)
            {
                vec2  dir   = v_TexCoord - vec2(0.5);
                vec2  off   = dir * (0.012 * u_ChromaticBloom);
                bloom.r = texture(u_BloomTexture, clamp(v_TexCoord + off, 0.0, 1.0)).r;
                bloom.g = texture(u_BloomTexture, v_TexCoord).g;
                bloom.b = texture(u_BloomTexture, clamp(v_TexCoord - off, 0.0, 1.0)).b;
            }
            else
            {
                bloom = texture(u_BloomTexture, v_TexCoord).rgb;
            }
            bloom *= u_BloomIntensity;

            // Light shafts: additive HDR streaks toward the sun, BEFORE the tonemap so a strong shaft
            // rolls off through the same operator everything else does instead of clipping. The
            // intensity carries the screen-edge fade, so a sun leaving the view takes its streaks with
            // it; when the effect is off the intensity is exactly zero and the (stale) texture is inert.
            vec3 shafts = texture(u_LightShaftTexture, v_TexCoord).rgb *
                          (u_LightShaftTintIntensity.rgb * u_LightShaftTintIntensity.a);

            // Lens flare: added in HDR alongside bloom and the shafts, before the tonemap, so a bright
            // ghost rolls off through the same operator instead of clipping to a flat disc. The intensity
            // carries the sun's screen-edge fade and is exactly zero whenever the flare pass did not run,
            // which is what makes the (stale) texture inert — the bloom image's contract.
            vec3 flare = texture(u_LensFlareTexture, v_TexCoord).rgb *
                         (u_LensFlareTintIntensity.rgb * u_LensFlareTintIntensity.a);

            vec3 color = (scene + bloom + shafts + flare) * exposure;

        	// The scene's chosen operator. Both branches return LINEAR colour — the gamma encode below is
        	// the display transfer function and belongs to neither of them.
        	vec3 mappedColor = (u_TonemapOperator < 0.5) ? TonemapACES(color)
        	                                             : TonemapReinhard(color, u_WhitePoint);

        	// Gamma correction.
        	vec3 mapped = pow(mappedColor, vec3(1.0 / u_Gamma));

        	// Ordered dithering on the FINAL 8-bit output to break gradient banding. Without it, smooth gradients
        	// (esp. the procedural sky) quantize into 8-bit steps; under camera motion those steps sweep across the
        	// screen and read as a "lighter/darker" shimmer. Keyed on gl_FragCoord so the pattern is stationary on
        	// screen (never crawls). +/- ~0.5 LSB is enough to randomize the rounding and is invisible otherwise.
        	float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
        	mapped += (dither - 0.5) / 255.0;

        	oColor = vec4(mapped, 1.0);
        }
    }

    Vertex
    {
        #include <Common/QuadPositions.glslh>
        #include <Common/QuadTextureCoords.glslh>

        Out(0) vec2 v_TexCoord; 

        void main()
        {
            v_TexCoord = QUAD_TEXTURE_COORDINATES[gl_VertexIndex];
            gl_Position = vec4(QUAD_POSITIONS[gl_VertexIndex], 0.0, 1.0);
        }
    }
}
