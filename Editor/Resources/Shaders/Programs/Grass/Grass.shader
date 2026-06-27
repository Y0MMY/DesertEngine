#pragma program Grass

#pragma use_stage vertex "Grass.glsl.vert"
#pragma use_stage fragment "Grass.glsl.frag"

// GPU-driven instanced grass — triangle-list blades. Double-sided (cull none) so blades are visible from
// both faces; depth-tested + depth-write so they sort against the terrain and each other.
#pragma domain terrain
#pragma state topology triangles
#pragma state cull none
#pragma state depth less write on
