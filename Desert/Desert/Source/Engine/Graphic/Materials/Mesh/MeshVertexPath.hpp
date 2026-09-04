#pragma once

#include <cstdint>
#include <optional>

namespace Desert::Graphic
{
    // ── THE THREE AXES OF A MESH DRAW ───────────────────────────────────────────────────────────────
    //
    // A mesh draw is a PERMUTATION of three independent things, and the class hierarchy that stood here
    // was their cartesian product spelled out in C++ — `MaterialGlass` inherited from `StaticMaterialPBR`,
    // so a SHADING MODEL was a subclass of a (shading model x vertex path) pair.
    //
    //   1. the SHADING MODEL — PBR, glass, a node graph, unlit. What the fragment stage does with the
    //      surface. It belongs in a SHADER, named by the asset, with no privileged C++ type behind it.
    //   2. the VERTEX PATH — how the vertices are fetched and transformed before that surface is shaded.
    //      Static, skinned, hardware-instanced. A property of the GEOMETRY, chosen by the renderer, and
    //      the artist never names it. THIS ENUM.
    //   3. the SCENE-STATE BINDING — camera, lights, cascades, environment, cloud shadow. Legitimately
    //      C++, and ALREADY decoupled: `Graphic::PBRSceneFrame::ApplyTo` takes any `Material*` and writes
    //      by block NAME, so it reaches the PBR path, the skinned path, the graph path and unlit alike.
    //      It is a finished foundation and nothing here rebuilds it.
    //
    // Where each axis lives AFTER this change — the census, because the answer was smaller than the six
    // classes suggested:
    //
    //   | axis           | where it lives now                | how many C++ types |
    //   |----------------|-----------------------------------|--------------------|
    //   | shading model  | the shader NAME, from the `.demat`| 0 (it is data)     |
    //   | vertex path    | MeshVertexPath, a renderer argument| 0 (it is an enum) |
    //   | scene binding  | PBRSceneFrame + SceneLightingBinding| 1, already shared |
    //
    // `MaterialGlass` and `MaterialRSM` are gone entirely: they were a shader name and nothing else, so
    // they are `MaterialPBR::Create(Static, Glass)` and `(Static, GBuffer)`. And glass was never chosen
    // by a class in the first place — `MeshRenderer::DrawStaticMeshes` splits it out by the material's
    // own `Transmission` value, i.e. by DATA, which is what makes deleting the class safe.
    //
    // WHAT IS STILL A C++ SPLIT, AND WHY IT IS NOT THE SHADING-MODEL AXIS. `MaterialPBR` and
    // `DataDrivenMaterial` remain two types, and it is tempting to read that as "PBR is privileged". It is
    // not a shading-model distinction: both take a shader name and shade whatever it says. They differ in
    // PARAMETER TRANSPORT — MaterialPBR packs per-object parameters into a `Materials[]` storage buffer
    // indexed by a push constant, which is what lets identical meshes collapse into one instanced draw,
    // while DataDrivenMaterial writes per-material uniform blocks by reflection and draws one object at a
    // time. Collapsing THEM is a real piece of work with a real payoff and it is NOT done here; the report
    // sizes it.
    //
    // UE calls axis 2 a *vertex factory* and compiles (material x vertex factory x pass) into a shader
    // permutation at draw time; Unity's SRP reaches the same shape with per-pass shader variants and
    // `#pragma multi_compile` keywords; Godot's spatial shaders take a `skeleton` input the same way.
    // None of the three lets a material belong to a vertex path, and none of the three has a C++ class
    // per shading model.
    //
    // This engine had both. `StaticMaterialPBR`, `SkinnedMaterialPBR` and `StaticMaterialPBRInstanced`
    // were three C++ CLASSES for one surface model, one per path, and `MaterialService` resolved a
    // `.demat` into exactly one of them. The consequences were all one defect wearing different clothes:
    //
    //   1. an imported character with its own materials did not draw AT ALL — MeshRenderer looked for a
    //      slot whose parent was a `SkinnedMaterialPBR`, and MaterialFactory could not build one from an
    //      asset under any circumstances (it answered `StaticMaterialPBR` even for a `.demat` naming the
    //      skinned shader);
    //   2. a skinned mesh cast NO SHADOW — the cascade pass walked the static queue by name;
    //   3. a skinned mesh ignored per-instance material overrides — its Bind built the GPU material from
    //      the parent's data and never looked at the instance;
    //   4. two skinned meshes sharing one material rendered with ONE pose — the bone matrices lived on
    //      the material, so the last writer in the frame won for every draw recorded before it.
    //
    // The axis below is the fix: the surface stays in the asset, the path becomes an explicit enum the
    // renderer supplies, and a runtime material is identified by the PAIR. `MaterialService::Get` and
    // `CreateRuntimeInstance` therefore take a path, and one `.demat` legitimately yields one material
    // per path with the same parameters — it cannot drift, because there is one asset behind all of them.
    //
    // ── WHY THE DESCRIPTOR LAYOUT LIVES ON THE PAIR AND NOT ON THE MATERIAL ──────────────────────────
    //
    // A `Graphic::Material` in this engine is "one shader's descriptor sets plus a parameter payload"
    // (Material.cpp: the constructor takes a shader NAME and builds a MaterialExecutor from its
    // reflection). So the descriptor layout was never a property of the surface — it is a property of the
    // shader, and the shader is chosen by (path x pass). That is exactly what `MeshShaderFor` below says,
    // and it is why the strangest lines in the renderer exist: `StaticMeshGBuffer.shader` multiplies
    // three environment samples, four cascade maps and two light SSBOs by 1e-20 so that its reflected
    // layout stays identical to `StaticMeshPBR`'s, because MeshRenderer binds the (Static x Forward)
    // material's sets against the (Static x GBuffer) pipeline layout and Vulkan demands compatibility
    // (`Desert/Tests/Engine/ShaderCacheKey` asserts it). With the pair named, that is no longer a
    // mystery but a stated shortcut: the G-buffer pass borrows a neighbouring variant's sets instead of
    // owning its own. Giving it its own is a separate change and is NOT made here.
    //
    // ── THE RELATION THIS TABLE EXISTS TO MAKE CHECKABLE ─────────────────────────────────────────────
    //
    // Every forward variant shades the SAME surface, so every forward variant must declare the same
    // surface bindings, differing only by what its own vertex stage adds (`MeshPathOwnBinding` below).
    // `Desert/Tests/Engine/MeshVertexPath` asserts that against the reflected SPIR-V rather than against
    // a list somebody maintains. A shader added to the table without matching the surface is caught
    // there, and not by a black character in somebody's scene.
    enum class MeshVertexPath : uint8_t
    {
        Static    = 0, // one model matrix per draw, from the push constant
        Skinned   = 1, // bone matrices from the Bones SSBO, offset per draw
        Instanced = 2, // one model matrix per INSTANCE, from the InstanceTransforms SSBO
    };

