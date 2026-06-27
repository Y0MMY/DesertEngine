#pragma program Unlit

#pragma use_stage vertex   "Unlit.glsl.vert"
#pragma use_stage fragment "Unlit.glsl.frag"

// Fully data-driven surface shader (no C++ material class). Its one parameter is a real UB field, so the
// generic DataDrivenMaterial can set it by name from the #pragma param schema.
#pragma domain surface
#pragma state cull back
#pragma state depth lessorequal write on

#pragma param color Color "Color" default(0.8,0.4,0.1,1)
#pragma param texture2D u_AlbedoTex "Albedo"
