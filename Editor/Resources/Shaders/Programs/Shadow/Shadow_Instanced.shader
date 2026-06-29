#pragma program Shadow_Instanced

#pragma use_stage fragment "Shadow.glsl.frag"
#pragma use_stage vertex "Shadow_Instanced.glsl.vert"

// Instanced variant of the depth-only shadow caster: identical fragment, but the vertex reads each caster's
// model matrix from the InstanceTransforms SSBO (binding 16) by gl_InstanceIndex. Batched shadow casters of
// the same mesh collapse to one instanced draw per cascade (the dominant cost in the 256-mesh stress test).
