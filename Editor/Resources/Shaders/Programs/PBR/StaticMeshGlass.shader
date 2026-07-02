#pragma program StaticMeshGlass

#pragma use_stage vertex   "Static.glsl.vert"
#pragma use_stage fragment "Glass.glsl.frag"

// Forward transparent (glass) pass for static meshes: shares Static.glsl.vert + the Materials[] SSBO with
// StaticMeshPBR so material data binds unchanged, but shades glass (Fresnel edge + specular + transmission)
// and blends over the composited scene. Selected for materials with Transmission > 0.
