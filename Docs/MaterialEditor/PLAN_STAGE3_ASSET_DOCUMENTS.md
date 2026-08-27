# Stage 3 — the material editor becomes an asset document

Status: **teamlead decomposition, approved by the owner 2026-08-27.** Continues
`STAGE1_END_TO_END.md`, `STAGE1B_SLOT_ROUTE_FIX.md`, `STAGE2_PREVIEW_WINDOW.md`.

The owner's ask, in his words: *«у меня есть панель material preview — я хочу её интегрировать (убрать
из просто панелей) в объекты, где может быть редактирование материалов, как например mesh… повторить
flow Unreal: открывается отдельное окно для редактирования, убирается текущая логика из Details».*

---

## 0. What Stage 2 actually left us, and why that decides the shape of Stage 3

Stage 2 built a **single** preview window with a material combo. UE has no such thing. UE has one
Material Editor **per asset**, opened by double-clicking the asset, and the Details panel holds only
the slot list.

Three facts from the tree decide everything below:

1. **`IPanel` has no subject.** `IPanel.hpp:18-22` takes a name and a visibility flag; `IPanel.hpp:92`
   makes the name `const`. There is no `Open(asset)`, no key, no identity beyond the name.
2. **Two panels with the same name merge into one ImGui window.** Every panel is drawn with
   `ImGui::Begin( PanelDisplayTitle( panel->GetName() ) )` — `EditorLayer.cpp:1357`. `ViewportPanel` is
   the **only** type that escapes this, by baking `"###sceneview" + id` into its title
   (`EditorLayer.cpp:957`, with the comment stating exactly why).
3. **There is no asset→editor routing.** `FileExplorerPanel.cpp:1996-2001` is the whole table: folder,
   `ShaderGraph`, `Scene`. Double-clicking a `.demat` does nothing. What exists instead is three
   hand-wired file-static inboxes (`NodeGraphPanel::RequestOpen` `NodeGraphPanel.cpp:276-281`,
   `MaterialPreviewPanel::RequestPreview` `MaterialPreviewPanel.cpp:63-67`, `Core::SceneOpenRequest`),
   plus `Editor/Core/PanelRequests.hpp:22-65` which carries **a panel name and no payload** and
   therefore cannot express "open *this* asset".

So Stage 3 is not a UI move. **The missing thing is an asset-document seam**, and the material editor
is its first consumer. Build the seam with one real consumer, never on its own — contract §0.

## 1. The scarce resource, named before it is spent

There are **six** renderer slots (`one-scenerenderer-per-frame`, keyed by frame × slot). Today they are
claimed by: the main scene, each extra scene view, the Details preview, and the preview window.

`STAGE2_PREVIEW_WINDOW.md:115-118` records the live defect: **the Details panel's preview never gives
its slot back.** `PreviewViewport::EnsureInit` is lazy, but once shown it holds a slot for the session.
N material-editor windows on top of that is how we run out.

Stage 3 therefore owns two rules, and M1 does not ship without them:

- Every asset-document window creates its viewport on the first frame it draws and destroys it on the
  first frame it does not — the discipline `MaterialPreviewPanel.cpp:102-107` already proves works, and
  the byte-identical closed-cost evidence in `STAGE2_PREVIEW_WINDOW.md:40-48` is the acceptance bar.
- The cap is **explicit and refuses out loud**. Opening a seventh consumer logs the slot census with
  names and refuses to open. A silent fallback here is contract §1.4.

## 2. The tasks

Each task lists the files it owns. Anything outside the list needs my agreement first (contract §1.6).

---

### M1 — the asset-document seam, with the Material Editor as its first consumer

**Delivers:** double-clicking a `.demat` in the asset browser opens a Material Editor window bound to
**that** asset; two different materials give two windows; the same material twice focuses the first;
closing a window releases its scene, renderer and slot.

**Interface first** (contract §2.1), and this is the part to agree with me before writing bodies:

