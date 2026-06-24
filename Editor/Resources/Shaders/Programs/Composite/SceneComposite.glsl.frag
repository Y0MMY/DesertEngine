#version 450 core

layout(location = 0) in vec2 v_TexCoord;
layout(binding = 2) uniform sampler2D u_GeometryTexture;
layout(binding = 3) uniform sampler2D u_BloomTexture;
layout(binding = 4) uniform sampler2D u_AvgLuminance; // 1x1 adapted luminance (eye adaptation)
layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform TonemapUB
{
    float u_Exposure;
    float u_Gamma;
    float u_BloomIntensity;
    float u_ExposureKey;          // middle-grey target for auto-exposure
    float u_AutoExposureEnabled;  // > 0.5 -> use measured luminance instead of manual exposure
};

void main()
{
    const float pureWhite = 1.0;

    // Auto-exposure: scale so the adapted average luminance maps to the key value; else manual exposure.
    float exposure = u_Exposure;
    if (u_AutoExposureEnabled > 0.5)
    {
        float adaptedLum = texture(u_AvgLuminance, vec2(0.5)).r;
        exposure = u_ExposureKey / max(adaptedLum, 1e-4);
    }

    vec3 scene = texture(u_GeometryTexture, v_TexCoord).rgb;
    vec3 bloom = texture(u_BloomTexture, v_TexCoord).rgb * u_BloomIntensity;
    vec3 color = (scene + bloom) * exposure;

    // Reinhard tonemapping operator.
	// see: "Photographic Tone Reproduction for Digital Images", eq. 4
	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
	float mappedLuminance = (luminance * (1.0 + luminance / (pureWhite * pureWhite))) / (1.0 + luminance);

	// Scale color by ratio of average luminances.
	vec3 mappedColor = (mappedLuminance / (luminance + 0.0001)) * color;

	// Gamma correction.
	oColor = vec4(pow(mappedColor, vec3(1.0 / u_Gamma)), 1.0);
}
