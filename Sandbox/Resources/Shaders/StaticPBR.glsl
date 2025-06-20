#pragma stage : vertex

#version 450 

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Bitangent;
layout(location = 4) in vec2 a_TextureCoord;

layout( push_constant ) uniform constants
{
	mat4 project;
	mat4 view;
} m_PushConstants;


layout(location=0) out Vertex
{
	vec3 WorldPosition;
	vec3 Normal;
	vec2 Texcoord;
} outVertex;

void main()
{
	outVertex.WorldPosition = a_Position;
	outVertex.Texcoord = a_TextureCoord;
	outVertex.Normal = 	 a_Normal;

	gl_Position =  m_PushConstants.project * m_PushConstants.view * vec4(a_Position, 1.0);
}

#pragma stage : fragment

#version 450 core

layout(location=0) in Vertex
{
	vec3 WorldPosition;
	vec3 Normal;
	vec2 Texcoord;
} inVertex;

const float PI = 3.141592;
const float Epsilon = 0.00001;

const vec3 Fdielectric = vec3(0.04);


layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform PBRData {
	vec3 		Albedo;
	float     	Metallic;
	float     	Roughness;
} pbr;

layout(binding = 1) uniform LightningUB {
	vec3 		Direction;
} lights;

layout(binding = 2) uniform GlobalUB {
	vec3 CameraPosition;
} global;

// Environment maps
layout (binding = 8) uniform samplerCube u_EnvSpecularTex;
layout (binding = 9) uniform samplerCube u_EnvIrradianceTex;

// BRDF LUT
layout (binding = 10) uniform sampler2D u_BRDFLUTTexture;

float ndfGGX(float cosLh, float roughness)
{
	float alpha   = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

float gaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

float gaSchlickGGX(float cosLi, float cosLo, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
	return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}

vec3 fresnelSchlick(vec3 F0, float cosTheta)
{
	return F0 + (vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 Lightning(vec3 view, vec3 N, vec3 F0, float metalness, float roughness, vec3 albedo)
{
	vec3 color = vec3(0);

	for(uint i = 0; i < 1; i++)
	{
		vec3 Li = -lights.Direction;
		vec3 Lradiance = vec3(1.0, 1.0, 1.0);
		vec3 Lh = normalize(Li + view);

		float cosLi = max(0.0, dot(N, Li));
		float cosLh = max(0.0, dot(N, Lh));
		float cosLo = max(0.0, dot(N, view));

		vec3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, view)));
		// Calculate normal distribution for specular BRDF.
		float D = ndfGGX(cosLh, roughness);
		// Calculate geometric attenuation for specular BRDF.
		float G = gaSchlickGGX(cosLi, cosLo, roughness);

		vec3 kd = (1.0 - F) * (1.0 - metalness);
		vec3 diffuseBRDF = kd * albedo;

		vec3 specularBRDF  = (F * D * G) / max(Epsilon, 4.0 * cosLi * cosLo);

		color += (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
	}

	return color;
}

vec3 IBL(vec3 view, vec3 N, vec3 F0, float metalness, float roughness, vec3 albedo)
{
	// Sample diffuse irradiance at normal direction.
	vec3 irradiance = texture(u_EnvIrradianceTex, N).rgb;

	float cosLo = max(0.0, dot(N, view));

	vec3 Lr = 2.0 * cosLo * N - view;

	// Calculate Fresnel term for ambient lighting.
	// Since we use pre-filtered cubemap(s) and irradiance is coming from many directions
	// use cosLo instead of angle with light's half-vector (cosLh above).
	// See: https://seblagarde.wordpress.com/2011/08/17/hello-world/
	vec3 F = fresnelSchlick(F0, cosLo);

	// Get diffuse contribution factor (as with direct lighting).
	vec3 kd = mix(vec3(1.0) - F, vec3(0.0), metalness);

	// Irradiance map contains exitant radiance assuming Lambertian BRDF, no need to scale by 1/PI here either.
	vec3 diffuseIBL = kd * albedo * irradiance;

	// Sample pre-filtered specular reflection environment at correct mipmap level.
	int specularTextureLevels = textureQueryLevels(u_EnvSpecularTex);
	vec3 specularIrradiance = textureLod(u_EnvSpecularTex, Lr, roughness * specularTextureLevels).rgb;

	// Split-sum approximation factors for Cook-Torrance specular BRDF.
	vec2 specularBRDF = texture(u_BRDFLUTTexture, vec2(cosLo, roughness)).rg;

	vec3 specularIBL = (F0 * specularBRDF.x + specularBRDF.y) * specularIrradiance;

	return diffuseIBL + specularIBL;
}

void main() {

	const vec3 albedo = pbr.Albedo;
	const float metalness = pbr.Metallic;
	const float roughness  = pbr.Roughness;

	const vec3 view = normalize(global.CameraPosition - inVertex.WorldPosition);

	vec3 F0 = mix(Fdielectric, albedo, metalness);
	vec3 light = Lightning(view, inVertex.Normal, F0, metalness, roughness, albedo );
	vec3 ibl = IBL(view, inVertex.Normal, F0, metalness, roughness, albedo);
    oColor = vec4(light + ibl, 1.0);
}