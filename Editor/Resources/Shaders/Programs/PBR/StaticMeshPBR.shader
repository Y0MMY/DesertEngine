#pragma program StaticMeshPBR

#pragma use_stage vertex   "Static.glsl.vert"
#pragma use_stage fragment "PBR.glsl.frag"

// PBR is a SPECIALIZED C++ material (per-submesh material slots, texture slots, SSBO batching, skinning).
// It is NOT data-driven and is intentionally NOT assignable via MaterialComponent — no #pragma domain/param.
