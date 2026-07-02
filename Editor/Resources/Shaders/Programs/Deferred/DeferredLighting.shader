#pragma program DeferredLighting

#pragma use_stage vertex   "DeferredLighting.glsl.vert"
#pragma use_stage fragment "DeferredLighting.glsl.frag"

// Deferred lighting + G-buffer debug visualization (fullscreen). Consumes the scene renderer's MRT G-buffer.
