#version 450 core

#include "../../Mesh/PointLight.glslh"
#include "../../Mesh/LightsMetadata.glslh"

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

// Shared push-constant block. Must be byte-for-byte identical to the one in Static.glsl.vert /
// Skinned.glsl.vert. Per-object material data lives in the Materials[] storage buffer (GPU-scene
// style); the push constant only carries the per-object index into it.
layout( push_constant ) uniform PushConstants
{
	mat4 Transform;     // offset 0   (vertex)
	uint MaterialIndex; // offset 64  index into Materials[]
} pc;

// One entry per drawn object (std430). Filled on the CPU each frame (per-object / per-instance).
struct GpuMaterial
{
	vec4 AlbedoAO;           // rgb = albedo, a = ambient occlusion
	vec4 MetalRoughEmission; // x = metallic, y = roughness, z = emission strength
	vec4 EmissionColor;      // rgb = emission color
};

layout( std430, binding = 2 ) readonly buffer Materials
{
	GpuMaterial materials[];
};

struct DirectionLight
{
	vec4 Direction;      // xyz = normalized direction
	vec4 ColorIntensity; // rgb = color, a = intensity
};

layout(binding = 3) uniform DirectionLightsUB {
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
		vec3 Li = -directionLights.directionLights.Direction.xyz;
		vec3 Lradiance = directionLights.directionLights.ColorIntensity.rgb
		               * directionLights.directionLights.ColorIntensity.a;
		vec3 Lh = normalize(Li + view);

		float cosLi = max(0.0, dot(N, Li));
		float cosLh = max(0.0, dot(N, Lh));
		float cosLo = max(0.0, dot(N, view));

		vec3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, view)));
		// Calculate normal distribution for specular BRDF.
		float D = DistributionGGX(cosLh, roughness);
		// Visibility = G/(4*NdotL*NdotV) computed analytically to avoid 0/0 at grazing angles.
		float Vis = VisibilitySmith(cosLi, cosLo, roughness);

		vec3 kd = (1.0 - F) * (1.0 - metalness);
		vec3 diffuseBRDF = kd * albedo;

		vec3 specularBRDF = F * D * Vis;

		color += (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
	}

	return color;
}

vec3 IBL(vec3 view, vec3 N, vec3 F0, float metalness, float roughness, vec3 albedo)
{
	// Sample diffuse irradiance at normal direction.
	vec3 irradiance = texture(u_EnvIrradianceTex, N).rgb;

	float cosLo = max(0.0, dot(N, view));

	// reflect(-view, N) = 2*dot(N,view)*N - view (unclamped, correct specular reflection)
	vec3 Lr = reflect(-view, N);

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

	GpuMaterial mat = materials[pc.MaterialIndex];

	m_Params.AlbedoColor = mat.AlbedoAO.rgb;
	m_Params.AlbedoColor *= texture(u_AlbedoTexture, inVertex.Texcoord).rgb;

	// Default: use the world-space normal from the vertex shader directly.
	m_Params.Normal = normalize(inVertex.Normal);

	const ivec2 textureSize = textureSize(u_NormalTexture, 0);
	if(textureSize.x > 1 && textureSize.y > 1) // real normal map — not the 1x1 fallback
	{
		// Transform tangent-space normal to world space via TBN.
		vec3 tangentNormal = normalize(2.0 * texture(u_NormalTexture, inVertex.Texcoord).rgb - 1.0);
		m_Params.Normal = normalize(inVertex.TBN * tangentNormal);
	}
	// Without a normal map the TBN transform is intentionally skipped:
	// inVertex.Normal is already in world space and needs no further transformation.

	const float metalness = mat.MetalRoughEmission.x;
	// Clamp to a minimum roughness so the GGX NDF stays finite even for mirror-smooth materials.
	const float roughness = max(mat.MetalRoughEmission.y, 0.04);
	const float ao        = mat.AlbedoAO.a;

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

    // Ambient occlusion attenuates only the ambient (IBL) term; emission is added unlit.
    vec3 emission = mat.EmissionColor.rgb * mat.MetalRoughEmission.z;

    oColor = vec4( light + ibl * ao + pointLight + emission, 1.0);
}