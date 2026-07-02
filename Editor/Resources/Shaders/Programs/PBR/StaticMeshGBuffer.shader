#pragma program StaticMeshGBuffer

#pragma use_stage vertex   "Static.glsl.vert"
#pragma use_stage fragment "PBR_GBuffer.glsl.frag"

// Deferred G-buffer geometry pass for static meshes (writes Albedo+Metallic / Normal+Roughness MRT).
// Shares Static.glsl.vert + the Materials[] SSBO with StaticMeshPBR so material data binds unchanged.
