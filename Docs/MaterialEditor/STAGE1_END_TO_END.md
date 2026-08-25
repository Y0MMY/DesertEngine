# Stage 1 — is the shader-graph → material → mesh path actually usable?

Checked by rendering, not by reading. Debug build, MoltenVK, this worktree, `dev` @ `0cbbda1f`.
Every claim below has a frame or a log line behind it.

## Reproduction assets committed with this document

| file | what it is |
|---|---|
| `Editor/Resources/Assets/ShaderGraphs/MatConst.dgraph` | Color const → Surface Output, unlit. **No parameters at all.** |
| `Editor/Resources/Assets/ShaderGraphs/MatLitConst.dgraph` | same, `Lit` on. |
| `Editor/Resources/Assets/ShaderGraphs/MatProbeUnlit.dgraph` | `Color Param` ×2 + `Float Param` → `Lerp` → Albedo, unlit. |
| `Editor/Resources/Assets/ShaderGraphs/MatProbe.dgraph` | same graph, `Lit` on. |
| `Editor/Resources/Assets/ShaderGraphs/MatBroken.dgraph` | a `vec2` wired into the `vec4` Albedo pin — legal in the file, rejected by the canvas. |
| `Editor/Resources/Assets/Materials/MP_*.demat` | one material per shader; `MP_BlendMax` / `MP_GreenTint` carry non-default parameter values. |
| `Editor/Resources/Assets/Scenes/MAT_Probe.desce` | the six-sphere comparison. |
| `Editor/Resources/Assets/Scenes/MAT_ProbeOverride.desce` | the same shader down both routes: `MaterialComponent` override vs `.demat` slot. |
| `Editor/Resources/Assets/Scenes/MAT_ProbeBroken.desce` | the broken-shader repro, kept apart because it kills the editor. |

The `.shader` files under `Editor/Resources/Shaders/Programs/Graph/` were produced by
`ShaderGraph::CompileToDShader` — the same function the panel's **Compile** button calls.

Command used for every frame below:

```
scripts/MacOS/RunEditor.sh Debug --project Desert.deproj \
    --scene Resources/Assets/Scenes/MAT_Probe.desce \
    --shot <out>.png --shot-sequence <dir> --shot-every 1 --shot-frames 12 \
    --camera 100,60,900 --look 0,-0.02,-1
```

---

## 1. A graph material does reach the mesh — the wiring is real

`Created GenericMesh_MatProbe VulkanPipeline` in the log, and the spheres are drawn.
`MaterialData::ShaderName` (a plain string) → `MaterialFactory::CreateMaterial` →
`DataDrivenMaterial` → `MeshECSSystem` per-slot routing → `MeshRenderer::DrawGenericMeshes`.
Nothing about that chain is a stub.

`Docs/MaterialEditor/Shots/MAT_isolation_const_vs_param.png` — left to right:
PBR reference, graph **constant** unlit, graph **constant** lit, then three graph materials that
use **parameters**. The two constant ones are correct. The three parameterised ones are black.

## 2. THE DEFECT: any graph material with a parameter strobes black, 2 frames in 3

Same scene, consecutive frames, nothing moving:

* `Docs/MaterialEditor/Shots/MAT_params_correct_frame5.png` — every value is **exactly right**:
  `Blend`=0 gives `TintA` red, the `.demat` override `Blend`=1 gives `TintB` blue, `Lit` shading
  is present. The whole chain (schema default → `.demat` value → GPU) is correct.
* `Docs/MaterialEditor/Shots/MAT_params_black_frame6.png` — the same three spheres, pure black.

Over frames 1–12 the good frames are 5, 8, 11: **period 3, one frame in three**. Three is
`MaxFramesInFlight` on this swapchain (`[GpuProfiler] ... 3 frames x 6 slots`).

So the parameters are not wrong, they are **written into one frame-in-flight copy of the material
uniform buffer and never into the other two**. This is exactly the failure mode
`Docs/RENDERER_FRAME_STATE.md` exists for, and it is already live on the main viewport — no preview
window is needed to hit it.

Where the asymmetry sits, for whoever fixes it — `MeshRenderer.cpp:328`:

```cpp
if ( !g.SlotMaterial )
{
    material->ApplyDefaults();
    for ( const auto& [name, value] : g.Overrides.Params )
        material->SetParamRaw( name, value );
    ...
}
```

The per-frame re-application of parameters exists **only for the shader-override draws**. A
per-slot material — the `.demat` route, which is the only route the shader graph can take — has its
parameters applied once, in `MaterialFactory::ApplyShaderAsset`, at material-build time and outside
any recording. Constants survive because a constant-only graph emits no `Properties` block and
therefore has no material UB to miss.

