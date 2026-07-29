Shader "StaticMeshGlass"
{
    // Forward transparent (glass) pass for static meshes: shares Static.glsl.vert + the Materials[] SSBO with
    // StaticMeshPBR so material data binds unchanged, but shades glass (Fresnel edge + specular + transmission)
    // and blends over the composited scene. Selected for materials with Transmission > 0.

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
        // Forward TRANSPARENT (glass) fragment. Drawn over the composited opaque scene with alpha blending. v1:
        // clear glass — Fresnel-bright reflective edges + a sun specular highlight + see-through centre (alpha from
        // transmission). Background TINT + screen-space REFRACTION are v2 (need the scene colour bound as a texture).
        //
        // CRITICAL: declares the SAME descriptor bindings as PBR.glsl.frag (shared StaticMaterialPBR set, 17
        // descriptors) and references each via a ~0 epsilon so SPIR-V reflection keeps the identical layout.

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

        Out(0) vec4 oColor;

        PushConstant PushConstants { mat4 Transform; uint MaterialIndex; } pc;

        struct GpuMaterial
        {
        	vec4 AlbedoAO;
        	vec4 MetalRoughEmission;
        	vec4 EmissionColor;
        	vec4 ExtraParams;  // z = IOR
        	vec4 GlassTint;    // rgb = tint, a = transmission
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
        Uniform(19) sampler2D  u_SceneColor; // copy of the composited opaque scene (for refraction)

        void main()
        {
        	GpuMaterial mat = materials[pc.MaterialIndex];

        	vec3  tint         = mat.GlassTint.rgb;
        	float transmission = clamp(mat.GlassTint.a, 0.0, 1.0);
        	float ior          = max(mat.ExtraParams.z, 1.0);

        	vec3 N = normalize(inVertex.Normal);
        	const ivec2 nrmSize = textureSize(u_NormalTexture, 0);
        	if (nrmSize.x > 1 && nrmSize.y > 1)
        		N = normalize(inVertex.TBN * normalize(2.0 * texture(u_NormalTexture, inVertex.Texcoord).rgb - 1.0));

        	vec3 V = normalize(inVertex.CameraPosition - inVertex.WorldPosition);

        	// Schlick Fresnel with the IOR-derived normal reflectance (glass F0 ~0.04, water ~0.02).
        	float f0        = pow((ior - 1.0) / (ior + 1.0), 2.0);
        	float fresnel   = f0 + (1.0 - f0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);

        	// Sun specular highlight (glass is smooth -> a tight hotspot).
        	vec3  L        = normalize(-directionLights.directionLights.Direction.xyz);
        	vec3  H        = normalize(V + L);
        	float spec     = pow(max(dot(N, H), 0.0), 128.0) * directionLights.directionLights.ColorIntensity.a;

        	// --- keep every forward binding statically referenced so reflection retains the identical layout ---
        	float keep = 0.0;
        	keep += texture(u_ShadowMap0, inVertex.Texcoord).r + texture(u_ShadowMap1, inVertex.Texcoord).r
        	      + texture(u_ShadowMap2, inVertex.Texcoord).r + texture(u_ShadowMap3, inVertex.Texcoord).r;
        	keep += texture(u_EnvSpecularTex, N).r + texture(u_EnvIrradianceTex, N).r + texture(u_BRDFLUTTexture, inVertex.Texcoord).r;
        	keep += texture(u_AlbedoTexture, inVertex.Texcoord).r + texture(u_OpacityTexture, inVertex.Texcoord).r;
        	keep += u_ShadowParams.x + u_LightViewProj[0][0][0] + mat.AlbedoAO.r + mat.MetalRoughEmission.r + mat.EmissionColor.r;
        	keep += float(lightsMetadata.DirectionLightCount);
        	if (lightsMetadata.PointLightCount > 0u) keep += pointLights[0].intensity;
        	if (lightsMetadata.SpotLightCount  > 0u) keep += spotLights[0].intensity;
        	keep *= 1e-20;

        	// --- Screen-space REFRACTION: sample the composited scene behind the glass, bent by the surface normal
        	// (so objects behind the sphere show through, distorted by IOR) + tinted. ---
        	vec2 screenUV   = gl_FragCoord.xy / vec2(textureSize(u_SceneColor, 0));
        	float bendScale = (1.0 - 1.0 / ior) * 0.35;                 // stronger IOR -> more bend
        	vec2  refractUV = clamp(screenUV + N.xy * bendScale, vec2(0.001), vec2(0.999));
        	vec3  refracted = texture(u_SceneColor, refractUV).rgb * tint;

        	// Environment reflection at grazing edges (skybox); Fresnel mixes refraction (centre) -> reflection (edge).
        	vec3 R       = reflect(-V, N);
        	vec3 envRefl = texture(u_EnvSpecularTex, R).rgb;

        	vec3 color = mix(refracted, envRefl, fresnel) + vec3(spec);

        	// Written opaque (the refraction already carries the background); the transmission factor only trims the
        	// reflection strength for very clear glass.
        	oColor = vec4(color + keep, 1.0);
        }
    }
}
