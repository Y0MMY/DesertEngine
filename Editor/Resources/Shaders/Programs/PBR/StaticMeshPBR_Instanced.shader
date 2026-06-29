#pragma program StaticMeshPBR_Instanced

#pragma use_stage vertex   "Static_Instanced.glsl.vert"
#pragma use_stage fragment "PBR.glsl.frag"

// Instanced variant of StaticMeshPBR: same fragment (PBR.glsl.frag), but the vertex reads the per-instance
// model matrix from an InstanceTransforms storage buffer (binding 16) by gl_InstanceIndex. Drawn as a
// single instanced draw call (RenderMeshInstanced). Like StaticMeshPBR it is a specialized C++ material,
// not data-driven (no #pragma domain/param).
