#pragma stage : vertex

#version 450 

layout(location = 0) in vec3 a_Position;  
layout(location = 0) out vec2 v_TexCoord; 

vec2 textureCoords[4] = 
{
#ifdef VULKAN
    {0, 0},  
    {0, 1},  
    {1, 0},  
    {1, 1}   
#endif
};

void main()
{
    v_TexCoord = textureCoords[gl_VertexIndex];
    gl_Position = vec4(a_Position, 1.0);
}

#pragma stage : fragment

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
	vec3 mappedColor = (mappedLuminance / luminance) * color;

	// Gamma correction.
	oColor = vec4(pow(mappedColor, vec3(1.0 / gamma)), 1.0);

}