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

## Phase 2 — mesh & material sections that read like UE — **DONE**

* **Static/Skinned Mesh:** preview + the facts UE shows next to it — triangle and vertex counts, LOD
  count and the active LOD, bounds extent, UV channel count, whether a collision shape exists. All of it
  is already known to `Mesh`/`Submesh`; it just isn't surfaced.
* **Materials:** per-slot row = thumbnail + name + the slot's shader, drag-drop target, and an inline
  swatch strip of that material's base colour/metallic/roughness so a slot is identifiable without
  opening it.
* **Skinned Mesh:** bone count, the bound skeleton's signature, current clip, and a small bone-count
  warning when it exceeds the skinning limit.

**Done when:** you can pick the right material slot and spot a too-heavy mesh without leaving Details.

**As shipped**

*Mesh section* (`ComponentWidgets/Helper/MeshDetailsWidget.{hpp,cpp}`, rewritten) — one section shared by
the static and skinned widgets, driven off the mesh **actually drawn** (an edited `RuntimeMesh` / in-editor
rig wins over the asset mesh, the same precedence the material slot count uses):

* triangles (from `IndexCount`, not `VertexCount/3` — the old readout undercounted every indexed mesh),
  vertices, elements, LOD levels, bounds extent in cm, world size when the entity is scaled, UV channels,
  and the entity's collision shape; plus a folded per-element table (name / tris / verts / LODs).
* **Active LOD** is reported through the renderer's own policy: `MeshRenderer::ComputeLOD` moved its body
  into `Engine/Geometry/LODSelection.hpp` (`Geometry::SelectLOD`, header-only) and now only resolves the
  camera + the LOD toggle around it. Details calls the same function with the scene's active camera, so
  "drawing LOD 1" cannot drift from what the viewport draws. The existing *Level of Detail* list marks the
  same level with `(drawing)`.
* A mesh whose GPU build hasn't run says so instead of rendering zeros.

*Material slots* (`MaterialsPanelComponent`): each element header now reads
`Element 0 — M_Rock (inherited)` and carries a right-aligned **swatch strip** — a colour chip plus up to
two mini bars — painted straight into the header bar with `ImDrawList` so it adds no item and the row keeps
its click and `.demat` drop behaviour. Opening a slot shows an identity card: rendered thumbnail, material
name, shader, `Instance of:` parent, and the same swatches labelled with their values.

The strip is built from the **shader schema**, never from hardcoded PBR names: the first `Color` property
becomes the chip, the first two `Range`-bounded floats become the bars. For `StaticMeshPBR` that is exactly
albedo / metallic / roughness; a custom DSL shader gets an equally identifiable slot for free. Values
resolve child-override → parent chain → schema default, so an instance's chip shows what it renders.

*Skinned* (`SkinnedMeshComponentWidget`): skeleton signature, per-frame pose upload (`bones × 64 B`), the
influence cap, and the current clip (marked when an Anim Graph drives it). The planned "over the skinning
limit" warning was **not** implementable as specified — there is no fixed bone cap: the pose lives in a
storage buffer that grows on demand (`VulkanStorageBuffer::SetData`). Instead the section audits the vertex
weights (cached; recomputed only when the mesh changes) and reports the real defects: vertices referencing a
bone the skeleton lacks (wrong rig — a GPU out-of-range read), vertices with no weights at all (they stay in
bind pose), and how many use all four influence slots (the importer dropped the rest).

Still open: Details reads the **shared** rendered-thumbnail PNG cache but never fills it — a material the
asset browser has not displayed yet falls back to its colour chip. Generating one from here means a second
offscreen renderer in this panel, which is deliberately deferred.

---

## Phase 3 — lights, sky and other "invisible" components — **MOSTLY DONE**

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

**As shipped**

