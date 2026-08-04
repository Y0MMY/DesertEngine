# CubeGrid (Modeling Mode) — remaining UE features

Status of the UE5 "Cube Grid" tool reproduction. **Grid Mode core is done**; the items below are the
UE features we have **not** implemented yet. Tool code: `Editor/Source/Editor/Panels/ViewportPanel/Tools/CubeGridTool.{hpp,cpp}`,
panel `Editor/Source/Editor/Panels/Modeling/ModelingPanel.cpp`, shared state `Editor/.../Selection/ModelingState.hpp`.
Reference screenshots: `docs/ModelingImages/`.

## Done (Grid Mode)
- Sparse voxel volume (hash set of packed `ivec3`) at a **fixed base resolution** — drawn blocks never move
  when the grid step changes (finer → lossless subdivide `RefineBy`; coarser → bigger `K`-block stamp).
- **Committed layers**: starting a new marquee freezes the volume you just pushed out (`Layer{cells, unit}`).
  A frozen layer keeps its own Block Size forever, so a later Resize Grid only ever re-scales the piece you
  are working on now. Frozen layers are still raycast (build on them) and meshed (face-culled across layers),
  but Pull/carve and the grid resize can no longer touch them.
- **Level up/down** moves the ground work-plane in world units (`m_GroundY`), so the hover preview follows it.
- Raycast targeting: ground plane + nearest cube-face pick → work-plane.
- Marquee rectangle **selection** (block-aligned, latched drag, plane locked at press).
- **Push / Pull** (Extrude / carve) along the plane normal; build on the ground or any face.
- **Blocks Per Step** multiplier; **Resize Grid** (free Block Size input, snaps to a base multiple, `/2 ×2`).
- On-plane grid lines (per block) + dimension labels: **width × depth** on the edges and a vertical
  **height** line at the near corner (how deep the solid runs under the work-plane). Green preview tracks
  the cursor.
- **Grid frame**: `Grid Frame Origin` + **Reset Grid from Actor** put the lattice on an object's origin so
  any block size stays flush with its corners; `Show Gizmo` draws the frame axes. Changing the frame
  commits the current piece (cells are lattice indices) — frozen layers remember the frame they were built
  in, so nothing already built ever moves.
- **Grid Power** slider (1 m ≫ power) driving the free `Current Block Size` field.
- **Hit Unrelated Geometry**: targeting also considers other scene meshes (`Scene::Raycast`, box level), so
  a grid can start on top of an imported prop. **Accept and Start New** in the panel.
- Face-culled meshing → live `DynamicMesh` "Blockout" entity; **Accept** (keep) / **Cancel** (delete).
- UE shortcuts: **E / Q** = Push / Pull, **Ctrl+E / Ctrl+Q** = Resize Grid, **Ctrl + Wheel** = shift the
  work-plane, **Ctrl + MMB** = snap the work-plane onto the clicked surface, **Esc** = clear the selection.
  While a modeling tool is active the fly-camera only takes the keyboard during an RMB look
  (`EditorCamera::SetKeyboardRequiresLook`), so the bare keys belong to the tool — as in UE. Every action
  also has an on-screen button, and the panel lists the bindings under "Shortcut Info".

## Remaining

### 1. Corner Mode — remaining bits
Done: cells store 8 vertical corner offsets (`Cell::V`, 1/60 of a base cell) instead of a solid flag, the
mesher triangulates the deformed box with per-quad normals, face culling compares the four shared corners
(so a ramp's slanted face survives while the flat faces under it stay culled), **Z** toggles the mode, the
selection's four posts are click-picked (Shift adds) and **E / Q** move them by the **Snap Size** step
(½ / ¼ / ¹⁄₁₀ block); the cells under the rectangle take the bilinear blend, so two posts up = a ramp,
one post up = a hip. A finer Block Size commits a deformed piece instead of splitting it.
Remaining:
- Corners only move along the grid's **up axis** — a selection on a vertical face can't be sloped yet.
- Pick **individual cell corners / edges** (today it is the rectangle's four posts, which is what gives
  clean ramps but can't dent a single cell).
- **Crosswise Diagonal** (which way a deformed quad is split) + arches from sub-block corner steps.

### 2. Rotated / local grids
- **Grid Frame Orientation** + **Local Grid** (World vs Actor) so the lattice follows a rotated object —
  every ray/cell conversion is axis-aligned today, so this needs a frame rotation throughout.
- Explicit **leading-axis switch (X/Y/Z)** for sideways/vertical grids.

### 3. Re-edit baked meshes
- Re-activating CubeGrid on an existing Static Mesh **recognises its voxel structure** and re-shows the
  orange grid to keep building/carving. Needs the voxel volume persisted with the asset (or reconstructed
  from an axis-aligned box mesh).

### 4. Selection ergonomics
- **Shift+LMB drag = deselect** (subtract); **Ctrl+Shift+LMB** additive one-click box marquee.
  Needs the free-form buffer below — the selection is a single rectangle today.
- **Space** = rotate the selection / slope direction 90°.
- Free (non-rectangular) multi-cell selection buffer in addition to the rectangle.

### 5. Quick Materials
- **Active Material** slot; Extrude assigns that material ID to the new faces.
- **Shift+B** applies the active material to the selected faces without changing geometry.
- Bake splits into **sub-meshes per material ID**.

### 6. Bake quality (Accept)
- **Triplanar / world-aligned UVs** (no stretching on resized blocks).
- **Hard-edge normals + tangents** at block seams.
- **Collision** generation (BVH triangle mesh, or merged box colliders for Jolt).
- **Output Type = Static Mesh**: bake to an optimised StaticMesh asset (the combo exists; only the live
  DynamicMesh path is wired today).

### 7. Greedy meshing (face merging)
- Merge coplanar adjacent faces into larger quads — cleaner topology / fewer tris (today: per-cell face
  culling only).

### 8. Create-category primitives
- Box / Sphere / Cylinder / Cone / Stairs presets in the palette are placeholders — not implemented.

---
Related but separate: **PolyEdit** (face push/pull on arbitrary meshes) is its own tool, Phase 1 done.
