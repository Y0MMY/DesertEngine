#version 450 core

// Deferred G-buffer WRITE pass. It shades NOTHING — it writes the material attributes into two MRT targets:
//   GBufferA (RGBA8F)  = Albedo.rgb + Metallic.a
//   GBufferB (RGBA32F) = world Normal.rgb + Roughness.a
//
// CRITICAL: this fragment declares the EXACT SAME descriptor bindings as the forward PBR.glsl.frag, because
// it is drawn with the SHARED StaticMaterialPBR descriptor set (17 descriptors). If it declared fewer, the
// pipeline layout wouldn't match the bound set (VUID-vkCmdDrawIndexed-None-02697 -> device lost). SPIR-V
// reflection drops UNUSED bindings, so every binding is referenced through a ~0 epsilon at the end of main().

#include "../../Mesh/PointLight.glslh"      // binding 6  (SSBO pointLights[])
#include "../../Mesh/Spotlight.glslh"       // binding 16 (SSBO spotLights[])
#include "../../Mesh/LightsMetadata.glslh"  // binding 4  (UB lightsMetadata)

layout(location=0) in Vertex
{
	vec3 WorldPosition;
	vec3 Normal;
	vec2 Texcoord;
	mat3 TBN;
	vec3 CameraPosition;
} inVertex;

layout(location = 0) out vec4 oGBufferA; // Albedo.rgb, Metallic.a
layout(location = 1) out vec4 oGBufferB; // Normal.rgb, Roughness.a
layout(location = 2) out vec4 oGBufferC; // WorldPosition.xyz (w unused)

layout( push_constant ) uniform PushConstants
{
	mat4 Transform;
	uint MaterialIndex;
} pc;

struct GpuMaterial
{
	vec4 AlbedoAO;
	vec4 MetalRoughEmission;
	vec4 EmissionColor;
	vec4 ExtraParams;
	vec4 GlassTint; // rgb = tint, a = transmission (G-buffer path is opaque; used to SKIP glass, not shade it)
};
layout( std430, binding = 2 ) readonly buffer Materials { GpuMaterial materials[]; };

struct DirectionLight { vec4 Direction; vec4 ColorIntensity; };
layout(binding = 3) uniform DirectionLightsUB { DirectionLight directionLights; } directionLights;

layout(binding = 5)  uniform sampler2D u_ShadowMap0;
layout(binding = 13) uniform sampler2D u_ShadowMap1;
layout(binding = 14) uniform sampler2D u_ShadowMap2;
layout(binding = 15) uniform sampler2D u_ShadowMap3;
layout(binding = 7) uniform ShadowUB
{
	mat4 u_LightViewProj[4];
	vec4 u_ShadowParams;
	vec4 u_DebugParams;
	vec4 u_CascadeTexelWorld;
};
layout(binding = 8)  uniform samplerCube u_EnvSpecularTex;
layout(binding = 9)  uniform samplerCube u_EnvIrradianceTex;
layout(binding = 10) uniform sampler2D  u_BRDFLUTTexture;
layout(binding = 11) uniform sampler2D  u_AlbedoTexture;
layout(binding = 12) uniform sampler2D  u_NormalTexture;
layout(binding = 18) uniform sampler2D  u_OpacityTexture;

void main()
{
	GpuMaterial mat = materials[pc.MaterialIndex];

	vec2 tiling = mat.ExtraParams.xy;
	if (tiling.x <= 0.0) tiling.x = 1.0;
	if (tiling.y <= 0.0) tiling.y = 1.0;
	vec2 uv = inVertex.Texcoord * tiling;

	float alphaCutoff = mat.MetalRoughEmission.w;
	if (alphaCutoff > 0.0 && texture(u_OpacityTexture, uv).r < alphaCutoff)
		discard;

	vec3 albedo = mat.AlbedoAO.rgb * pow(texture(u_AlbedoTexture, uv).rgb, vec3(2.2));

	vec3 N = normalize(inVertex.Normal);
	const ivec2 nrmSize = textureSize(u_NormalTexture, 0);
	if (nrmSize.x > 1 && nrmSize.y > 1)
	{
		vec3 tangentNormal = normalize(2.0 * texture(u_NormalTexture, uv).rgb - 1.0);
		N = normalize(inVertex.TBN * tangentNormal);
	}

	const float metallic  = mat.MetalRoughEmission.x;
	const float roughness = max(mat.MetalRoughEmission.y, 0.04);

	// --- keep every forward binding statically referenced so reflection retains the identical layout ---
	float keep = 0.0;
	keep += texture(u_ShadowMap0, uv).r + texture(u_ShadowMap1, uv).r
	      + texture(u_ShadowMap2, uv).r + texture(u_ShadowMap3, uv).r;
	keep += texture(u_EnvSpecularTex, N).r + texture(u_EnvIrradianceTex, N).r + texture(u_BRDFLUTTexture, uv).r;
	keep += directionLights.directionLights.ColorIntensity.a + u_ShadowParams.x + u_LightViewProj[0][0][0];
	keep += float(lightsMetadata.DirectionLightCount);
	if (lightsMetadata.PointLightCount > 0u) keep += pointLights[0].intensity; // binding 6
	if (lightsMetadata.SpotLightCount  > 0u) keep += spotLights[0].intensity;  // binding 16
	keep *= 1e-20;

	oGBufferA = vec4(albedo + keep, metallic);
	oGBufferB = vec4(N, roughness);
	oGBufferC = vec4(inVertex.WorldPosition, 1.0);
}
