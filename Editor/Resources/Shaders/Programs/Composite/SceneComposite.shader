Shader "SceneComposite"
{
    Fragment
    {
        In(0) vec2 v_TexCoord;
        Uniform(2) sampler2D u_GeometryTexture;
        Uniform(3) sampler2D u_BloomTexture;
        Uniform(4) sampler2D u_AvgLuminance;      // 1x1 adapted luminance (eye adaptation)
        Uniform(5) sampler2D u_LightShaftTexture; // radial sun streaks (LightShaftRenderer), half res
        Out(0) vec4 oColor;

        Uniform(0) TonemapUB
        {
            float u_Exposure;
            float u_Gamma;
            float u_BloomIntensity;
            float u_ExposureKey;          // middle-grey target for auto-exposure
            float u_AutoExposureEnabled;  // > 0.5 -> use measured luminance instead of manual exposure
            float u_ChromaticBloom;       // lens dispersion strength on the bloom halo (0 = off)
            float u_WhitePoint;           // the luminance that maps to pure white (see the operator below)
            vec4  u_LightShaftTintIntensity; // rgb = the sun light's Bloom Tint, a = Bloom Scale x screen fade
        };

        void main()
        {
            // The luminance that maps to pure white. It is a UNIFORM and not a constant, and that is the
            // whole point of this line: extended Reinhard is
            //     L' = L * (1 + L / W^2) / (1 + L)
            // and at W = 1 that expression reduces ALGEBRAICALLY to L' = L. The operator was hard-coded to
            // W = 1, so this pass tonemapped nothing — every luminance above 1 walked through untouched and
            // was clipped by the 8-bit store. That is what flattened bright content into paper silhouettes:
            // a cloud lit to 2.0 and one lit to 6.0 both wrote 255 and lost every gradient between them.
            // W is the value that now maps to 1.0, so everything below it keeps its shading.
            float pureWhite = max(u_WhitePoint, 1.0);

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

            vec3 color = (scene + bloom + shafts) * exposure;

            // Reinhard tonemapping operator.
        	// see: "Photographic Tone Reproduction for Digital Images", eq. 4
        	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        	float mappedLuminance = (luminance * (1.0 + luminance / (pureWhite * pureWhite))) / (1.0 + luminance);

        	// Scale color by ratio of average luminances.
        	vec3 mappedColor = (mappedLuminance / (luminance + 0.0001)) * color;

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
