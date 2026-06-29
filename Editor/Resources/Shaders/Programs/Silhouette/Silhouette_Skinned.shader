#pragma program Silhouette_Skinned

#pragma use_stage vertex "Silhouette_Skinned.glsl.vert"
#pragma use_stage fragment "Silhouette.glsl.frag"

// Skinned variant of the silhouette mask: same flat-white fragment, but the vertex skins by the Bones SSBO
// (binding 1) so a selected SKINNED mesh's Jump Flood outline matches its posed/animated shape.
