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
- On-plane grid lines (per block) + drawn-size labels (W×D world units); green preview tracks the cursor.
- Face-culled meshing → live `DynamicMesh` "Blockout" entity; **Accept** (keep) / **Cancel** (delete).
- All controls are on-screen buttons — no keyboard clash with the Q/E/arrow camera flight.

## Remaining

### 1. Corner Mode (`K`) — ramps / slopes / roofs / arches  ← biggest
- Per-cell **8-vertex offsets** (`int8 vertexOffsets[8]`) instead of a plain solid flag.
- Vertex-based raycast: highlight nearest **corner / edge (2 verts) / face (4 verts)**; Shift = multi-select.
- Extrude/Push moves the selected vertices by a step → wedge/prism/ramp.
- Mesher must triangulate **deformed cubes** (not just axis-aligned boxes).
- **Slope subdivision / Snap Size** param (¹⁄₂, ¹⁄₄, ¹⁄₁₀ of the block height).

### 2. Grid realignment & snapping
- **Ctrl+MMB** — snap the work-plane onto any clicked surface/face height.
- **Reset Grid / "Reset Grid from Actor"** — set the grid origin to the object's local (0,0,0) so any grid
  size stays aligned to its corners (fixes the misaligned-domain Scenario 4 / stray Push).
- **Grid Frame Origin / Orientation** panel fields; **Local Grid** (World vs Actor) so the grid follows a
  rotated object; explicit **leading-axis switch (X/Y/Z)** for sideways/vertical grids.

### 3. Plane shift via **Ctrl + Mouse Wheel** (today only the on-screen Level buttons).

### 4. Re-edit baked meshes
- Re-activating CubeGrid on an existing Static Mesh **recognises its voxel structure** and re-shows the
  orange grid to keep building/carving. Needs the voxel volume persisted with the asset (or reconstructed
  from an axis-aligned box mesh).

### 5. Selection ergonomics
- **Shift+LMB drag = deselect** (subtract); **Ctrl+Shift+LMB** additive one-click box marquee.
- **Z** = clear all selection. **Space** = rotate the selection / slope direction 90°.
- Free (non-rectangular) multi-cell selection buffer in addition to the rectangle.

### 6. Quick Materials
- **Active Material** slot; Extrude assigns that material ID to the new faces.
- **Shift+B** applies the active material to the selected faces without changing geometry.
- Bake splits into **sub-meshes per material ID**.

### 7. Bake quality (Accept)
- **Triplanar / world-aligned UVs** (no stretching on resized blocks).
- **Hard-edge normals + tangents** at block seams.
- **Collision** generation (BVH triangle mesh, or merged box colliders for Jolt).
- **Output Type = Static Mesh**: bake to an optimised StaticMesh asset (the combo exists; only the live
  DynamicMesh path is wired today).

### 8. Greedy meshing (face merging)
- Merge coplanar adjacent faces into larger quads — cleaner topology / fewer tris (today: per-cell face
  culling only).

### 9. Create-category primitives
- Box / Sphere / Cylinder / Cone / Stairs presets in the palette are placeholders — not implemented.

---
Related but separate: **PolyEdit** (face push/pull on arbitrary meshes) is its own tool, Phase 1 done.
