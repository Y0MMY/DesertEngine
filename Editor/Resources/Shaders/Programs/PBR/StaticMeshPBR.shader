#pragma program StaticMeshPBR

#pragma use_stage vertex   "Static.glsl.vert"
#pragma use_stage fragment "PBR.glsl.frag"

// The standard surface shader. Surface domain like any other DSL shader — it appears in the
// material shader picker naturally (no hardcoded editor entry); the ONLY special thing about it
// is the optimized backend (StaticMaterialPBR: SSBO batching) the renderer picks by name.
#pragma domain surface
//
// v4 material protocol: the schema below is the SINGLE source of truth for what a PBR material
// stores. Every .demat persists these as generic ShaderParams/ShaderTextures (same protocol as
// custom DSL shaders); the legacy reflected fields are kept in sync as a compatibility mirror
// for the optimized backend + older editor builds.

#pragma param color AlbedoColor       "Albedo"             default(1,1,1,1)          category("Surface")
#pragma param float MetallicFactor    "Metallic"           range(0,1)   default(0)   category("Surface")
#pragma param float RoughnessFactor   "Roughness"          range(0,1)   default(0.5) category("Surface")
#pragma param float AOStrength        "Ambient Occlusion"  range(0,1)   default(1)   category("Surface")
#pragma param color EmissiveColor     "Emissive"           default(0,0,0,1)          category("Surface")
#pragma param float EmissiveIntensity "Emissive Intensity" range(0,100) default(1)   category("Surface")
#pragma param float AlphaCutoff       "Alpha Cutoff"       range(0,1)   default(0)   category("Surface")
#pragma param float Transmission      "Transmission"       range(0,1)   default(0)   category("Glass")
#pragma param float IOR               "IOR"                range(1,2.5) default(1.5) category("Glass")
#pragma param color GlassTint         "Glass Tint"         default(1,1,1,1)          category("Glass")
#pragma param vec2  UVTiling          "UV Tiling"          default(1,1)              category("Surface")

#pragma param texture2D u_AlbedoTexture    "Albedo Map"     category("Textures")
#pragma param texture2D u_NormalTexture    "Normal Map"     category("Textures")
#pragma param texture2D u_OpacityTexture   "Opacity Map"    category("Textures")
#pragma param texture2D u_MetallicTexture  "Metallic Map"   category("Textures")
#pragma param texture2D u_RoughnessTexture "Roughness Map"  category("Textures")
#pragma param texture2D u_AOTexture        "AO Map"         category("Textures")
#pragma param texture2D u_EmissiveTexture  "Emissive Map"   category("Textures")
