Shader "SkinnedMeshPBR"
{
    // Specialized C++ material (see StaticMeshPBR.shader) — not assignable via MaterialComponent.

    Vertex
    {
        In(0) vec3 a_Position;
        In(1) vec3 a_Normal;
        In(2) vec3 a_Tangent;
        In(3) vec3 a_Bitangent;
        In(4) vec2 a_TextureCoord;
        In(5) ivec4 a_BoneIndices;
        In(6) vec4  a_BoneWeights;

        #include <Common/CameraUB.glslh>

        // Must match PBR.glsl.frag / Static.glsl.vert push block.
        PushConstant PushConstants
        {
            mat4 Transform;     // offset 0
            uint MaterialIndex; // offset 64
        } m_PushConstants;

        // raw-glsl: implicit (shared) layout kept — std430 would change the bone matrix offsets.
        layout(binding = 1) readonly buffer Bones
        {
            mat4 BoneMatrices[];
        } bones;

        Out(0) Vertex
        {
            vec3 WorldPosition;
            vec3 Normal;
            vec2 Texcoord;
            mat3 TBN;
            vec3 CameraPosition;
        } outVertex;

        void main()
        {
            // ------------------------------------------------------------
            // 1. GPU Skinning
            // ------------------------------------------------------------
            mat4 skinMatrix =
                  bones.BoneMatrices[a_BoneIndices.x] * a_BoneWeights.x +
                  bones.BoneMatrices[a_BoneIndices.y] * a_BoneWeights.y +
                  bones.BoneMatrices[a_BoneIndices.z] * a_BoneWeights.z +
                  bones.BoneMatrices[a_BoneIndices.w] * a_BoneWeights.w;

            vec4 skinnedPosition = skinMatrix * vec4(a_Position, 1.0);
            vec3 skinnedNormal   = mat3(skinMatrix) * a_Normal;
            vec3 skinnedTangent  = mat3(skinMatrix) * a_Tangent;
            vec3 skinnedBitangent= mat3(skinMatrix) * a_Bitangent;

            // ------------------------------------------------------------
            // 2. World space
            // ------------------------------------------------------------
            mat4 model = m_PushConstants.Transform;
            mat3 normalMatrix = transpose(inverse(mat3(model)));

            vec4 worldPos = model * skinnedPosition;

            outVertex.WorldPosition = worldPos.xyz;
            outVertex.Normal        = normalize(normalMatrix * skinnedNormal);
            outVertex.Texcoord      = vec2(a_TextureCoord.x, 1.0 - a_TextureCoord.y);

            vec3 T = normalize(normalMatrix * skinnedTangent);
            vec3 B = normalize(normalMatrix * skinnedBitangent);
            vec3 N = normalize(normalMatrix * skinnedNormal);

            outVertex.TBN = mat3(T, B, N);
            outVertex.CameraPosition = cameraUB.CameraPos;

            // ------------------------------------------------------------
            // 3. Clip space
            // ------------------------------------------------------------
            gl_Position = cameraUB.Projection * cameraUB.View * worldPos;
        }
    }

    Fragment
    {
        #include <Mesh/PointLight.glslh>
        #include <Mesh/Spotlight.glslh>
        #include <Mesh/LightsMetadata.glslh>

        In(0) Vertex
        {
        	vec3 WorldPosition;
        	vec3 Normal;
        	vec2 Texcoord;
        	mat3 TBN;
        	vec3 CameraPosition;
        } inVertex;

        const float Epsilon = 0.00001;

        const vec3 Fdielectric = vec3(0.04);


        Out(0) vec4 oColor;

        // Shared push-constant block. Must be byte-for-byte identical to the one in Static.glsl.vert /
        // Skinned.glsl.vert. Per-object material data lives in the Materials[] storage buffer (GPU-scene
        // style); the push constant only carries the per-object index into it.
        PushConstant PushConstants
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
        	vec4 ExtraParams;        // xy = UV tiling, z = IOR, w = reserved
        	vec4 GlassTint;          // rgb = glass tint, a = transmission (opaque path ignores it)
        };

        ReadBuffer(2) Materials
        {
        	GpuMaterial materials[];
        };

        struct DirectionLight
        {
        	vec4 Direction;      // xyz = normalized direction
        	vec4 ColorIntensity; // rgb = color, a = intensity
        };

        Uniform(3) DirectionLightsUB {
        	DirectionLight 		directionLights;
        } directionLights;

        // Cascaded directional shadow maps (R32F light-space depth, one per cascade) + per-cascade light VP.
        Uniform(5) sampler2D u_ShadowMap0;
        Uniform(13) sampler2D u_ShadowMap1;
        Uniform(14) sampler2D u_ShadowMap2;
        Uniform(15) sampler2D u_ShadowMap3;
        Uniform(7) ShadowUB {
        	mat4 u_LightViewProj[4];
        	vec4 u_ShadowParams;      // x = bias, y = enabled (>0.5), z = debug mode (0/1/2), w = cascade count
        	vec4 u_DebugParams;       // x = show normals (>0.5); y,z,w reserved
        	vec4 u_CascadeTexelWorld; // per-cascade world size of one shadow-map texel (x..w = cascade 0..3)
        };

        // Runtime cascade index can't index a sampler array here, so branch over the 4 named maps.
        float sampleShadowMap(int c, vec2 uv)
        {
        	if (c == 0) return texture(u_ShadowMap0, uv).r;
        	if (c == 1) return texture(u_ShadowMap1, uv).r;
        	if (c == 2) return texture(u_ShadowMap2, uv).r;
        	return texture(u_ShadowMap3, uv).r;
        }
        ivec2 shadowMapSize(int c)
        {
        	if (c == 0) return textureSize(u_ShadowMap0, 0);
        	if (c == 1) return textureSize(u_ShadowMap1, 0);
        	if (c == 2) return textureSize(u_ShadowMap2, 0);
        	return textureSize(u_ShadowMap3, 0);
        }
        vec3 cascadeDebugColor(int c)
        {
        	if (c == 0) return vec3(1.0, 0.35, 0.35);
        	if (c == 1) return vec3(0.35, 1.0, 0.35);
        	if (c == 2) return vec3(0.35, 0.55, 1.0);
        	if (c == 3) return vec3(1.0, 0.95, 0.35);
        	return vec3(0.4); // outside all cascades
        }

        // First cascade (tightest→loosest) whose light-space projection contains worldPos. -1 = outside all.
        int chooseCascade(vec3 worldPos)
        {
        	for (int c = 0; c < int(u_ShadowParams.w); ++c)
        	{
        		vec4 lc  = u_LightViewProj[c] * vec4(worldPos, 1.0);
        		vec3 ndc = lc.xyz / lc.w;
        		vec2 uv  = ndc.xy * 0.5 + 0.5;
        		if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 && ndc.z <= 1.0)
        			return c;
        	}
        	return -1;
        }

        // N = world-space surface normal, L = world-space direction TOWARD the sun (both normalized).
        // Picks the cascade covering worldPos and PCF-samples it. `cascadeOut` returns the chosen cascade (-1
        // if outside all / disabled) for the debug visualization.
        float ShadowFactor(vec3 worldPos, vec3 N, vec3 L, out int cascadeOut)
        {
        	cascadeOut = -1;
        	if (u_ShadowParams.y < 0.5)
        		return 1.0;

        	int c = chooseCascade(worldPos);
        	cascadeOut = c;
        	if (c < 0)
        		return 1.0;

        	float NdotL = max(dot(N, L), 0.0);

        	// Normal-offset bias scaled by the CHOSEN cascade's world-per-texel: a few texels along the normal,
        	// widening at grazing angles. Cascade-correct (tight near cascades get a small offset, far ones large)
        	// instead of the old fixed world-unit constants.
        	float texelWorld   = u_CascadeTexelWorld[c];
        	float normalOffset = (1.5 + 2.5 * (1.0 - NdotL)) * texelWorld;
        	vec3  samplePos    = worldPos + N * normalOffset;

        	vec4 lightClip = u_LightViewProj[c] * vec4(samplePos, 1.0);
        	vec3 ndc = lightClip.xyz / lightClip.w;
        	vec2 uv = ndc.xy * 0.5 + 0.5;
        	// Shadow maps are rendered through the engine's negative-height (Y-flipped) viewport -> flip Y.
        	uv.y = 1.0 - uv.y;

        	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0)
        		return 1.0;

        	float currentDepth = ndc.z;
        	float bias = clamp(u_ShadowParams.x * (1.0 - NdotL) * 4.0, u_ShadowParams.x, 0.02);

        	// 3x3 PCF for soft edges.
        	float shadow = 0.0;
        	vec2 texel = 1.0 / vec2(shadowMapSize(c));
        	for (int y = -1; y <= 1; ++y)
        	{
        		for (int x = -1; x <= 1; ++x)
        		{
        			float closest = sampleShadowMap(c, uv + vec2(x, y) * texel);
        			shadow += (currentDepth - bias > closest) ? 0.0 : 1.0;
        		}
        	}
        	return shadow / 9.0;
        }

        // Environment maps
        Uniform(8) samplerCube u_EnvSpecularTex;
        Uniform(9) samplerCube u_EnvIrradianceTex;

        // BRDF LUT
        Uniform(10) sampler2D u_BRDFLUTTexture;

        Uniform(11) sampler2D u_AlbedoTexture;
        Uniform(12) sampler2D u_NormalTexture;
        Uniform(18) sampler2D u_OpacityTexture; // alpha-cutout mask (foliage); unused when cutoff == 0 (16/17 = light SSBOs)

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

        // The split-sum ambient — the SAME text the deferred lighting pass compiles, so a scene shaded
        // through RenderingPath 0 and one shaded through RenderingPath 1 get one ambient model and not
        // two. Included HERE and not with the other headers at the top because it names the three
        // environment bindings declared just above.
        #include <Mesh/AmbientIBL.glslh>


        void main() {

        	GpuMaterial mat = materials[pc.MaterialIndex];

        	// Tiled UV: surface UVs * material UV-tiling (ExtraParams.xy; default {1,1} = no tiling). Guard against 0
        	// (un-set / legacy material) so the texture never collapses to a single texel.
        	vec2 tiling = mat.ExtraParams.xy;
        	if (tiling.x <= 0.0) tiling.x = 1.0;
        	if (tiling.y <= 0.0) tiling.y = 1.0;
        	vec2 uv = inVertex.Texcoord * tiling;

        	// Alpha cutout (foliage/cards): discard transparent texels per the Opacity Map. MetalRoughEmission.w is
        	// the cutoff (0 = disabled, so opaque materials are unaffected). Done first to skip lighting on discards.
        	float alphaCutoff = mat.MetalRoughEmission.w;
        	if (alphaCutoff > 0.0 && texture(u_OpacityTexture, uv).r < alphaCutoff)
        		discard;

        	m_Params.AlbedoColor = mat.AlbedoAO.rgb;
        	// Albedo maps are authored in sRGB (gamma) space; lighting must run in LINEAR space. The engine loads
        	// 8-bit textures as UNORM (no hardware sRGB sampling yet), so convert here. Normal/roughness/metallic/AO
        	// are DATA maps and are intentionally NOT converted. (Proper fix later: hardware VK_FORMAT_*_SRGB.)
        	m_Params.AlbedoColor *= pow( texture(u_AlbedoTexture, uv).rgb, vec3(2.2) );

        	// Default: use the world-space normal from the vertex shader directly.
        	m_Params.Normal = normalize(inVertex.Normal);

        	const ivec2 textureSize = textureSize(u_NormalTexture, 0);
        	if(textureSize.x > 1 && textureSize.y > 1) // real normal map — not the 1x1 fallback
        	{
        		// Transform tangent-space normal to world space via TBN.
        		vec3 tangentNormal = normalize(2.0 * texture(u_NormalTexture, uv).rgb - 1.0);
        		m_Params.Normal = normalize(inVertex.TBN * tangentNormal);
        	}
        	// Without a normal map the TBN transform is intentionally skipped:
        	// inVertex.Normal is already in world space and needs no further transformation.

        	// Debug: visualize the final world-space normal as RGB (Scene Settings -> Debug -> Show Normals).
        	if (u_DebugParams.x > 0.5)
        	{
        		oColor = vec4(m_Params.Normal * 0.5 + 0.5, 1.0);
        		return;
        	}

        	const float metalness = mat.MetalRoughEmission.x;
        	// Clamp to a minimum roughness so the GGX NDF stays finite even for mirror-smooth materials.
        	const float roughness = max(mat.MetalRoughEmission.y, 0.04);
        	const float ao        = mat.AlbedoAO.a;

        	const vec3 view = normalize(inVertex.CameraPosition - inVertex.WorldPosition);

        	vec3 F0 = mix(Fdielectric, m_Params.AlbedoColor, metalness);
        	vec3 light = Lightning(view, m_Params.Normal, F0, metalness, roughness, m_Params.AlbedoColor );
        	vec3 ibl = AmbientIBL(view, m_Params.Normal, F0, metalness, roughness, m_Params.AlbedoColor);

        	vec3 pointLight = vec3(0.0);

        	for(uint i = 0; i < lightsMetadata.PointLightCount; i++)
        	{
        		PointLight light = pointLights[i];
                pointLight += CalculatePointLight(light, inVertex.WorldPosition, view,
                                                m_Params.Normal, F0, metalness,
                                                roughness, m_Params.AlbedoColor);
        	}

        	vec3 spotLight = vec3(0.0);
        	for(uint i = 0; i < lightsMetadata.SpotLightCount; i++)
        	{
                spotLight += CalculateSpotLight(spotLights[i], inVertex.WorldPosition, view,
                                                m_Params.Normal, F0, metalness,
                                                roughness, m_Params.AlbedoColor);
        	}

            // Directional (sun) light is occluded by the shadow map; IBL/point/emission are not.
            vec3  sunDir = normalize(-directionLights.directionLights.Direction.xyz); // toward the sun
            int   cascade;
            float shadow = ShadowFactor(inVertex.WorldPosition, m_Params.Normal, sunDir, cascade);

            // Lighting debug (Scene Settings -> Debug -> Light Debug): each source gets a distinct color, the
            // surface is tinted by the sources reaching it (weighted by attenuation * NdotL), brightness = light
            // strength, fully-unlit areas read black. Albedo/IBL/emission are ignored.
            if (u_DebugParams.y > 0.5)
            {
                vec3 dbg = vec3(0.0);
                // Point lights: hue cycles 0,1,2,... (red, then well-spread).
                for (uint i = 0; i < lightsMetadata.PointLightCount; i++)
                    dbg += LightDebugColor(i) * PointLightContribution(pointLights[i], inVertex.WorldPosition, m_Params.Normal);
                // Spot lights: same hue cycle but offset half a turn so spot #0 != point #0.
                for (uint i = 0; i < lightsMetadata.SpotLightCount; i++)
                    dbg += HueToRGB(fract(float(i) * 0.61803398875 + 0.5)) * SpotLightContribution(spotLights[i], inVertex.WorldPosition, m_Params.Normal);
                // Sun (directional): fixed warm yellow, occluded by its shadow — unmistakable vs the cycled hues.
                for (uint i = 0; i < lightsMetadata.DirectionLightCount; i++)
                {
                    float sunStrength = directionLights.directionLights.ColorIntensity.a
                                      * max(dot(m_Params.Normal, sunDir), 0.0) * shadow;
                    dbg += vec3(1.0, 0.85, 0.35) * sunStrength;
                }
                // Physical attenuation*NdotL is numerically dim, so a debug viz built from it reads as near-black.
                // Map it through an exposure-like curve so ANY light reaching the surface shows as a clear color,
                // while a truly unlit fragment (dbg == 0) stays black. This is a visualization, not radiometry.
                dbg = vec3(1.0) - exp(-dbg * 6.0);
                oColor = vec4(dbg, 1.0);
                return;
            }

            // Debug mode 2 (Cascades): tint by the cascade that shadows this fragment, darkened where shadowed.
            if (u_ShadowParams.z > 1.5)
            {
                oColor = vec4(cascadeDebugColor(cascade) * (shadow * 0.7 + 0.3), 1.0);
                return;
            }
            // Debug mode 1 (ShadowFactor): raw shadow factor as grayscale (1 = lit, 0 = shadowed).
            if (u_ShadowParams.z > 0.5)
            {
                oColor = vec4(vec3(shadow), 1.0);
                return;
            }

            // Ambient occlusion attenuates only the ambient (IBL) term; emission is added unlit.
            vec3 emission = mat.EmissionColor.rgb * mat.MetalRoughEmission.z;

            // The ambient, assembled by the shared header — the SAME call the deferred composite makes,
            // so the two paths cannot floor, occlude or albedo-weight it differently. The forward path
            // has no indirect-bounce gather, so it passes zero there; that is the only difference
            // between the two call sites and it is visible in the argument list.
            vec3 ambient = ComposeAmbient(ibl, m_Params.AlbedoColor, ao, vec3(0.0));

            oColor = vec4( light * shadow + ambient + pointLight + spotLight + emission, 1.0);
        }
    }
}