* **Colour temperature is an attribute, not a widget**: `PROPERTY(Color, Temperature)` adds a Kelvin slider
  under any colour swatch that WRITES the RGB (blackbody approximation, normalised so the brightest channel
  is 1 — Kelvin sets hue, Intensity owns brightness). Only the resulting colour is stored, so the slider
  keeps its own position for the session; a colour cannot be turned back into one temperature. It carries
  its own undo pair, because the row's existing capture belongs to the colour widget. Applied to all three
  light types.
* **Intensity** now says what it is: `Units("x")` plus a tooltip stating it is a linear multiplier on the
  light colour, *not* lux or candela — the renderer multiplies radiance by it directly.
* **Sun dial** (Directional Light became a custom entry): a top-down hemisphere — centre is the zenith, rim
  the horizon, angle the compass azimuth (N/E/S/W marked) — dragged directly, with azimuth/elevation
  sliders beside it for precision and for a sun below the horizon (drawn hollow). It writes
  `TransformComponent.Translation`, which is where the engine keeps the direction light TRAVELS, and keeps
  the vector's length so scenes that author it as a "sun position" are not silently rewritten.
* **Camera**: focal length in millimetres (35mm-equivalent, 24mm sensor height) as a second view of the same
  FOV field, editable both ways; and "Look through this camera", which moves the EDITOR camera to the
  component's transform (`SnapToDirection` + `Focus`) instead of handing the viewport over — leaving is just
  moving the view again.
* **Collider**: a warning when the shape disagrees with the mesh bounds by more than 25% (relative, because
  5 cm matters on a doorknob and not on a hillside), drawn right above the "Fit to Mesh Bounds" button. The
  check reuses the fit's own measurement, so the two can never disagree.
* **Sky**: a colour ramp of the procedural sky's authored colours (zenith → horizon → ground) with the
  sunset tint mixed in at low sun and the sun marked at its current elevation, taken from the scene's
  directional light. Deliberately a legend for the fields below rather than a render of the sky shader:
  it costs nothing and updates while you drag.
* **Particle emitter** (custom entry): play/pause writes the same `Enabled` field the renderer reads, and
  Restart raises a transient `RequestRestart` that `ParticleRenderer::PrepareFrame` consumes by ZEROING the
  emitter's particle buffer — not destroying it, since the GPU may still be reading it this frame (the
  lesson from `43d2e65`). It works while paused, so resuming starts clean.

Still open in this phase: **draggable** radius / cone handles in the viewport (`LightGizmoRenderer` draws
them read-only today) and a looping *rendered* thumbnail of the emitter, which needs the preview renderer to
carry a live particle scene.

---

## Phase 4 — the grid itself — **DONE**

