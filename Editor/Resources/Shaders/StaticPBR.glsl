#pragma stage : vertex
#version 450 

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec3 a_Bitangent;
layout(location = 4) in vec2 a_TextureCoord;

#include "Common/CameraUB.glslh"

layout( push_constant ) uniform constants
{
	mat4 Transform;
} m_PushConstants;


layout(location=0) out Vertex
{
	vec3 WorldPosition;
	vec3 Normal;
	vec2 Texcoord;
	mat3 TBN;
	vec3 CameraPosition;
} outVertex;

void main()
{
	outVertex.WorldPosition = vec3(m_PushConstants.Transform * vec4(a_Position, 1.0));
	outVertex.Texcoord = vec2(a_TextureCoord.x, 1.0 - a_TextureCoord.y);

	mat3 modelRotation =  transpose(inverse(mat3(m_PushConstants.Transform)));
	outVertex.Normal = 	mat3(m_PushConstants.Transform) * a_Normal;
	outVertex.TBN = modelRotation * mat3(a_Tangent, a_Bitangent, a_Normal);
	outVertex.CameraPosition = cameraUB.CameraPos;

	gl_Position =  cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4(a_Position, 1.0);
}

#pragma stage : fragment
#version 450 core

#include "Mesh/PointLight.glslh"
#include "Mesh/LightsMetadata.glslh"

layout(location=0) in Vertex
{
	vec3 WorldPosition;
	vec3 Normal;
	vec2 Texcoord;
	mat3 TBN;
	vec3 CameraPosition;
} inVertex;

const float Epsilon = 0.00001;

const vec3 Fdielectric = vec3(0.04);


layout(location = 0) out vec4 oColor;

layout(binding = 2) uniform MaterialProperties {
	vec3	  AlbedoColor;
    float     AlbedoBlend;
    float     MetallicValue;
    float     MetallicBlend;
    float     RoughnessValue;
    float     RoughnessBlend;
    vec3	  EmissionColor;
    float     EmissionStrength;
    float     AOValue;
} pbr;

struct DirectionLight
{
	vec3 Direction;
};

layout(binding = 1) uniform LightningUB {
	DirectionLight 		directionLights;
} directionLights;

// Environment maps
layout (binding = 8) uniform samplerCube u_EnvSpecularTex;
layout (binding = 9) uniform samplerCube u_EnvIrradianceTex;

// BRDF LUT
layout (binding = 10) uniform sampler2D u_BRDFLUTTexture;

layout(binding = 11) uniform sampler2D u_AlbedoTexture;
layout(binding = 12) uniform sampler2D u_NormalTexture;

struct Params
{
	vec3 AlbedoColor;
	vec3 Normal;
} m_Params;

vec3 Lightning(vec3 view, vec3 N, vec3 F0, float metalness, float roughness, vec3 albedo)
{
	vec3 color = vec3(0);

	for(uint i = 0; i < lightsMetadata.DirectionLightCount; i++)
	{
		vec3 Li = -directionLights.directionLights.Direction;
		vec3 Lradiance = vec3(1.0, 1.0, 1.0);
		vec3 Lh = normalize(Li + view);

		float cosLi = max(0.0, dot(N, Li));
		float cosLh = max(0.0, dot(N, Lh));
		float cosLo = max(0.0, dot(N, view));

		vec3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, view)));
		// Calculate normal distribution for specular BRDF.
		float D = DistributionGGX(cosLh, roughness);
		// Calculate geometric attenuation for specular BRDF.
		float G = GeometrySchlickGGX(cosLi, cosLo, roughness);

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

	m_Params.AlbedoColor = pbr.AlbedoColor * pbr.AlbedoBlend;
	m_Params.AlbedoColor *= texture(u_AlbedoTexture, inVertex.Texcoord).rgb;
	m_Params.Normal =  normalize(inVertex.Normal);

	const ivec2 textureSize = textureSize(u_NormalTexture, 0);
	if(textureSize.x > 1 && textureSize.y > 1) // not fallback (TODO. bad way)
	{
		m_Params.Normal = normalize(2.0 * texture(u_NormalTexture, inVertex.Texcoord).rgb - 1.0);
	}
	m_Params.Normal = normalize(inVertex.TBN * m_Params.Normal);

	const float metalness = pbr.MetallicValue * pbr.MetallicBlend;
	const float roughness  = pbr.RoughnessValue * pbr.RoughnessBlend;

	const vec3 view = normalize(inVertex.CameraPosition - inVertex.WorldPosition);

	vec3 F0 = mix(Fdielectric, m_Params.AlbedoColor, metalness);
	vec3 light = Lightning(view, m_Params.Normal, F0, metalness, roughness, m_Params.AlbedoColor );
	vec3 ibl = IBL(view, m_Params.Normal, F0, metalness, roughness, m_Params.AlbedoColor);

	vec3 pointLight = vec3(0.0);

	for(uint i = 0; i < lightsMetadata.PointLightCount; i++)
	{
		PointLight light = pointLights.lights[i];
        pointLight += CalculatePointLight(light, inVertex.WorldPosition, view, 
                                        m_Params.Normal, F0, metalness, 
                                        roughness, m_Params.AlbedoColor);
	}

    oColor = vec4( pointLight, 1.0);
}