- `Editor/Core/AssetOpenRequest.hpp` — one payload-carrying request `{ AssetHandle, AssetTypeID }`,
  replacing `MaterialPreviewPanel::RequestPreview`. `NodeGraphPanel::RequestOpen` and
  `Core::SceneOpenRequest` are migrated onto it in M4 and not before — one consumer at a time.
- `Editor/Core/AssetEditorRegistry.{hpp,cpp}` — `AssetTypeID → factory`, plus the open-or-focus lookup
  keyed by the subject handle. This is the thing `PanelRequests` could never be, because it carries
  the asset.
- `IAssetEditorPanel : IPanel` — an **immutable subject handle**, and a title built as
  `<name>###assetdoc<uuid>`. The `###` discipline is not a detail; it is fact 2 above, and the
  `ViewportPanel` precedent at `EditorLayer.cpp:957` is the form to copy, not to reinvent.

**Also in M1, because the seam is not finished without them:**

- `MaterialPreviewPanel` — the singleton with the combo — is **deleted**, not left beside the new
  window (contract §5: the old path goes with the change that replaces it). Its two genuinely load-
  bearing pieces move: the lazy create/destroy of `PreviewViewport`, and `InvalidatePipelines`
  (`MaterialPreviewPanel.cpp:156-161`) — pipeline caches are per-`SceneRenderer` and
  `AssetHotReload::PollShaders` only invalidates the main scene's. Losing that reintroduces the
  Stage-1 disease exactly as `STAGE2_PREVIEW_WINDOW.md:26-32` describes.
- `NodeGraphPanel::PublishToPreview` (`NodeGraphPanel.cpp:453-464`) is repointed at the registry.
- `ScenePropertiesPanel`'s preview gains the same release path, fixing the recorded defect.
- `FileExplorerPanel.cpp:1996-2001` gains the `FileType::Material` branch.
- The dead declaration `ScenePropertiesPanel.hpp:36` `DrawMaterialEntity` — declared, never defined,
  never called — goes.

**Owns:** `Editor/Core/AssetOpenRequest.hpp`, `Editor/Core/AssetEditorRegistry.*`,
`Editor/Source/Editor/Panels/IPanel.hpp`, `Editor/Source/Editor/Panels/MaterialEditor/*` (new),
`MaterialPreviewPanel.{hpp,cpp}` (deleted), `EditorLayer.{hpp,cpp}`,
`FileExplorer/FileExplorerPanel.cpp`, `NodeGraph/NodeGraphPanel.cpp` (the publish call only),
`SceneProperties/ScenePropertiesPanel.{hpp,cpp}`.

**Acceptance:**
- Two `.demat` open as two dockable windows; the ImGui ids differ. A test asserts the id relation the
  way `SceneViewIdentity` is asserted today.
- `Desert/Tests/Editor/MaterialPreviewRoute` still passes **in both directions** — it is the guard that
  the window cannot flatter (it fills `MaterialSlots`, the per-slot route, not the override route).
- Closed cost is byte-identical: the sha256 protocol of `STAGE2_PREVIEW_WINDOW.md:40-48`, repeated.
- The slot census refuses the seventh consumer with names and numbers in the log.
- Looked at, and the shot is kept: a `.demat` opened from the browser, its material on the sphere.

---

### M2 — the authoring moves out of Details

**Delivers:** parameters, textures, shader picker, instance parenting live **only** in the Material
Editor window. Details keeps the slot list.

`ComponentWidgets/MaterialsPanelComponent.cpp` is 1094 lines and is the material editor today. What
moves is `DrawShaderPicker` (`:372-414`), `DrawCustomShaderMaterial` (`:416-553`, including the texture
rows at `:468-510` that the Stage-2 window could never do — it skips texture params at
`MaterialPreviewPanel.cpp:258-259`), and the live-edit propagation at `:1042-1074`.

What **stays** in Details, because it is per-entity and not per-material: `HostOf` (`:54-76`), the slot
rows (`DrawSlotRow` `:667-789`) reduced to asset field + swatch + drag-drop + **Edit → opens the
window**, `CreateAndRegisterMaterial` / `…Instance` (`:252-343`), and the runtime-override warning
banner (`:78-116`).

