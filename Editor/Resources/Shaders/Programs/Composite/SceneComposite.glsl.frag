#version 450 core

layout(location = 0) in vec2 v_TexCoord; 
layout(binding = 2) uniform sampler2D u_GeometryTexture; 
layout(location = 0) out vec4 oColor; 

void main()
{
    const float gamma     = 2.2;
	const float pureWhite = 1.0;

    ivec2 texSize = textureSize(u_GeometryTexture, 0);
	ivec2 texCoord = ivec2(v_TexCoord * texSize);

    vec3 color = texture(u_GeometryTexture, v_TexCoord).rgb;

    // Reinhard tonemapping operator.
	// see: "Photographic Tone Reproduction for Digital Images", eq. 4
	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
	float mappedLuminance = (luminance * (1.0 + luminance / (pureWhite * pureWhite))) / (1.0 + luminance);

	// Scale color by ratio of average luminances.
	vec3 mappedColor = (mappedLuminance / (luminance + 0.0001)) * color;

	// Gamma correction.
	oColor = vec4(pow(mappedColor, vec3(1.0 / gamma)), 1.0);

}