The constants shot is the control that makes this a defect in the parameter path specifically and
not in the generic mesh path.

### The override path is correct, and that is the worst part

`MAT_ProbeOverride.desce` puts the two paths side by side on the **same shader**: two spheres driven
by a `MaterialComponent` shader override (no material asset), one sphere driven by the `.demat` slot.

* `Docs/MaterialEditor/Shots/MAT_override_ok_slot_black_frame7.png`
* `Docs/MaterialEditor/Shots/MAT_override_ok_slot_red_frame8.png`

The two override spheres are byte-identical across all twelve frames — defaults and the `Blend`=1
override both correct, `Lit` shading present, no strobe. The slot sphere alternates black/red on the
same period 3. One shader, one set of values, two routes, and only one of them works.

The Node Graph panel's existing 128 px preview (`AssetThumbnailRenderer::RequestShader`) clears
`MaterialSlots` and drives the sphere through `MaterialComponent::ShaderName` — the **override**
path. So today's preview already shows the artist a correct material while the scene renders it
black two frames in three. The preview lies, and it lies in the flattering direction. Any live
preview window built on the thumbnail renderer inherits that lie unless the slot path is fixed
first.

### Nothing tests this

`grep -rl "DataDrivenMaterial\|SetParamRaw" Desert/Tests` returns nothing. The entire generic
material parameter path — the one every shader-graph material takes — has no test of any kind.

## 3. Restart: the shader survives, and the `.demat` does not use a path-derived handle

`NodeGraphPanel::CompiledShaderPath` writes under `Resources/Shaders/Programs/Graph/`, which
`AssetPreloader::PreloadShaders` scans recursively at every launch, registering by **file stem**.
`MaterialData::ShaderName` stores that stem as a plain string. Every frame above came from a cold
launch that had never seen the graph before, so the restart leg is proven by the same frames.

The absolute-path warning does **not** bite the shader reference: `SHADERDIR_PATH` is `const` and is
never remapped by `SetProjectRoot`, and shaders are looked up by name, not by handle. Materials are
insulated separately — `SurfaceMaterialAsset::AdoptStableHandle` replaces the path-derived handle
with the in-file `MaterialId`.

It **does** bite scenes: every shipped `.desce` stores `MaterialPaths` as absolute paths into one
developer's home directory. It works today only because `MaterialGuids` is tried first. The probe
scenes here use relative paths on purpose.

## 4. A shader that fails to compile takes the editor down

`MatBroken` emits `vec4 albedo = n0;` where `n0` is a `vec2`. The graph compiler does **no type
checking** — the canvas refuses the link, the file format does not, and neither does
`CompileToDShader`.

```
[error] Shader Compilation Error (Fragment): .../MatBroken.shader:25: error: '=' :
        cannot convert from ' temp highp 2-component vector of float' to ' ... 4-component ...'
```

The error is reported properly. What follows is not: the engine goes on to build a graphics pipeline
out of the failed shader, Vulkan validation floods with `stageCount is 0` /
`no stage in pStages contains a Vertex Shader`, and the process dies with SIGSEGV **before the first
frame is presented**. No PNG, no sequence, nothing. Reproduced twice, deterministically, from
`MAT_ProbeBroken.desce`.

There is no pink fallback and no "keep the previous shader". For a live-preview window this is the
governing constraint: a preview that recompiles while the artist edits would be recompiling
intermediate graphs, and one bad intermediate currently ends the session.

## 5. Cost of compiling a graph to DShader text

`ShaderGraph::CompileToDShader` on `MatProbe.dgraph`, 2000 iterations, `-O2`: **7.96 µs per call**.
Text generation is free; it is not the thing to debounce. The cost that matters for a live preview
is the shaderc GLSL→SPIR-V compile plus pipeline creation inside the engine, which is not measured
here.

## Smaller things noticed, none of them load-bearing

* `Editor/Resources/Shaders/Programs/Graph/NewShaderGraph.shader` is committed with a parameter
  named `fdf`, while the committed `Editor/Resources/Assets/Materials/NewShaderGraph.dgraph` names it
  `BaseColor`. The generated file in the repo does not correspond to the graph in the repo.
* That `.dgraph` sits in `Materials/`, but the panel's **Load** popup only lists
  `ASSETS_PATH/ShaderGraphs`. A graph created from the content browser in any other folder is
  invisible to Load (double-click still opens it).
* The generated `Pass "Depth"` has no consumer: `RegisterShadowPass` walks only the static and
  instanced queues, so graph materials cast no shadows.
