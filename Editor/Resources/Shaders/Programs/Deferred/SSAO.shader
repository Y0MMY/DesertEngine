#pragma program SSAO

#pragma use_stage vertex   "SSAO.glsl.vert"
#pragma use_stage fragment "SSAO.glsl.frag"

// Screen-space ambient occlusion (fullscreen). Reads the G-buffer world position + normal, writes a
// single-channel AO factor the deferred lighting pass multiplies into the ambient term.