The comment at `MaterialsPanelComponent.cpp:1033-1035` — *"UE opens a separate Material Editor window;
a fold keeps the slot LIST readable without one"* — is the note this task closes. Delete it with the
fold.

**Owns:** `ComponentWidgets/MaterialsPanelComponent.{hpp,cpp}`, `MaterialEditor/*`.
**Blocked by:** M1.

**Acceptance:** the moved code exists once, not twice — a grep for the schema-driven param loop finds
one site. A texture bound in the window shows in the scene without reopening Details. Shot kept.

---

### M3 — Terrain leaves the override route, and the route stops being authored

**Delivers:** `ComponentEditorRegistrations.cpp:114-250` `DrawTerrainMaterialWidget` — the last place
that *authors* `ECS::MaterialComponent` — is replaced by real `.demat` slots. After this,
`MaterialComponent` survives only as (a) the runtime/Lua `setMaterialParam` channel and (b) legacy-scene
compatibility, which is what `MaterialsPanelComponent.cpp:96-98` already claims it is.

The second author, `AssetThumbnailRenderer.cpp:187-192`, is in scope too: it is the exact route that
produced the Stage-1 defect where a thumbnail showed a correct material while the scene showed black.

Terrain has no mesh material slots today — that is the stated reason it went the override way. So this
task carries a real design decision, and it is mine to make before the task starts: **either** Terrain
gains a slot vector like `StaticMeshComponent::MaterialSlots`, **or** it gains a single
`Assets::AssetHandle TerrainMaterial`. I lean to the second — one surface, one material — but I want
the terrain shader's schema in front of me first.

**Owns:** `ComponentEditorRegistrations.cpp`, `ECS/Components.hpp` (terrain component only),
`Widgets/AssetThumbnailRenderer.{hpp,cpp}`, terrain scene migration.
**Blocked by:** M2.

**Acceptance:** old terrain scenes load and render identically — migration, measured against a shot,
not asserted. A grep finds no editor code writing `MaterialComponent::ShaderName`.

---

### M4 — the graph and its material are one document (optional, after the owner sees M1–M3)

UE's Material Editor **is** the graph editor. Ours are two windows joined by a scratch `.demat` whose
only link to the graph is a shader-name string (`NodeGraphPanel.cpp:441`) and a `_Preview` filename
suffix (`:56-59`). Nothing associates an arbitrary `.demat` back to a `.dgraph`.

This is the task that would migrate `NodeGraphPanel::RequestOpen` and `SceneOpenRequest` onto the M1
seam and give the graph a real asset link. **Not scheduled.** It is written down so that M1's interface
is designed knowing it exists, and so nobody claims M1 was under-designed later.

---

## 3. Order, and why it is not a preference

Contract §5: **the shared backbone goes first, by one person.** M1 is that backbone — it owns
`IPanel.hpp` and `EditorLayer.cpp`, the two files every other task would otherwise touch at once. M2
and M3 are sequential after it because both edit the same widget file. **Р3** of the clouds programme
(`Docs/Clouds/PLAN_REALISM_AND_AUTHORING.md`) is a second consumer of the same seam and is blocked by
M1 for the same reason.

Nothing here runs in parallel with anything else here. The parallelism in this fortnight is between
this programme and the clouds one — they share no files.

## 4. Delivery notes specific to Stage 3

- **`ComponentReflection` and `SettingConsumers` will fire on M3**, because the terrain material
  becomes a reflected property. `SettingConsumers` demands that every field name the file that reads
  it, which is contract §1.3 enforced automatically.
- **Deleting `MaterialPreviewPanel` deletes a build entry.** New or removed files ⇒ `premake5 gmake`
  before building; the generated makefiles list files explicitly, so the change silently misses the
  build otherwise. `MaterialPreviewRoute.make` and `Desert/Tests/Editor/MaterialPreviewRoute` keep
  their name — the route they guard is unchanged, only its window moved.
- **Frames, not "it builds".** Every one of M1–M3 changes what is on screen. The Stage-1 defect —
  a preview showing a correct material while the scene showed black — is precisely the class that
  passes tests and fails on sight.
