#pragma program Terrain

#pragma use_stage vertex "Terrain.glsl.vert"
#pragma use_stage tess_control "Terrain.glsl.tesc"
#pragma use_stage tess_evaluation "Terrain.glsl.tese"
#pragma use_stage fragment "Terrain.glsl.frag"

// Data-driven material metadata (consumed by the pipeline cache + generic material in later phases).
#pragma domain terrain
#pragma state topology patches 4
#pragma state cull none
#pragma state depth less write on

#pragma param color Tint "Tint" default(1,1,1,1)
#pragma param float DetailTiling "Texture Tiling (m)" range(0.25,64) default(4)

// Splat layers — drag textures onto these in Details; unassigned = white fallback (shows the base tint).
#pragma param texture2D u_GrassTex "Grass Texture"
#pragma param texture2D u_RockTex "Rock Texture"
#pragma param texture2D u_SnowTex "Snow Texture"
