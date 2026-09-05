# The material editor window

A live material preview: the material an artist is editing, on a primitive, under a key light and a
procedural sky, orbitable, with its parameters beside it. `View -> Material Preview`, or automatically
when the Node Graph compiles.

## Built on PreviewViewport, not SceneDocument

`PreviewViewport` already **was** "own tiny scene + own `SceneRenderer` + own image, a primitive under a
key light and a procedural sky, drawn in ImGui", and the Details panel is a working example of a panel
owning one. `SceneDocument` exists to give a fully editable **document** a render registry, scene-tree
binding and active-document switching — none of which a preview wants — and it has **no destruction path
anywhere**, so a copy of it could never have given its renderer slot back.

## It cannot flatter, by construction

`PreviewViewport::SetMaterial` fills `StaticMeshComponent::MaterialSlots` — the per-slot route, the one a
real scene mesh takes and the one stage 1 fixed. The 128 px thumbnail it replaces went through
`MaterialComponent::ShaderName`, the **override** route, which `MeshRenderer` re-seeds with schema
defaults every frame. That is how the old preview could show a correct material while the scene showed
black. It is deleted, not moved — only the Node Graph's *use* of `AssetThumbnailRenderer`, since the
asset browser still needs the class.

`Desert/Tests/Editor/MaterialPreviewRoute` guards it, and fails in **both** directions.

## The defect it would otherwise have inherited

A recompile is only half published. The `Shader` object is shared and reloads itself, but pipelines are
cached **per `SceneRenderer`**, and `AssetHotReload::PollShaders` invalidates only the cache of the scene
handed to `Tick` — the main one. A preview with its own renderer would keep drawing the OLD modules while
the viewport drew the new: the stage-1 disease in new clothes. `PreviewViewport::InvalidatePipelines`
exists for this, and `Compile` calls it directly rather than waiting on the file watchdog.

## Closed costs zero, proven byte for byte

The viewport — and with it a `Scene`, a `SceneRenderer` and one of the six renderer slots — is created on
the first frame the window actually draws and **destroyed** as soon as it is not. `OnPreUpdate` runs for
hidden panels, which is what makes returning the slot possible there.

`Starter.desce`, fixed camera, 60 frames:

| build | sha256 |
|---|---|
| before the panel existed | `bd51a908fecc328231f22ad0f856ea5eee004c867b285d5f39c9038c2917bffb` |
| panel present, closed | `bd51a908fecc328231f22ad0f856ea5eee004c867b285d5f39c9038c2917bffb` |

Reproduced four times across three binaries. The log carries the negative too: no renderer slot claimed,
no preview scene created.

## What the window costs when open

Per-pass GPU timestamps, `--gpu-profile`, `MAT_Probe.desce`, 240 frames, three interleaved pairs.
**Both legs instrumented** — the instrument inflates its own frame by ~1.24 ms, so an instrumented run
must never be compared against an uninstrumented one.

| | run 1 | run 2 | run 3 | min |
|---|---|---|---|---|
| closed — frame (wall) | 8.690 | 8.816 | 8.430 | **8.430 ms** |
| open — frame (wall) | 10.762 | 10.549 | 12.599 | **10.549 ms** |
| closed — GPU frame | 7.502 | 7.280 | 7.369 | **7.280 ms** |
| open — GPU frame | 9.254 | 9.377 | 9.449 | **9.254 ms** |

**The open window costs ≈2.1 ms of wall and ≈2.0 ms of GPU** per frame, at a 512×512 preview with FXAA
and a procedural sky. `SceneRenderer::OnUpdate` reports `x=1` closed and `x=2` open — the second renderer
made visible in the table rather than inferred.

## Why parameters are live and topology is debounced

Measured, interleaved, three pairs. A cold editor start (shader cache emptied) against a warm one:

| | run 1 | run 2 | run 3 |
|---|---|---|---|
| cold | 37.25 s | 37.47 s | 37.98 s |
| warm | 22.18 s | 23.35 s | 22.12 s |

≈15.1 s buys 123 SPIR-V modules — **≈123 ms per module** — and a Surface graph emits three (vertex,
fragment, and the depth pass), so **≈370 ms per rebuild**.

The SPIR-V disk cache (`Editor/Cooked/ShaderCache`) does **not** soften this. Its key is the compiled
text, and a preview recompile happens precisely *because* that text just changed, so every one is a miss.
Measuring on a warm cache would have understated the cost by more than half.

So the answer splits, and the split is the whole design:

* **A parameter is a uniform-buffer field.** Editing it recompiles nothing — it is the path stage 1
  fixed. Sliders in the preview window update live.
* **Graph topology needs a full rebuild.** `NodeGraphPanel::AutoCompileIfSettled` therefore debounces at
  **400 ms**, about the cost of one rebuild, so a settled graph reaches the preview in roughly the time a
  rebuild takes while a dragged edit produces one compile instead of a dozen.

The fingerprint the debounce watches covers node kinds, constant values, parameter names and links —
**never node positions**, because dragging a node changes no generated GLSL and recompiling for it would
spend ~370 ms producing byte-identical output.

Loading or creating a graph seeds the fingerprint, so auto-compile fires on an **edit** and not on merely
opening the panel. That was measured too: before the seeding, `GraphShader.shader` was written the moment
the window appeared, for nothing.

## Looked at, and the looking found something

* `Shots/MAT_window_labels_overlapped.png` — the first capture. Parameter rows read `A255e255 255 255`,
  because ImGui draws a widget's label *after* the widget. Invisible in the source, which reads fine.
* `Shots/MAT_window_live_graph_material.png` — after rebuilding on the editor's two-column table.
  MP_GreenTint, a shader-graph material, green on a lit sphere against the sky, `TintA`/`TintB`/`Blend`
  beside it, and `Created GenericMesh_MatProbe VulkanPipeline` in the log.

Both are kept. The pair is the argument for the rule.

`--open-panel <name>` is new and is why there is a capture at all: a tool panel rightly defaults to
hidden, and there was no way to put one on screen unattended — scripted clicking needs macOS assistive
access, which a build agent does not have. It opens exactly what the View menu opens.

## Left undone, deliberately

* ~~**The Details panel's own preview never gives its slot back.**~~ **FIXED since — `808168af`, "slots:
  the Details preview leased one of six and never gave it back", which also added
  `Engine/Core/RendererSlotPool.hpp` and the `RendererSlots` suite.** Left here rather than deleted
  because the entry was cited as a live defect by the Stage 3 plan a week later, and the developer who
  caught that checked the tree instead of the document. An item in a "left undone" list has no expiry
  stamped on it, and that is how it outlived its own repair. Details still *takes* a slot while
  something is selected — it now gives it back.
* **The preview's shape and orbit are not persisted.** Reopening the window starts at the default angle.
* **The window previews one material at a time**, chosen in its combo or pushed by Compile. No side-by-side.
* **`AutoCompileIfSettled` polls a fingerprint** rather than being told the graph changed. It is cheap
  (a few dozen integer mixes) but it is a poll, and an explicit dirty flag from the canvas would be better
  if the graph ever grows large.
