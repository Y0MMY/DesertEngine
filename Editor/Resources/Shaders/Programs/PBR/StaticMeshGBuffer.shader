Shader "StaticMeshGBuffer"
{
    // Deferred G-buffer geometry pass for static meshes (writes Albedo+Metallic / Normal+Roughness MRT).
    // Shares Static.glsl.vert + the Materials[] SSBO with StaticMeshPBR so material data binds unchanged.

    Vertex
    {
        In(0) vec3 a_Position;
        In(1) vec3 a_Normal;
        In(2) vec3 a_Tangent;
        In(3) vec3 a_Bitangent;
        In(4) vec2 a_TextureCoord;

        #include <Common/CameraUB.glslh>

        // Shared push-constant block. Must be byte-for-byte identical to the one in PBR.glsl.frag so the
        // reflected range (offset/size) matches across stages. The vertex stage only reads Transform; the
        // per-object material parameters are consumed by the fragment stage. Per-object data lives here
        // (not in a uniform buffer) so each draw carries its own values — a shared material UB would be
        // overwritten by later objects in the same frame (last-write-wins) before the GPU executes the draws.
        // Must match PBR.glsl.frag / Skinned.glsl.vert. Material data lives in a storage buffer (read in the
        // fragment); the vertex stage only needs Transform.
        PushConstant PushConstants
        {
        	mat4 Transform;     // offset 0
        	uint MaterialIndex; // offset 64
        } m_PushConstants;


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
        	outVertex.WorldPosition  = vec3(m_PushConstants.Transform * vec4(a_Position, 1.0));
        	outVertex.Texcoord       = vec2(a_TextureCoord.x, 1.0 - a_TextureCoord.y);
        	outVertex.CameraPosition = cameraUB.CameraPos;

        	mat3 normalMatrix = transpose(inverse(mat3(m_PushConstants.Transform)));

        	vec3 T = normalize(normalMatrix * a_Tangent);
        	vec3 B = normalize(normalMatrix * a_Bitangent);
        	vec3 N = normalize(normalMatrix * a_Normal);

        	outVertex.Normal = N;
        	outVertex.TBN    = mat3(T, B, N);

        	gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4(a_Position, 1.0);
        }
    }

    Fragment
    {
        // Deferred G-buffer WRITE pass. It shades NOTHING — it writes the material attributes into two MRT targets:
        //   GBufferA (RGBA8F)  = Albedo.rgb + Metallic.a
        //   GBufferB (RGBA32F) = world Normal.rgb + Roughness.a
        //
        // CRITICAL: this fragment declares the EXACT SAME descriptor bindings as the forward PBR.glsl.frag, because
        // it is drawn with the SHARED StaticMaterialPBR descriptor set (17 descriptors). If it declared fewer, the
        // pipeline layout wouldn't match the bound set (VUID-vkCmdDrawIndexed-None-02697 -> device lost). SPIR-V
        // reflection drops UNUSED bindings, so every binding is referenced through a ~0 epsilon at the end of main().

        #include <Mesh/PointLight.glslh>      // binding 6  (SSBO pointLights[])
        #include <Mesh/Spotlight.glslh>       // binding 16 (SSBO spotLights[])
        #include <Mesh/LightsMetadata.glslh>  // binding 4  (UB lightsMetadata)

        In(0) Vertex
        {
        	vec3 WorldPosition;
        	vec3 Normal;
        	vec2 Texcoord;
        	mat3 TBN;
        	vec3 CameraPosition;
        } inVertex;

        Out(0) vec4 oGBufferA; // Albedo.rgb, Metallic.a
        Out(1) vec4 oGBufferB; // Normal.rgb, Roughness.a
        Out(2) vec4 oGBufferC; // WorldPosition.xyz (w unused)

        PushConstant PushConstants
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
        ReadBuffer(2) Materials { GpuMaterial materials[]; };

        struct DirectionLight { vec4 Direction; vec4 ColorIntensity; };
        Uniform(3) DirectionLightsUB { DirectionLight directionLights; } directionLights;

        Uniform(5) sampler2D u_ShadowMap0;
        Uniform(13) sampler2D u_ShadowMap1;
        Uniform(14) sampler2D u_ShadowMap2;
        Uniform(15) sampler2D u_ShadowMap3;
        Uniform(7) ShadowUB
        {
        	mat4 u_LightViewProj[4];
        	vec4 u_ShadowParams;
        	vec4 u_DebugParams;
        	vec4 u_CascadeTexelWorld;
        };
        Uniform(8) samplerCube u_EnvSpecularTex;
        Uniform(9) samplerCube u_EnvIrradianceTex;
        Uniform(10) sampler2D  u_BRDFLUTTexture;
        Uniform(11) sampler2D  u_AlbedoTexture;
        Uniform(12) sampler2D  u_NormalTexture;
        Uniform(18) sampler2D  u_OpacityTexture;

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
    }
}