    inline constexpr uint32_t kMeshVertexPathCount = 3;

    // What the fragment stage of the draw WRITES. Orthogonal to the path above: the same geometry can be
    // rasterized into the lit scene, into the deferred G-buffer, into the transparent composite or into a
    // shadow cascade, and which one it is has nothing to do with how its vertices were fetched.
    //
    // A PASS, not a shading model — the distinction matters and `Glass` is where it is easiest to blur.
    // Glass is the same PBR surface with transmission, drawn in a blended pass over the composite; which
    // objects go there is decided by the material's own `Transmission` value in
    // MeshRenderer::DrawStaticMeshes, exactly as UE routes a Translucent blend mode into the translucency
    // pass. Nothing about the shading model is encoded here.
    enum class MeshPass : uint8_t
    {
        Forward     = 0, // lit colour, the forward path and the over-composite draws
        GBuffer     = 1, // deferred albedo/normal MRT (also the Reflective Shadow Map, from the sun)
        Glass       = 2, // transparent, samples the composited scene for refraction
        ShadowDepth = 3, // depth only, into a cascade
    };

    inline constexpr uint32_t kMeshPassCount = 4;

    // (path x pass) -> the shader that draws it, or nullptr where the engine has no such variant.
    //
    // THE HOLES ARE THE DIAGNOSIS. Before this table the two axes were implicit, so nobody could see
    // that (Skinned x ShadowDepth) did not exist — the cascade pass simply never mentioned skinned
    // meshes and a character stood in the sun casting nothing. The cell is filled now. The remaining
    // holes are deliberate and each has a reason:
    //
    //   (Skinned  x GBuffer) — a skinned mesh is drawn FORWARD over the deferred composite instead
    //                          (MeshRenderer::RenderSkinnedManual), so it needs no G-buffer variant.
    //   (Instanced x GBuffer) — same story: instancing is disabled in the G-buffer pass, every static
    //                          takes the per-object path there.
    //   (* x Glass)          — transparency is a static-mesh feature; no skinned or instanced glass
    //                          exists to draw.
    //
    // A hole answers nullptr and the caller must SAY so rather than silently drawing something else —
    // that silence is what defect (2) above was made of.
    const char* MeshShaderFor( MeshVertexPath path, MeshPass pass );

    // The one binding a path adds to the surface's own set, or nothing for a path that adds none.
    // Set 0 binding 1 is the skinned path's `Bones`, binding 17 the instanced path's
    // `InstanceTransforms`; both are free in every other variant, which is what makes "same surface,
    // different path" expressible as ONE descriptor-layout relation.
    //
    // OPTIONAL and not "0 means none": binding 0 is the shared camera block in every mesh shader in the
    // engine, so a sentinel of 0 is a live slot wearing a sentinel's clothes. The first draft of this
    // returned a bare uint32_t and its own test caught it — the static path's "own binding" of 0 deleted
    // CameraUB from the comparison and made two shaders that agree look like two that do not.
    std::optional<uint32_t> MeshPathOwnBinding( MeshVertexPath path );

    // Human-readable, for logs and test failure text. Never parsed.
    const char* MeshVertexPathName( MeshVertexPath path );
    const char* MeshPassName( MeshPass pass );
} // namespace Desert::Graphic
