# Details panel — UE-parity plan (previews + per-component visuals)

Goal: the Details panel should let you understand and shape a component **by looking at it**, the way
Unreal's does — a mesh you can see and orbit, a light whose colour and reach are visible, a sky you can
read at a glance — instead of a grid of numbers with a filename on top.

Written as a work plan: each phase is independently shippable and testable, ordered so the shared pieces
land before the things that use them.

---

## Where we are today (facts, not impressions)

Read this before planning around it — a fair amount already exists.

**Rendering path.** `ScenePropertiesPanel` draws the entity header (checkbox, type icon, editable name)
then walks `ComponentWidgetRegistry`. A component is either
* **reflected** — `DESERT_REGISTER_REFLECTED_COMPONENT(...)`; `PropertyEditorBuilder` builds the rows from
  `PROPERTY(...)` metadata, grouped into `CollapsingHeader`s by `Category`, or
* **custom** — `DESERT_REGISTER_CUSTOM_COMPONENT(...)` with a hand-written widget
  (`ComponentWidgets/`: Transform, StaticMesh, SkinnedMesh, Materials, Skybox, Animation, Prefab).

Components self-register at static-init, so adding one never touches the panel.

**Already there:**
* Reset-to-default arrow per field (from the type's default instance).
* Tooltips from `PROPERTY(Tooltip(...))`, `Range`, `Color`, `Hidden`, `ReadOnly`, `Header`, `Length` (cm).
* Asset slots: texture (name + **hover** thumbnail), font picker, video slot, icon slot with a **96px
  inline preview** on a checkerboard.
* `AssetThumbnailRenderer` renders a material on a sphere / a mesh auto-framed by its bounds / a shader
  to a PNG, cached on disk by `ThumbnailCache` and shown in the Content Browser grid.
* Material editing lives in `MaterialsPanelComponent` (per-slot, drops its cached thumbnail on save).
* Multi-select editing with a `(mixed)` marker.

**What's missing** — everything below.

---

## Phase 1 — a reusable preview widget — **DONE**

Everything else depends on this, so it comes first.

`AssetThumbnailRenderer` can only write PNGs, one capture at a time, spread over two frames. That is
right for a browser grid and wrong for Details, where the preview must be **live** (orbit it, watch a
material change as you drag a slider).

Build `Editor/Widgets/PreviewViewport` — an owned `SceneRenderer` + a private one-entity scene rendered
into an offscreen image shown with `UIHelper::Image`:

```
class PreviewViewport {
    void SetMesh( meshHandle, materials );       // auto-frame by bounds
    void SetMaterial( materialHandle, shape );   // sphere / plane / cube
    ImTextureID Render( ImVec2 size );           // live, this frame
    // LMB-drag orbits, wheel zooms, RMB pans; state persists per (panel, entity)
};
```

Notes that will bite otherwise:
* One preview renderer per **panel**, not per component row — a `SceneRenderer` is not cheap.
* Do the offscreen render in the panel's `OnPreUpdate()`, never inside `OnUIRender()`: the editor
  already learned this the hard way (destroying descriptor pools while their sets are bound to the
  recording command buffer — see the `m_PendingViewportSize` comment in `ViewportPanel`).
* Skip the render entirely when the panel is collapsed or the preview is scrolled out of view.
* Reuse `ThumbnailCache` for the *static* case (a collapsed section shows the cached PNG until you
  expand it) so a scene full of meshes doesn't spin up renderers.

**Done when:** selecting a static mesh shows a live, orbitable preview in Details, and dragging a
material slider updates it in place.

**As shipped** (`Editor/Widgets/PreviewViewport.{hpp,cpp}`):
* `SetMesh(handle, materialSlots)` / `SetMaterial(handle, Shape::Sphere|Cube|Plane)` / `Clear()`.
* Unlike the thumbnail renderer it installs its **own `GameplayCamera`** via `Scene::SetActiveCamera`
  and drives it from orbit state (`SetFromTransform`), so it frames by moving the camera instead of
  scaling the object — LMB/RMB drag orbits, wheel zooms multiplicatively, double-click re-frames.
* The API enforces the frame-ordering rule: **`Update(w,h)`** records the render (panel `OnPreUpdate`),
  **`Draw(uiHelper, size)`** only blits + handles input (panel `OnUIRender`).
* Framing retries until the `MeshService` actually has the mesh, so an asset that streams in a few
  frames later still gets framed rather than previewed against a guessed radius.
* Wired into `ScenePropertiesPanel` as a "Preview" section above the component list: one renderer per
  panel, re-pointed by a content key (entity + mesh/primitive + slots), and the render is **opt-in per
  UI frame** — the flag is consumed in `OnPreUpdate`, so a folded section, a hidden dock tab or a
  closed panel all stop the GPU work by simply not drawing.

Still open from the notes below: falling back to the cached `ThumbnailCache` PNG for a *collapsed*
section (today a folded section just shows nothing until you expand it), and RMB panning.

---

## Phase 2 — mesh & material sections that read like UE

* **Static/Skinned Mesh:** preview + the facts UE shows next to it — triangle and vertex counts, LOD
  count and the active LOD, bounds extent, UV channel count, whether a collision shape exists. All of it
  is already known to `Mesh`/`Submesh`; it just isn't surfaced.
* **Materials:** per-slot row = thumbnail + name + the slot's shader, drag-drop target, and an inline
  swatch strip of that material's base colour/metallic/roughness so a slot is identifiable without
  opening it.
* **Skinned Mesh:** bone count, the bound skeleton's signature, current clip, and a small bone-count
  warning when it exceeds the skinning limit.

**Done when:** you can pick the right material slot and spot a too-heavy mesh without leaving Details.

---

## Phase 3 — lights, sky and other "invisible" components

These have no natural thumbnail; UE gives them **visual affordances** instead.

* **Point / Spot light:** colour swatch with a **temperature (Kelvin) slider** that writes the RGB, an
  intensity readout in the unit the renderer actually uses, and a live in-viewport gizmo for radius /
  cone angles (`LightGizmoRenderer` already draws icons and frustums — extend it to draggable handles).
* **Directional light:** a small sun-direction dial (azimuth/elevation) that writes the transform, since
  the direction currently hides in `TransformComponent.Translation`.
* **Skybox / atmosphere:** a strip preview of the procedural sky at the current sun angle, and a
  cubemap face-strip for an HDR skybox.
* **Camera:** an FOV readout in both degrees and focal-length mm, near/far in cm (the `Length` attribute
  already labels them), and a "look through this camera" button that borrows the viewport.
* **Particle emitter:** a looping thumbnail of the emitter's current state, plus a play/pause/restart
  transport (the Particle Editor has the machinery; Details needs the summary).
* **Collider / RigidBody:** shape sketch + mass/inertia readouts, and a warning when the collider
  visibly disagrees with the mesh bounds (the greybox house shipped with double-size colliders for
  exactly this reason — see the units commit).

**Done when:** a light, a sky and a camera can each be set up correctly without trial-and-error in Play.

---

## Phase 4 — the grid itself

* **Search box** filtering fields across every component (UE's Details search).
* **Per-component header:** icon + a one-line summary of the component's state (e.g. "Point · 1000 cm ·
  warm"), so a collapsed component still tells you something.
* **Row polish:** hover highlight, a drag-handle cursor on numeric labels, unit suffixes everywhere
  (extend the `Length` attribute pattern to angles/time/percent), and consistent control widths.
* **Sections:** remember expand/collapse per component type; "Advanced" fold for rarely-touched fields
  (a new `PROPERTY(Advanced)` attribute — the header tool already parses attributes, and `Length` is the
  precedent for adding one).
* **Favourites:** pin a field to the top of Details (UE's star), persisted in `EditorPreferences`.

---

## Phase 5 — reflection metadata to make it all data-driven

Every visual above should come from the component declaration, not from a hand-written widget, so a new
component gets the treatment for free. Additions to `PROPERTY(...)` (parser: `Tools/DesertHeaderTool`,
consumer: `PropertyEditorBuilder`):

| Attribute | Effect |
| --- | --- |
| `Advanced` | field folds under "Advanced" |
| `Units("deg" / "s" / "%")` | suffix + sensible drag speed (generalises `Length`) |
| `Preview` | the field's asset gets an inline preview instead of a name button |
| `Summary` | field participates in the collapsed-header one-liner |
| `EditCondition("Foo")` | grey the field out while another field is false (UE's EditCondition) |

---

## Risks / decisions to make up front

1. **GPU cost.** A live preview per selection is one extra render pass. Cap it: one renderer per panel,
   paused when hidden, and fall back to the cached PNG for collapsed sections.
2. **Frame ordering.** All offscreen rendering goes in `OnPreUpdate()`. This is not optional — see the
   Phase 1 note.
3. **Don't fork the material editor.** Details should *host* `MaterialsPanelComponent`, not grow a second
   material UI.
4. **Custom vs reflected.** Prefer extending reflection metadata (Phase 5) over writing another custom
   widget; each custom widget is a place the next component won't benefit from.
5. **Vulkan cannot run in the dev container**, so every phase needs a visual check on a real machine.
   Keep phases small for that reason.

---

## Suggested order

1. Phase 1 (preview widget) — unblocks everything.
2. Phase 2 (mesh/material) — the most-used components.
3. Phase 4 row polish + search — cheap, felt everywhere.
4. Phase 3 (lights/sky/camera) — highest "can't do this today" value.
5. Phase 5 metadata — convert the hand-written bits back to data as they stabilise.
