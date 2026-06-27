#pragma program SkinnedMeshPBR

#pragma use_stage vertex   "Skinned.glsl.vert"
#pragma use_stage fragment "PBR.glsl.frag"

// Specialized C++ material (see StaticMeshPBR.shader) — not assignable via MaterialComponent.