* **Search box** filtering fields across every component (UE's Details search).
* **Per-component header:** icon + a one-line summary of the component's state (e.g. "Point · 1000 cm ·
  warm"), so a collapsed component still tells you something.
* **Row polish:** hover highlight, a drag-handle cursor on numeric labels, unit suffixes everywhere
  (extend the `Length` attribute pattern to angles/time/percent), and consistent control widths.
* **Sections:** remember expand/collapse per component type; "Advanced" fold for rarely-touched fields
  (a new `PROPERTY(Advanced)` attribute — the header tool already parses attributes, and `Length` is the
  precedent for adding one).
* **Favourites:** pin a field to the top of Details (UE's star), persisted in `EditorPreferences`.

**As shipped**

Three new `PROPERTY(...)` attributes, end to end (`Tools/DesertHeaderTool` → `PropertyMetadata` →
`PropertyEditorBuilder`), which is phase 5's mechanism arriving early — every future component gets these
for free by declaring them:

| Attribute | Effect |
| --- | --- |
| `Units("deg")` | suffix in the value text + a drag step that suits the quantity. `Length` is the world-distance case (cm) and stays its own flag. Nothing is ever converted — the stored number already is in these units. |
| `Advanced` | the field folds under an "Advanced" node at the end of its category |
| `Summary` | the field feeds the one-line summary beside the component header |

* **Search** (`ScenePropertiesPanel::DrawSearchBox` → `ComponentEditContext::FieldFilter`): matches a
  field's label, its C++ name or its category, case-insensitively; empty categories and whole components
  disappear, and everything auto-expands while a search is active. A **hand-written** component widget has
  no field metadata, so it can only be matched on its NAME — it is shown whole or not at all. Reflected
  components filter per field.
* **Component header**: icon (a table in `ComponentEditor.cpp`; a component missing from it gets a neutral
  glyph, so nothing has to be registered twice) + a right-aligned summary built from the type's `Summary`
  fields, dropped when the panel is too narrow for it. Expand/collapse now **persists** across restarts in
  `EditorPreferences::CollapsedComponents`; ImGui still owns the live state, the panel just seeds it once
  and mirrors user toggles back (a search-forced expansion is deliberately not written back).
* **Pinned fields** (`EditorPreferences::FavouriteFields`, keyed `TypeName.FieldName`): a star appears on a
  row while it is hovered, and stays once pinned; pinned fields are repeated in a "Pinned (n)" section
  above every component, editing the same memory. Reflected fields only — a custom widget has no field
  identity to key on.
* **Rows**: a hover band across the full row (painted before the row, since a fill drawn afterwards would
  cover the widgets), the pin and reset affordances share the label column's right edge, and the label
  column keeps its panel-relative width.

Annotated as the first users: camera FOV (deg, summary), the three light types (intensity/radius/range
summaries, cone angles in deg, falloff + min radius advanced), collider shape, rigid body type/mass
(friction + restitution advanced), audio clip, character controller slope/gravity.

Not done: the **drag-handle cursor on numeric labels** (dragging the label itself to change the value).
It needs the label to become an interactive drag zone that forwards to the widget, which is a bigger
change to the row than the rest of this phase and is better done with the row rewrite in phase 5.

---

## Phase 5 — reflection metadata to make it all data-driven — **ATTRIBUTES DONE**

Every visual above should come from the component declaration, not from a hand-written widget, so a new
component gets the treatment for free. Additions to `PROPERTY(...)` (parser: `Tools/DesertHeaderTool`,
consumer: `PropertyEditorBuilder`):

| Attribute | Effect |
| --- | --- |
| ~~`Advanced`~~ | done in phase 4 |
| ~~`Units("deg" / "s" / "%")`~~ | done in phase 4 |
| ~~`Summary`~~ | done in phase 4 |
| ~~`Temperature`~~ | done in phase 3 (Kelvin slider on a colour) |
| ~~`Preview`~~ | done — an asset slot draws its content inline (checkerboard box) instead of only on hover |
| ~~`EditCondition("Foo")`~~ | done — the row greys out while the named bool of the same block is false; `"!Foo"` inverts |

**As shipped.** `EditCondition` leaves the field VISIBLE and merely inert (label greyed, value disabled, and
the hover tooltip says which flag it waits for): hiding a setting only makes people wonder where it went.
An unknown or non-bool name evaluates to *true* on purpose — a typo in an annotation must not silently
freeze a field nobody can then explain. Both attributes apply to the fields of the block being drawn, not
to deeper nested structs. Annotated as the first users: UI panel gradient/pulse fields, terrain grass
density, and the UI panel Sprite slot (`Preview`).

What remains is converting the hand-written widgets (mesh, materials, skybox, transform) to the same
metadata, so a new asset-bearing component gets its slot UI without a bespoke widget. Deliberately NOT
done yet: those widgets carry real behaviour (slot inheritance, LOD chains, rigging) that no attribute
expresses today, so the rewrite would be a large regression risk for no user-visible gain. The right
trigger is the next component that needs the same UI — build the attribute then, with a second use case
to design against.

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

1. ~~Phase 1 (preview widget)~~ — done.
2. ~~Phase 2 (mesh/material)~~ — done.
3. ~~Phase 4 row polish + search~~ — done.
4. ~~Phase 3 (lights/sky/camera)~~ — done except the draggable viewport handles.
5. Phase 5 metadata — convert the hand-written bits back to data as they stabilise.
