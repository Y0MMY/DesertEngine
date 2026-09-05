# UMG Designer & the Slate Layout System — capability reference

**Source of truth:** Epic Games/UnrealEngine `release` branch = **UE 5.8.2**. Every `file:line`
below is a path under `Engine/Source/` in that branch unless a different `?ref=` is named.
Anything marked **SECONDARY** comes from docs, not source.

**Correction to the brief:** `FAnchors` is *not* in `Runtime/SlateCore/Public/Layout/Anchors.h`
(that path does not exist). It lives in **`Runtime/Slate/Public/Widgets/Layout/Anchors.h`**, and
`SConstraintCanvas` — the Slate widget that actually implements anchoring — is in
`Runtime/Slate/Public/Widgets/Layout/SConstraintCanvas.h`. Slate, not SlateCore.

---

## 1. The Designer canvas

All paths in this section are under `Engine/Source/Editor/UMGEditor/`.

### 1.1 Editor shell: application modes

Three modes, not two — `Private/BlueprintModes/WidgetBlueprintApplicationModes.cpp:9-12`:
`DesignerMode = "DesignerName"`, `GraphMode = "GraphName"`, `PreviewMode = "PreviewName"`
(`DebugMode` is a deprecated alias of the same string). Labels "Designer" / "Graph" / "Preview"
at `:14-29`. **Preview mode is off by default**, gated by CVar `UMG.EnablePreviewMode` (`:31-41`).
Mode buttons built in `Private/WidgetBlueprintEditorToolbar.cpp:63-144`; the toolbar also carries a
**Widget Reflector** button (`:146-161`).

### 1.2 Designer-mode tab layout

`Private/BlueprintModes/WidgetDesignerApplicationMode.cpp:35-222`, layout name
`"WidgetBlueprintEditor_Designer_Layout_v4_8"` (`:151`):

* left column 15%: **Palette** + **Library** (0.5) over **Hierarchy** + **Bind Widgets** (0.5) — `:52-103`
* right 85%: ToolPalette 5% (closed) / **Designer** 80% (tab-well hidden) / **Details** 35% — `:105-140`
* bottom stack: **Animations**, **Compiler Results** (both closed) — `:141-148`
* `PostActivateMode` registers the Animations **status-bar drawer** (`Ctrl+Shift+Space`) — `:248-281`

Tabs are conditional on project settings `bEnableHierarchyWindow`, `bEnableBindWidgetWindow`,
`bEnablePaletteWindow`, `bEnableLibraryWindow`, `bEnableNavigationSimulationWindow` (`:171-194`).

| Tab | ID | Label | Source |
|---|---|---|---|
| Designer | `"SlatePreview"` | Designer | `Private/TabFactory/DesignerTabSummoner.cpp:12,25` |
| Palette | `"WidgetTemplates"` | Palette | `PaletteTabSummoner.cpp:9,15` |
| Library | `"WidgetLibrary"` | Library | `LibraryTabSummoner.cpp:9,15` |
| Hierarchy | `"SlateHierarchy"` | Hierarchy | `HierarchyTabSummoner.cpp:14,20` |
| Details | `"WidgetDetails"` | Details | `DetailsTabSummoner.cpp:12,18` |
| Animations | `"Animations"` | Animations | `AnimationTabSummoner.cpp:48,56` |
| Bind Widgets | `"BindWidget"` | Bind Widgets | `BindWidgetTabSummoner.cpp:9,15` |
| Navigation Simulation | `"NavigationSimulation"` | Navigation Simulation | `NavigationTabSummoner.cpp:12,18` |

### 1.3 `SDesignSurface` — zoom, pan, grid

`Public/Designer/SDesignSurface.h`, `Private/Designer/SDesignSurface.cpp`.

**31 fixed zoom levels** (`SDesignSurface.cpp:31-66`): 0.150, 0.175, 0.200, 0.225, 0.250, 0.375,
0.500, 0.675, 0.750, 0.875, **1.000 (index 10, label "1:1")**, 1.250, 1.500, 1.750, 2.0, 2.25, 2.5,
2.75, 3.0, 3.25, 3.5, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13. Default index 10 (`:98-101`).
Labels "Zoom -10" … "Zoom 1:1" … "Zoom +20". Over-1:1 zoom is gated behind
`bAllowFullZoomRange` / Ctrl (`ChangeZoomLevel`, `:374-419`; `bRequireControlToOverZoom` defaults
false, `:315`).

Input: wheel = zoom about cursor (`:310-319`); RMB or MMB drag = pan,
`ViewOffset = ViewOffsetStart + (delta/scale)/zoom` (`:295-304`), cursor `GrabHand` (`:196-204`);
RMB + (LMB|MMB|Alt|trackpad) = drag-zoom with `MouseZoomScaling = 0.04` (`:271-294`); trackpad
magnify threshold 0.07 → ±1 level (`:321-338`), scroll gesture pans honouring
`ULevelEditorViewportSettings::ScrollGestureDirectionForOrthoViewports` (`:339-347`).

**Zoom to fit**: `ZoomToFit(bInstantZoom)` sets `bDeferredZoomToExtents` (`:512-516`); an active
timer `HandleZoomToFit` (`:179-194`) interpolates via `ScrollToLocation` (speed 10.0, stops within
1 px, `:439-449`) and picks the largest zoom that fits with `ZoomToFitPadding = (100,100)`
(`ZoomToLocation`, `:451-510`, padding at `:147`).
**Zoom-to-fit on open** is instant: `SDesignerView::Construct` calls `ZoomToFit(true)` —
`Private/Designer/SDesignerView.cpp:397`. Toolbar button (non-instant) `:530-546`, handler
`:3823-3827`.

**Grid**: `PaintBackgroundAsLines` (`SDesignSurface.cpp:558-658`), background brush
`Graph.Panel.SolidBackground`, colours `Graph.Panel.GridLineColor` / `GridRuleColor` /
`GridCenterColor`; cell = `SnapGridSize * GridScaleAmount * zoom`, doubled until ≥ 8 px;
antialiasing from `UEditorStyleSettings::bAntiAliasGrid`. Designer overrides:
`GetSnapGridSize()` = `UWidgetDesignerSettings::GridSnapSize` (`SDesignerView.cpp:1171-1175`),
`GetGraphRulePeriod()` = **10** (`:1177-1180`), `GetGridScaleAmount()` = preview DPI scale
(`:1182-1185`).

**Rulers**: two `SRuler`s (top + left) in an `SGridPanel` (`SDesignerView.cpp:268-294`);
`SetRuling(AbsoluteOrigin, SlateToUnitScale)` ticks in *widget* units (`1/GetPreviewScale()`) and
`SetCursor(TOptional<FVector2D>)` draws a cursor line while hovered (`:2446-2462`).
**There are no user-placed guides** — rulers + grid + snapping only.

**Overlay HUD** (`CreateOverlayUI`, `SDesignerView.cpp:425-772`): zoom text, live cursor position
in widget space (`:3466-3481`), selection dimensions incl. render scale (`:3483-3512`, single
selection only), the designer toolbar, Zoom-To-Fit / aspect-swap / safe-zone-flip buttons,
**Screen Size** and **Screen Fill Rule** combos, W/H numeric entries, a disappearing info bar fed
by `PushDesignerMessage`/`PopDesignerMessage` (`:1187-1218`), device content scale + safe-zone +
resolution text, a **DPI Scale** readout with a Project-Settings shortcut (`:3514-3519`), and a
full-screen tint with corner text for "SIMULATING" (PIE) / "RECORDING" (Sequencer auto-key) /
"SELECTED: `<anim>`" (`:3386-3464`).
Preview stack: `SBorder(preview background) → SBox(preview area) → SDPIScaler → SBox(preview size)`
inside `SZoomPan` (`:308-335`).

### 1.4 Screen size, fill rule, DPI preview, orientation

`EDesignPreviewSizeMode` — `Runtime/UMG/Public/Blueprint/UserWidget.h:246-253`:
`FillScreen`, `Custom`, `CustomOnScreen`, `Desired`, `DesiredOnScreen`.
Stored on the **UUserWidget CDO** as `DesignSizeMode` (`:1535`) with `DesignTimeSize` (`:1532`).
The "Screen Fill Rule" combo enumerates exactly these five via `StaticEnum<>`
(`SDesignerView.cpp:3710-3766`).

Area-vs-size resolution — `Private/WidgetBlueprintEditorUtils.cpp:2227-2274`:

| Mode | Preview *area* | Preview *size* |
|---|---|---|
| `Custom` | `DesignTimeSize` | `DesignTimeSize` (0 falls back to desired) |
| `CustomOnScreen` | screen resolution | `DesignTimeSize` |
| `Desired` | desired | desired |
| `DesiredOnScreen` | screen resolution | desired |
| `FillScreen` | screen resolution | screen resolution |

The area-resize handle is hidden in `Desired` (`SDesignerView.cpp:1131-1145`); W/H spinners appear
only in `Custom`/`CustomOnScreen` (`:3677-3689`); the `*OnScreen` modes align the preview box
top-left instead of filling (`:2381-2391`).

**Where the resolution list comes from — not UMG.** `GetResolutionsMenu` generates
`ULevelEditorPlaySettings::GetCommonResolutionsMenuName()` through `UToolMenus`
(`SDesignerView.cpp:3702-3708`), i.e. the shared PIE "Common Window Sizes" list backed by
`FPlayScreenResolution` and device profiles. `UWidgetDesignerSettings` supplies only the
*default* 1280×720. Selection handler `HandleOnCommonResolutionSelected` (`:3521-3596`) stores
`bCanSwapAspectRatio`, logical W/H, aspect string, device `ProfileName`, `ScaleFactor`; persists to
`GEditorPerProjectIni` section **`"UMGEditor.Designer"`** (`:229`); auto-promotes
`Custom→CustomOnScreen` and `Desired→DesiredOnScreen` (`:3570-3585`); then `RefreshPreview()` so
`PreConstruct` re-runs at the new size. Startup restore: `SetStartupResolution` (`:915-991`).

**Orientation**: a landscape↔portrait swap button (`UMGEditor.OrientPortrait` /
`UMGEditor.OrientLandscape`), enabled only when the resolution has `bCanSwapAspectRatio`
(`:548-564`, `:3768-3780`); `HandleSwapAspectRatioClicked` (`:3829-3895`) also re-runs
`RescaleForMobilePreview` for the device profile. A separate **mirror / flip-safe-zones** button
(`:565-581`, `:3897-3930`).

**Debug resolution overlays**: while dragging the bottom-right area-resize handle
(`SResizeDesignerHandle`, `:92-183`) the grid hides and each `FDebugResolution` is painted as a
coloured rect with a "W x H - Description" label (`:2493-2556`); dragging **snaps** within 10 px
(`:158-168`). `FDebugResolution { int32 Width; int32 Height; FString Description; FLinearColor Color; }`
— `Public/WidgetEditingProjectSettings.h:22-38`, array `DebugResolutions` (`:234-235`).

### 1.5 `UWidgetDesignerSettings` — every property

`Public/Settings/WidgetDesignerSettings.h`, `UCLASS(config=EditorPerProjectUserSettings)`,
`UDeveloperSettings` section **"Widget Designer"** (`Private/Settings/WidgetDesignerSettings.cpp:12,26-34`).

| Property | Type | Editable? / Category | Default (`.cpp:7-22`) | Decl |
|---|---|---|---|---|
| `GridSnapEnabled` | `uint32:1` | EditAnywhere, `GridSnapping`, DisplayName "Enable Grid Snapping" | `true` | `.h:51-52` |
| `GridSnapSize` | `int32` | **config only** (set from the toolbar dropdown) | `4` | `.h:57-58` |
| `bLockToPanelOnDragByDefault` | `bool` | EditAnywhere, `Dragging` | *unset → false* | `.h:63-64` |
| `DefaultPreviewResolution` | `FUintVector2` | EditAnywhere, `Visuals` | `(1280, 720)` | `.h:67-68` |
| `bShowOutlines` | `bool` | EditAnywhere, `Visuals`, DisplayName **"Show Dashed Outlines By Default"** | `true` | `.h:73-74` |
| `bExecutePreConstructEvent` | `bool` | EditAnywhere, `Visuals` | `true` | `.h:80-81` |
| `bRespectLocks` | `bool` | EditAnywhere, `Interaction` | `true` | `.h:87-88` |
| `CreateOnCompile` | `EDisplayOnCompile` | EditAnywhere, `Interaction` | `DoC_ErrorsOrWarnings` | `.h:91-92` |
| `DismissOnCompile` | `EDisplayOnCompile` | EditAnywhere, `Interaction` | `DoC_ErrorsOrWarnings` | `.h:95-96` |
| `Favorites` | `UWidgetPaletteFavorites*` | default subobject | — | `.h:101-102` |

`EDisplayOnCompile { DoC_ErrorsOrWarnings, DoC_ErrorsOnly, DoC_WarningsOnly, DoC_Never }` (`.h:23-30`).
`UWidgetPaletteFavorites` (`Classes/WidgetPaletteFavorites.h:15-34`): `TArray<FString> Favorites`
keyed by template display name + `FOnFavoritesUpdated` so open designers stay in sync.

**Naming corrections vs. common assumption:** the field is `bExecutePreConstructEvent` (not
`bExecutePreConstruct`); there is no `bShowDashedOutlines` field (that is the *display name* of
`bShowOutlines`); there is **no** `bDisableAllDesignerAnimations` and no `PreviewDPIScale` /
"Apply DPI Scale" control in 5.8.

**Related per-project settings** — `Public/WidgetEditingProjectSettings.h` (base of
`UUMGEditorProjectSettings`): class filtering `bShowWidgetsFromEngineContent`,
`bShowWidgetsFromDeveloperContent`, `CategoriesToHide`, `WidgetClassesToHide` (`:144-154`);
designer `bUseWidgetTemplateSelector`, `CommonRootWidgetClasses`, `DefaultRootWidget`,
`bUseEditorConfigPaletteFiltering`, `bUseUserWidgetParentClassViewerSelector` (`:158-180`); window
toggles (`:182-208`); `DefaultWidgetAnimationFrameRate`, `FavoriteWidgetParentClasses`,
`DebugResolutions` (`:223-235`); compiler gates
`EPropertyBindingPermissionLevel { Allow, Prevent, PreventAndWarn, PreventAndError }` (`:40-59`) and
`FWidgetCompilerOptions { bAllowBlueprintTick, bAllowBlueprintPaint, PropertyBindingRule, Rules }`
(`:61-95`) with per-directory overrides (`:99-119`).

### 1.6 Designer toolbar and commands

`Private/Designer/DesignerCommands.cpp:11-22`, context `"WidgetDesigner"`:

| Command | Label | Chord |
|---|---|---|
| `LayoutTransform` | Layout Transform Mode | **W** |
| `RenderTransform` | Render Transform Mode | **E** |
| `LocationGridSnap` | Grid Snap | — |
| `RotationGridSnap` | Rotation Snap | — (declared, unused) |
| `ToggleOutlines` | Show Outlines | **G** |
| `ToggleRespectLocks` | Respect Locks | **L** |
| `ToggleLocalizationPreview` | Toggle Localization Preview | — |

Toolbar sections (`Private/Designer/SDesignerToolBar.cpp:39-109`): **Localization** (game-loc
preview toggle + per-culture radio list from `GetLocalizedCultureNames(Game)` + a "Region &
Language" shortcut, `:189-284`; the resulting flow direction is applied to the preview each Tick,
`SDesignerView.cpp:2399-2410`), **View** (`ToggleOutlines`, `ToggleRespectLocks`,
`ToggleLimitViewportSelection`), **Transform** (Layout / Render;
`ETransformMode::{Layout,Render}`, `Public/IUMGDesigner.h:12-21`), **LocationGridSnap**
(check + combo, snap sizes hard-coded **{1,2,3,4,5,10,15,25}**, `SDesignerToolBar.cpp:130-138`).
Arrow keys nudge by `GridSnapSize` via `UPanelSlot::NudgeByDesigner`
(`SDesignerView.cpp:1910-1961`, `:1968-2007`).

### 1.7 Selection UI, transform handles, anchor medallion

**Outlines** (`DrawSelectionAndHoverOutline`, `SDesignerView.cpp:2100-2185`, painted on a dedicated
`SPaintSurface` effects layer at `:338-345`): selection = 2 px green `(0,1,0)` polyline around the
inflated clipping zone; hover = 2 px azure `(0,0.5,1)` faded by a 0.15 s curve (`:252`,
`:1272-1280`), suppressed when the hovered widget is selected. Extensions paint first (`:2104-2108`).

**Transform handles**: eight `STransformHandle`s at `EExtensionLayoutLocation::{TopLeft…BottomRight}`
with a 10 px outward offset (`:1372-1418`). `STransformHandle`
(`Private/Designer/STransformHandle.{h,cpp}`): brush `UMGEditor.TransformHandle`; visible only if
the widget is not `bHiddenInDesigner` **and** (Layout mode ⇒ slot is a `UCanvasPanelSlot`) or
Render mode (`.cpp:43-73`, `:153-156`). Drag = `FScopedTransaction("Resize Widget")`, delta scaled
by `1/(PreviewScale*Geometry.Scale)`, applied to **both** preview and template (`.cpp:75-151`).
`Resize()` (`.cpp:158-232`) is anchor-aware — stretched axis moves the `Offsets` edge, docked axis
splits movement between position and size using the slot `Alignment`.
`ETransformDirection { TopLeft, TopCenter, TopRight, CenterLeft, CenterRight, BottomLeft,
BottomCenter, BottomRight, MAX }` (`.h:19-36`); `ETransformAction { None, Primary, Secondary }`
(`.h:39-44`, "Primary" when the grab is within 6 px of the corner origin, `:319-331`).

**Extension framework** (`Public/DesignerExtension.h`):
`EExtensionLayoutLocation { RelativeFromParent, TopLeft, TopCenter, TopRight, CenterLeft,
CenterCenter, CenterRight, BottomLeft, BottomCenter, BottomRight }` (`:32-48`);
`FDesignerSurfaceElement { TSharedRef<SWidget> Widget; EExtensionLayoutLocation Location;
TAttribute<FVector2D> Offset, Alignment; }` (`:55-110`); `FDesignerExtension` virtuals
`Initialize`, `Uninitialize`, `CanExtendSelection`, `ExtendSelection`, `PreviewContentCreated`,
`PreviewContentChanged`, `Tick`, `Paint`, `GetExtensionId`, `BeginTransaction`/`EndTransaction`
(`:116-182`). Positioning maths `SDesignerView::GetExtensionPosition` (`:1420-1501`).
Registration `RegisterExtensions()` (`:1573-1591`) + external factories via
`IUMGEditorModule::GetDesignerExtensibilityManager()`.

Per-slot extensions — note these are `F`-prefixed `FDesignerExtension` subclasses emitting plain
`SButton`s, **not** `S`-prefixed widgets:

| Extension | Applies to | Adds |
|---|---|---|
| `FCanvasSlotExtension` | `UCanvasPanelSlot` | the 9-piece **anchor medallion** — `Private/Extensions/CanvasSlotExtension.cpp:97-153` |
| `FVerticalSlotExtension` | `UVerticalBoxSlot` | ↑/↓ buttons → `ShiftChild(index±1)` — `VerticalSlotExtension.cpp:19-90` |
| `FHorizontalSlotExtension` | `UHorizontalBoxSlot` | ←/→ buttons → `ShiftChild` — `HorizontalSlotExtension.cpp:19-90` |
| `FStackBoxSlotExtension` | `UStackBoxSlot` | one directional shift pair — `StackBoxSlotExtension.h:10-23` |
| `FUniformGridSlotExtension` | `UUniformGridSlot` | ←→↑↓ → `SetRow`/`SetColumn(max(v±1,0))` — `UniformGridSlotExtension.cpp:31-129` |
| `FGridSlotExtension` | `UGridSlot` | ←→↑↓ → `SetRow`/`SetColumn` — `GridSlotExtension.cpp:31-129` |

**The anchor medallion** (`Private/Extensions/CanvasSlotExtension.cpp`): nine draggable pieces
`EAnchorWidget::{Center, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight}`,
sizes 16×16 / 32×16 / 16×32 / 24×24, positioned so the gizmo tracks the anchor rectangle in the
parent canvas (`:110-153`, `GetAnchorAlignment` `:250-283`). Brushes
`UMGEditor.AnchorGizmo.<Piece>[.Hovered]` (`:193-218`), tagged `FTagMetaData("AnchorMedallion")`
(`:163`). Piece visibility depends on stretch state — Center only when min == max, Left/Right only
when Y min == max, Top/Bottom only when X min == max (`:220-248`).
Dragging (`:334-555`): transaction "Move Anchor"; message *"Hold [Ctrl] to update widget
position"*; delta is `LocalPositionDelta / CanvasGeometry.LocalSize` (normalised anchor space),
clamped so min ≤ max within [0,1]; **snaps to the nearest 0.1 within 0.1 unless Shift is held**
(`MajorAnchorLine = 0.1f`, `MajorAnchorLineSnapDistance = 0.1f`, `:370-379`); runs
`SaveBaseLayout() → SetLayout → RebaseLayout()` so the widget stays visually still; **Ctrl** zeroes
the relevant offsets (`:498-538`); result written to preview **and** template via
`SetPropertyValue<UCanvasPanelSlot, FAnchorData>("LayoutData")`.
While dragging or hovering the medallion, dashed blue `(0.5, 0.75, 1)` guide lines with
`"%.1f%%"` labels are drawn from the canvas edges to the anchor lines, including span percentage
when stretched (`:328-706`). `PaintCollisionLines` (sibling edge snapping, `SnapDistance = 7.0`) is
present but **`#if 0`-disabled** (`:708-800`).

The **anchor *presets* grid** is in the Details panel, not on the surface — see §2.7.

### 1.8 Palette, Library, widget templates

`FWidgetTemplate` (abstract) — `Public/WidgetTemplate.h:25-57`: `FText Name`, pure-virtual
`GetCategory()`, `Create(UWidgetTree*)`, `GetToolTip()`; virtual `GetIcon()`, `GetFilterStrings()`,
`OnDoubleClicked()`.
`FWidgetTemplateClass` (`Public/Templates/WidgetTemplateClass.h:19-73`) wraps a
`TSubclassOf<UWidget>` **or** an unloaded `FAssetData`.
`FWidgetTemplateBlueprintClass` (`.../WidgetTemplateBlueprintClass.h:21-62`) handles
`UWidgetBlueprint`-generated classes even while unloaded; double-click opens the widget blueprint.
`FWidgetTemplateImageClass` drops a texture as a `UImage`.

Population — `Private/Palette/SPaletteViewModel.cpp:218-358`: (a) loaded `UWidget` subclasses via
`GetDerivedClasses`, (b) widget-class assets (loaded **and** unloaded) via
`UAssetRegistryHelpers::GetDerivedClassAssetData`, (c) optionally all `UBlueprintGeneratedClass`
assets when `bUseEditorConfigPaletteFiltering` (noted as >500 ms in a large project). Rejections:
the class being edited, `CLASS_Hidden|CLASS_HideDropDown`,
`FWidgetBlueprintEditorUtils::IsUsableWidgetClass`.
Category = `UWidget::GetPaletteCategory()` / `UUserWidget::PaletteCategory`; `CategoriesToHide`
entries dropped (`:373-389`). `BuildWidgetList()` (`:164-216`) sorts categories and children
alphabetically, **inserts "Favorites" at index 0**, and force-moves "Advanced" to the end.
Rebuild triggers at `:116-129`.
Palette widget (`Private/Palette/SPaletteView.cpp`): "Search Palette" box + `STreeView`
(SingleToggle) with `TreeFilterHandler` (`:121-183`); rows carry a star toggle (style
`UMGEditor.Palette.FavoriteToggleStyle`, visible when favourited or hovered), class icon and
highlighted name (`:76-114`). Expansion state persists to `GEditorPerProjectIni` section
`WidgetTemplatesExpanded` (`:287-308`). Drag out creates
`FWidgetTemplateDragDropOp::New(Template)` (`SPaletteViewModel.cpp:57-71`;
`Public/DragDrop/WidgetTemplateDragDropOp.h:19-29`).
The **Library** tab (`Private/Library/SLibraryView.cpp`) is an asset-browser variant with the same
favourites plus **View Type** (Tiles/List) and **Thumbnails / Thumbnail Size** options (`:141-202`).

### 1.9 Drag-and-drop authoring

**Palette → canvas** (`SDesignerView.cpp:2666-2707`, `:2742-2927`, `:3219-3283`):

* Hit-testing uses a **private `FHittestGrid`** rebuilt every Tick from an `ArrangeChildren` walk
  (`:2025-2090`). Widgets that are `!IsVisibleInDesigner()` or locked-while-respecting-locks are
  **excluded from the grid** — that is exactly how the eye and lock toggles work.
* Drop targets are filtered to `UPanelWidget`; `FindWidgetUnderCursor` walks the bubble path
  outward, skips drop previews, and additionally resolves which **NamedSlot** of a nested
  `UUserWidget` is under the cursor (`:1617-1684`).
* **Live drop preview**: a real widget is instantiated into the widget tree on every `OnDragOver`,
  given the current designer flags, and parented — then removed and renamed into the transient
  package on drag-leave (`FDropPreview` `:338-345`, `ClearDropPreviews` `:1508-1529`). The whole
  preview runs in an `FScopedTransaction` that is **cancelled** when `bIsPreview` (`:2900-2903`).
* Empty tree ⇒ the drop becomes `WidgetTree->RootWidget` (`:2811-2839`); otherwise
  `Parent->AddChild(Widget)` then `Slot->DragDropPreviewByDesigner(LocalPosition, GridSnap, GridSnap)`
  (`:2841-2891`).
* Refusals set `EMouseCursor::SlashedCircle` (`:2805-2810`, `:2878-2889`).

**Moving existing widgets** (`OnDragDetected` `:2558-2639`, `MoveWidgets` `:2929-3217`):
drag starts only in **Layout** mode and only if the root is not selected (`:1813-1838`); in
**Render** mode dragging edits `RenderTransform.Translation` directly (`:1839-1873`).
`FSelectedWidgetDragDropOp::FItem` (`Private/DragDrop/SelectedWidgetDragDropOp.h:37-56`) carries
`ExportedSlotProperties` (text-exported slot state, re-imported into the new slot so layout
survives reparenting), `Template`, `Preview`, `bStayingInParent`, `ParentWidget`, `DraggedOffset`.
Modifiers: **Alt** breaks the "stay in parent" lock (`:2954-2964`); **Shift** locks to the dominant
axis and disables grid snap on the other (`:3070-3092`). Canvas slots use
`SaveBaseLayout()/SetDesiredPosition()/RebaseLayout()` (labelled "HACK UMG" at `:3129-3134`).
Cross-blueprint moves call `UWidgetTree::TryMoveWidgetToNewTree` (`:3032-3053`).
`bLockToPanelOnDragByDefault` seeds `bStayingInParent`.

**Hierarchy drag/reparent** (`Private/Hierarchy/SHierarchyViewItem.cpp:149-370`): one function
handles both hover-feedback and commit; `EItemDropZone::Above/BelowItem` becomes "drop onto parent
at index N" (`:167-186`). Rejections with `Graph.ConnectorFeedback.Error`: onto itself,
`CanAddToParent` failure, *"Widget can't have multiple children."*, *"Can't make widget a child of
its children."*, *"This would cause a circular reference."* (`:475-496`). Viewport→Hierarchy drags
are **not** supported (`:162-165`). Rows **auto-expand after 0.3 s** of hover (`:1554-1573`).
Named-slot drops require an empty slot (`:790-959`).

### 1.10 Hierarchy panel (widget outliner)

`Private/Hierarchy/SHierarchyView.{h,cpp}`, `SHierarchyViewItem.{h,cpp}`. "Search Widgets" box +
multi-select `STreeView` (`SHierarchyView.cpp:52-83`).
Model: `FHierarchyModel` → `FHierarchyRoot` (bold `[BlueprintName]` row), `FHierarchyWidget`,
`FNamedSlotModelBase` → `FNamedSlotModel` / `FNamedSlotModelSubclass`.
`FHierarchyRoot::GetChildren` (`SHierarchyViewItem.cpp:605-633`) adds the root widget plus each
inherited available named slot; `FHierarchyWidget::GetChildren` (`:1262-1292`) adds the widget's own
named slots then its panel children. Selecting the root selects the **preview UUserWidget**
(`:635-648`).

Row contents left→right (`:1345-1472`): class icon; `SInlineEditableTextBlock` name **bold when the
widget is a named variable** (`:1180-1194`); a **navigation-override** glyph
(`FEditorFontGlyphs::Arrows`); a **flow-direction-override** glyph (`Exchange`); a **lock** toggle
(recursive by default, **Shift** = this widget only, `:1423-1448`); an **eye** toggle writing
`bHiddenInDesigner` on template **and** preview (`:1450-1469`).
Rename via `FGenericCommands::Rename` (`SHierarchyView.cpp:46-50`), blocked on locked widgets
(`:1323-1326`). Hover syncs both ways with the designer (`:1302-1315`). Expansion persists in
`bExpandedInDesigner`.

**Context menu** (shared with the designer RMB) —
`Private/WidgetBlueprintEditorUtils.cpp:201-273`: Cut, Copy, Paste, Duplicate, Delete, a **Find
References** submenu, Rename; then "Edit Widget Blueprint…", **"Wrap With…"** and (single
selection) **"Replace With…"**, both hidden if anything selected is locked.
`BuildWrapWithMenu` (`:642-689`) lists every usable non-hidden `UPanelWidget` subclass →
`WrapWidgets`. `BuildReplaceWithMenu` (`:691-806`) offers "Replace With `<palette selection>`",
"Replace With Child" (panel with exactly one child), "Replace With '`<NamedSlot>`'" per filled
named slot, then every multi-child panel class; uses
`EReplaceWidgetNamingMethod::MaintainNameAndReferences` and warns when variables are referenced in
the graph. Paste position comes from the RMB hit point, or from the cursor / selection + (25,25) for
keyboard paste (`SDesignerView.cpp:1910-1930`).

### 1.11 Details panel

`Private/Details/SWidgetDetailsView.cpp`.

Header area (`:90-207`): a **Category** row (root/CDO selection only) writing
`UUserWidget::PaletteCategory` on the CDO *and* `UWidgetBlueprint::PaletteCategory`, then forcing a
structural recompile (`:103-135`, `:441-471`); a **Name** row with live validation (`:137-171`,
`:513-604`); an **"Is Variable"** checkbox (only when `bEnableMakeVariable`, tri-state across
multi-selection, `:176`, `:606-660`); a class source link (`:311-322`); a `SUIComponentView` when
`bEnableUIComponentsProperty` (`:187-198`). Everything is disabled if any selected widget is locked
(`:366-379`).

Property view: `HideNameArea`, `bHideSelectionTip`, `NotifyHook = this` (`:49-54`); a Sequencer
**keyframe handler** `FUMGDetailKeyframeHandler` (`:56-61`); the **Bind dropdown** extension handler
`FDetailWidgetExtensionHandler` (`:63-65`); EditDefaultsOnly/EditInstanceOnly semantics resolved
against "is the CDO selected" (`:386-402`).
**The Details panel edits the *preview*** and migrates back to the template in
`NotifyPreChange`/`NotifyPostChange` via `FWidgetBlueprintEditor::MigrateFromChain`, skipped while
Sequencer auto-key is on; properties tagged **`DesignerRebuild`** force
`InvalidatePreview(bViewOnly=true)` (`:664-704`).
Registered customizations (`:239-268`): class layout `FBlueprintWidgetCustomization` for `UWidget`;
type layouts for `Widget`, `WidgetChild`, `WidgetNavigation`, **`PanelSlot` → `FCanvasSlotCustomization`**,
`EHorizontalAlignment`, `EVerticalAlignment`, `SlateChildSize`, `SlateBrush`, `SlateFontInfo`,
`ETextJustify`.

**Slot category injection** — `Private/Customizations/UMGDetailCustomizations.cpp:987-1041`: when
all selected widgets share one slot class the "Layout" category is retitled
**`Slot (<SlotClassDisplayName>)`** with `ECategoryPriority::TypeSpecific` (`:1028-1036`);
"Localization" is demoted to `ECategoryPriority::Uncommon` (`:992`).

**Events** (`:1043-1068`): an `FDelegateProperty` tagged `IsBindableEvent` (metadata, or a name
ending in `"Event"`, `:106-120`) gets a row with a `GraphEditor.Event_16x` icon and a
reset-to-default that *removes the binding* (`:784-863`). An `FMulticastDelegateProperty` gets an
**"Events"** category row with a `+` / jump-to-node button (`:891-985`); if the widget is not a
variable a single row explains *"To see available events, enable the Is Variable setting for this
widget."* (`:974-984`).

**The Bind dropdown and its deprecation** — `Private/Details/DetailWidgetExtensionHandler.cpp`:
`IsPropertyExtendable` (`:79-124`) requires exactly one outer object, never the CDO, and either a
matching `<Prop>Delegate` `FDelegateProperty` or a registered `IPropertyBindingExtension`.
`ShouldShowOldBindingWidget` (`:48-76`) additionally requires
`UWidgetBlueprint::ArePropertyBindingsAllowed()` — when bindings are disallowed the button still
appears if `Bindings.Num() > 0` so existing ones can be removed. This is the legacy-binding
deprecation path, governed by `EPropertyBindingPermissionLevel`.
`FDelegateEditorBinding { ObjectName, PropertyName, FunctionName, SourceProperty, SourcePath,
MemberGuid, EBindingKind Kind }` — `Public/WidgetBlueprint.h:128-174`; equality is
object+property, so **one binding per property**.

### 1.12 Design-time semantics: preview instance vs CDO

`FWidgetBlueprintEditor::GetCurrentDesignerFlags()` — `Private/WidgetBlueprintEditor.cpp:2005-2023`:
always `Designing`; `| ShowOutline` if `bShowDashedOutlines`; `| ExecutePreConstruct` if
`UWidgetDesignerSettings::bExecutePreConstructEvent`. **Never `Previewing`.**
`Previewing` is used only by Preview mode — `Private/Preview/SWidgetPreview.cpp:32` sets
`EWidgetDesignFlags::Previewing` *without* `Designing`, so `IsDesignTime()` is false there and
bindings/animations behave like runtime.

**The two-object model** is the single most important structural fact:

* the **template** tree is `UWidgetBlueprint::WidgetTree` (serialised authoring data);
* the **preview** is a live `UUserWidget` instance created into a private `FPreviewScene` world by
  `FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint`
  (`Private/WidgetBlueprintEditor.cpp:1905-1947`);
* `FWidgetReference` (`Public/WidgetReference.h`) pairs them: `GetTemplate()` / `GetPreview()` /
  `GetPreviewSlate()`. **Every** designer edit writes to *both* — transform handles
  (`STransformHandle.cpp:134-146`), anchors (`CanvasSlotExtension.cpp:540-546`), nudge
  (`SDesignerView.cpp:1989-1991`), is-variable (`SWidgetDetailsView.cpp:633-655`), visibility
  (`SHierarchyViewItem.h:277-285`). The Details panel is the exception (edit preview → migrate).
* screen-size / DPI / fill-mode live on the **generated class CDO**
  (`SDesignerView.cpp:3691-3700`), so changing them calls
  `MarkDesignModifed(/*bRequiresRecompile*/ false)`.
* all designers jettison their preview on **any** widget-BP recompile
  (`OnPreviewNeedsRecreation`, `:1593-1608`), because widget BPs nest.
* `AddPostDesignerLayoutAction` queues work run in `SDesignerView::Tick` after geometry caching
  (`:2419-2432`).

**There is no "play in designer" in 5.8** — the designer only tints "SIMULATING" when PIE runs
elsewhere (`:3386-3444`). Preview mode (`Private/Preview/SWidgetPreview.cpp`) is the separate
runtime-like instance.

### 1.13 Animations tab and Sequencer coupling

Animations tab (`Private/TabFactory/AnimationTabSummoner.cpp`) plus a status-bar drawer
(`WidgetAnimSequencerDrawerID`, `Ctrl+Shift+Space`; command `FUMGEditorCommands::OpenAnimDrawer`,
`Private/UMGEditorActions.cpp:24-29`).
`FSequencerCommands::ToggleLimitViewportSelection` is bound in the designer
(`SDesignerView.cpp:844-849`): when enabled only objects bound in the current animation are
selectable (`:3937-3953`, `:3983-4073`). The designer border turns red "RECORDING" when
`Sequencer->GetAutoChangeMode() != None` and blue "SELECTED: `<anim>`" when an animation is focused
(`:3386-3464`). `K2Node_WidgetAnimationEvent` provides animation events in the graph.

### 1.14 `UWidgetBlueprint` — designer-relevant properties

`Public/WidgetBlueprint.h:224-380`: `TArray<FDelegateEditorBinding> Bindings` (:225),
`TArray<UWidgetAnimation*> Animations` (:228), `TMap<FName, FGuid> WidgetVariableNameToGuidMap`
(:238, feeds `OnVariableAdded/Renamed/Removed` at :257-259 for rename-safe references),
`FString PaletteCategory` (`AssetRegistrySearchable`, :246 — a mirror of the CDO value so unloaded
assets can be categorised), `bool bCanCallInitializedWithoutPlayerContext` (:254),
`EWidgetTickFrequency TickFrequency` / `EWidgetCompileTimeTickPrediction TickPrediction` /
`TickPredictionReason` / `PropertyBindings` (all `AssetRegistrySearchable` diagnostics, :348-370),
and thumbnail settings `EThumbnailPreviewSizeMode ThumbnailSizeMode
{ MatchDesignerMode, FillScreen, Custom, Desired }` (:185-192), `ThumbnailCustomSize`,
`ThumbnailImage` (:372-379).
**Not** on the blueprint: `DesignTimeSize`, `DesignSizeMode`, the real `PaletteCategory` and
`PreviewBackground` all live on the **UUserWidget CDO** (`UserWidget.h:1528-1548`);
`PreviewBackground` is the canvas backdrop (`SDesignerView.cpp:1147-1159`).

### 1.15 Extensibility seams worth mirroring

`Editor/UMGEditor/Public/`: `IUMGDesigner.h` (what the designer exposes to extensions —
`GetPreviewScale`, `GetSelectedWidget(s)`, `GetTransformMode`, `GetDesignerGeometry`,
`GetWidgetGeometry`, `GetWidgetParentGeometry`, `MakeGeometryWindowLocal`, `MarkDesignModifed`,
`Push/PopDesignerMessage`); `DesignerExtension.h`; `IHasDesignerExtensibility.h`;
`IHasWidgetContextMenuExtensibility.h`; `IHasPropertyBindingExtensibility.h`;
`IHasWidgetDragDropExtensibility.h`; `IHasClipboardExtensibility.h`;
`Customizations/IBlueprintWidgetCustomizationExtender.h`; `WidgetBlueprintExtension.h`;
`WidgetBlueprintOperationUtils.h` (the transactional primitives: `AddWidget`, `MoveWidget`,
`WrapWidgets`, `ReplaceWidgetsWithTemplateClass`, `ReplaceWidgetWithChild`,
`ReplaceWidgetWithNamedSlot`, `RenameWidget`, `ToggleWidgetAsVariable`, `BindToEventProperty`,
`CanAddToParent`, `IsParentChildCycleFree`).

---

## 2. Anchors, in full

### 2.1 `FAnchors`

`Runtime/Slate/Public/Widgets/Layout/Anchors.h:13`

| Field | Type | Default | Meaning |
|---|---|---|---|
| `Minimum` | `FVector2D` | `(0,0)` | Normalised left+top anchor, in **parent fraction** 0..1 |
| `Maximum` | `FVector2D` | `(0,0)` | Normalised right+bottom anchor |

Constructors: uniform (`FAnchors(f)`), horizontal/vertical pair, and 4-arg
`(MinX,MinY,MaxX,MaxY)` — `Anchors.h:32-53`.

Two predicates drive the entire authoring UX:

```
bool IsStretchedVertical()   const { return Minimum.Y != Maximum.Y; }   // Anchors.h:78
bool IsStretchedHorizontal() const { return Minimum.X != Maximum.X; }   // Anchors.h:81
```

An anchor is therefore **a proportional rectangle of the parent canvas**, not a point. When min ==
max on an axis, the anchor degenerates to a line/point on that axis and the widget is "docked";
when min != max, the anchor spans a proportional region and the widget is "stretched" across it.

### 2.2 `FAnchorData`

`Runtime/UMG/Public/Components/CanvasPanelSlot.h:17`

| Field | Type | Default | Meaning |
|---|---|---|---|
| `Offsets` | `FMargin` | `(0,0,0,0)` | **Overloaded**: see §2.4 |
| `Anchors` | `FAnchors` | `(0,0,0,0)` | as above |
| `Alignment` | `FVector2D` | `(0,0)` | *pivot* of the widget, `(0,0)` = upper-left, `(1,1)` = lower-right (`CanvasPanelSlot.h:37-38`) |

### 2.3 `UCanvasPanelSlot`

`Runtime/UMG/Public/Components/CanvasPanelSlot.h:66`

| Property | Type | Notes |
|---|---|---|
| `LayoutData` | `FAnchorData` | `CanvasPanelSlot.h:75`; direct access deprecated 5.1, use `GetLayout`/`SetLayout` |
| `bAutoSize` | `bool` | `CanvasPanelSlot.h:80`; **shown in the Details panel as "Size To Content"**, `AdvancedDisplay` |
| `ZOrder` | `int32` | `CanvasPanelSlot.h:85`; higher = drawn later/on top |

Blueprint-exposed setters: `SetLayout`, `SetPosition`, `SetSize`, `SetOffsets`, `SetAnchors`,
`SetAlignment`, `SetAutoSize`, `SetZOrder`, plus `SetMinimum`/`SetMaximum`
(`CanvasPanelSlot.h:96-166`).

### 2.4 The stretched-axis rule (why Details shows Position/Size **or** Offsets)

The `Offsets` margin is reinterpreted **per axis** depending on whether that axis is stretched.
The Details panel relabels the four scalar fields at runtime via
`FCanvasSlotCustomization::GetOffsetLabel` — `Editor/UMGEditor/Private/Customizations/CanvasSlotCustomization.cpp:327-349`:

```
const bool bStretching =
    ( Orientation == Orient_Horizontal && AnchorData->Anchors.IsStretchedHorizontal() ) ||
    ( Orientation == Orient_Vertical   && AnchorData->Anchors.IsStretchedVertical()   );
return bStretching ? StretchingLabel : NonStretchingLabel;
```

Label bindings — `CanvasSlotCustomization.cpp:296-299`:

| `FMargin` field | Axis | Not stretched | Stretched |
|---|---|---|---|
| `Offsets.Left` | horizontal | **Position X** | **Offset Left** |
| `Offsets.Top` | vertical | **Position Y** | **Offset Top** |
| `Offsets.Right` | horizontal | **Size X** | **Offset Right** |
| `Offsets.Bottom` | vertical | **Size Y** | **Offset Bottom** |

So the two axes are **independent**: `Anchors = (0, 0, 1, 0)` (Top/Fill) yields
`Offset Left / Position Y / Offset Right / Size Y`. There is no single "mode" for the slot.

### 2.5 The arrange maths

`Runtime/Slate/Private/Widgets/Layout/SConstraintCanvas.cpp:210-312`
(`ArrangeLayeredChildren`, which `OnArrangeChildren` delegates to at :204-208):

```
AnchorPixels = FMargin( Anchors.Min.X * ParentW, Anchors.Min.Y * ParentH,
                        Anchors.Max.X * ParentW, Anchors.Max.Y * ParentH );   // :240-244
bIsHorizontalStretch = Anchors.Minimum.X != Anchors.Maximum.X;                // :246
SlotSize        = FVector2D(Offset.Right, Offset.Bottom);                     // :249
Size            = AutoSize ? Widget->GetDesiredSize() : SlotSize;             // :251
AlignmentOffset = Size * Alignment;                                           // :254

// horizontal
if (bIsHorizontalStretch) {                                                   // :260-264
    LocalPosition.X = AnchorPixels.Left + Offset.Left;
    LocalSize.X     = AnchorPixels.Right - LocalPosition.X - Offset.Right;
} else {                                                                      // :265-269
    LocalPosition.X = AnchorPixels.Left + Offset.Left - AlignmentOffset.X;
    LocalSize.X     = Size.X;
}
// vertical is the exact mirror at :272-281
```

Key consequences to replicate:

* In the **stretched** case, `Offset.Left` / `Offset.Right` are *insets from the two anchor
  lines*, and `Alignment` is **ignored on that axis**.
* In the **docked** case, `Offset.Right` / `Offset.Bottom` *are the size*, and `Alignment`
  subtracts `Size * Alignment` from the position — i.e. alignment is a **pivot**, applied to the
  widget's own box, whereas the anchor is a **reference frame in the parent**. Anchor
  `(1,1,1,1)` + Alignment `(1,1)` = bottom-right corner of the widget pinned to bottom-right of
  parent.
* `bAutoSize` ("Size To Content") replaces `SlotSize` with the child's desired size on **both**
  axes; on a stretched axis it has no effect because the stretched branch never reads `Size`.

`ComputeDesiredSize` for the canvas (`SConstraintCanvas.cpp:370-384`) only counts a child's offset
toward the canvas's own desired size when that child is **docked at 0 or 1** on that axis:

```
bIsDockedHorizontally = (Anchors.Min.X == Anchors.Max.X) && (Anchors.Min.X == 0 || Anchors.Min.X == 1);
FinalDesiredSize.X = Max(FinalDesiredSize.X, Size.X + (bIsDockedHorizontally ? Abs(Offset.Left) : 0));
```

### 2.6 Z-order and batching

`SConstraintCanvas` sorts slots by `ZOrder` on insertion (`SConstraintCanvas.cpp:114-120`,
`:140-148`). The project setting `USlateSettings::bExplicitCanvasChildZOrder`
(`Runtime/Slate/Public/SlateSettings.h:24-25`, config) changes the paint model: when **true**,
children with equal Z-order share a paint layer (batchable); when false every child gets its own
layer (`SConstraintCanvas.cpp:218-308`).

### 2.7 The anchor presets menu — all 16

Built in `FCanvasSlotCustomization::CustomizeAnchors`
(`CanvasSlotCustomization.cpp:351-535`), each entry an `SAnchorPreviewWidget` with a literal
`FAnchors`:

**3×3 point grid** (`CanvasSlotCustomization.cpp:407-449`)

| Preset | `FAnchors(MinX,MinY,MaxX,MaxY)` |
|---|---|
| Top/Left | `(0, 0, 0, 0)` |
| Top/Center | `(0.5, 0, 0.5, 0)` |
| Top/Right | `(1, 0, 1, 0)` |
| Center/Left | `(0, 0.5, 0, 0.5)` |
| Center/Center | `(0.5, 0.5, 0.5, 0.5)` |
| Center/Right | `(1, 0.5, 1, 0.5)` |
| Bottom/Left | `(0, 1, 0, 1)` |
| Bottom/Center | `(0.5, 1, 0.5, 1)` |
| Bottom/Right | `(1, 1, 1, 1)` |

**Horizontal fills** (`:461-471`)

| Preset | `FAnchors` |
|---|---|
| Top/Fill | `(0, 0, 1, 0)` |
| Center/Fill | `(0, 0.5, 1, 0.5)` |
| Bottom/Fill | `(0, 1, 1, 1)` |

**Vertical fills** (`:486-498`)

| Preset | `FAnchors` |
|---|---|
| Fill/Left | `(0, 0, 0, 1)` |
| Fill/Center | `(0.5, 0, 0.5, 1)` |
| Fill/Right | `(1, 0, 1, 1)` |

**Full fill** (`:505`): Fill/Fill `(0, 0, 1, 1)`.

**Modifier behaviour when a preset is clicked** — `SAnchorPreviewWidget::OnAnchorClicked`
(`CanvasSlotCustomization.cpp:176-208`):

* plain click → writes `Anchors` only (`:185-186`), inside an
  `FScopedTransaction("Changed Anchors")` (`:182`).
* **Shift** held → also sets `Alignment` to
  `(IsStretchedHorizontal ? 0 : Minimum.X, IsStretchedVertical ? 0 : Minimum.Y)` (`:190-193`).
  Tooltip: *"Hold Shift to update the alignment to match."* (`:522`).
* **Ctrl** held → also rewrites `Offsets` to
  `(Left=0, Top=0, Right = stretchedH ? 0 : Offsets.Right, Bottom = stretchedV ? 0 : Offsets.Bottom)`
  (`:199-204`). Tooltip: *"Hold Ctrl to update the position to match."* (`:528`).

The little animated preview inside each preset button is itself an `SConstraintCanvas` fed the
preset's own anchors, with `Offset(0,0, stretchedH?0:15, stretchedV?0:15)` and
`Alignment(stretchedH?0:Min.X, stretchedV?0:Min.Y)` — `CanvasSlotCustomization.cpp:114-119`.
Brushes: `UMGEditor.AnchorGrid` background, `UMGEditor.AnchoredWidget` marker (`:94`, `:119`).

### 2.8 Anchor rebasing (position preservation when anchors change)

`UCanvasPanelSlot::SaveBaseLayout()` / `RebaseLayout(bool PreserveSize = true)`
(`CanvasPanelSlot.h:186-191`, implementation `Runtime/UMG/Private/Components/CanvasPanelSlot.cpp:379+`).
`PreEditGeometry` and `PreEditLayoutData` are captured in `PreEditChange`
(`CanvasPanelSlot.h:198-199`), and on `PostEditChangeChainProperty` the offsets are recomputed so
the widget stays put on screen:

* docked→docked: `Offsets.Left = LeftTopDelta.X + AlignmentOffset.X` (`CanvasPanelSlot.cpp:432`)
* stretched→docked: `Offsets.Left = LeftTopDelta.X + AlignmentOffset.X`, `Offsets.Right = PreEditGeometry.Size.X` (`:414-415`) — i.e. the stretch inset becomes an explicit size
* docked→stretched with `PreserveSize`: `Offsets.Left = LeftTopDelta.X`,
  `Offsets.Right = AnchorPositions.Right - (AnchorPositions.Left + Offsets.Left + PreEditGeometry.Size.X)` (`:426-427`)
* docked→stretched without `PreserveSize`: both zeroed (`:420-421`)

Designer-only slot hooks on the base class (`UPanelSlot`), overridden by `UCanvasPanelSlot`:
`NudgeByDesigner(NudgeDirection, GridSnapSize)` (`CanvasPanelSlot.h:89`, impl `.cpp:44-86` — snaps
by integer modulo and **shrinks the size by the nudge on a stretched axis** at `:65-72`),
`DragDropPreviewByDesigner(LocalCursorPosition, XGridSnapSize, YGridSnapSize)` (`.h:90`,
impl `.cpp:88-120` — does a `SlatePrepass()` on the dragged widget and clamps its drop size to a
minimum of **100 × 40** at `.cpp:98`), and `SynchronizeFromTemplate` (`.h:91`).

---

## 3. Every layout panel and its slot

### 3.0 The slot base

`UPanelSlot` (`Runtime/UMG/Public/Components/PanelSlot.h:18-22`) carries only `Parent`
(`UPanelWidget`) and `Content` (`UWidget`). Everything else is per-panel.
`UPanelWidget` (`PanelWidget.h`) provides `AddChild`/`RemoveChild`/`GetChildAt`/`GetChildIndex`/
`HasChild`/`ClearChildren` (`:27-122`), `ReplaceChild` (`:103`), `GetSlotClass()` (`:167`),
`LockToPanelOnDrag()` (`:142`) and the flag `bCanHaveMultipleChildren` (`:190`).
`UContentWidget` (`ContentWidget.h`) is the single-child variant.

Alignment enums are shared by every slot —
`Runtime/SlateCore/Public/Types/SlateEnums.h:173-206`:
`EHorizontalAlignment { HAlign_Fill, HAlign_Left, HAlign_Center, HAlign_Right }`,
`EVerticalAlignment { VAlign_Fill, VAlign_Top, VAlign_Center, VAlign_Bottom }`.

### 3.1 CanvasPanel — `UCanvasPanel` / `UCanvasPanelSlot`

`CanvasPanel.h`, slot detailed in §2. Panel itself exposes **no** UPROPERTYs; all layout is in
the slot. Backed by `SConstraintCanvas`.

### 3.2 Overlay — `UOverlay` / `UOverlaySlot`

Panel: no properties (`Overlay.h`). Slot (`OverlaySlot.h:31-42`):
`Padding` (`FMargin`), `HorizontalAlignment`, `VerticalAlignment`. Children stack in add order.

### 3.3 HorizontalBox — `UHorizontalBox` / `UHorizontalBoxSlot`

Panel: no properties (`HorizontalBox.h:20-53`). Slot (`HorizontalBoxSlot.h:27-41`):
`Size` (`FSlateChildSize`), `Padding`, `HorizontalAlignment`, `VerticalAlignment`.

### 3.4 VerticalBox — `UVerticalBox` / `UVerticalBoxSlot`

Identical set; `Size` carries `meta=(DisplayAfter="Padding")` (`VerticalBoxSlot.h:25-41`).

### 3.5 StackBox — `UStackBox` / `UStackBoxSlot`  *(new in UE 5.1)*

Panel (`StackBox.h:30-31`): `Orientation` (`TEnumAsByte<EOrientation>`) — a single class that is
either an H- or V-box. Slot (`StackBoxSlot.h:23-36`): `Padding`,
`Size = FSlateChildSize(ESlateSizeRule::Automatic)`, `HorizontalAlignment = HAlign_Fill`,
`VerticalAlignment = VAlign_Fill` (note the defaults differ from H/VBox).
Verified absent at `?ref=5.0.3-release`, present at `?ref=5.1.1-release`.

### 3.6 ScrollBox — `UScrollBox` / `UScrollBoxSlot`

Slot (`ScrollBoxSlot.h:24-42`): `Size` (`FSlateChildSize`, `DisplayAfter="Padding"`), `Padding`,
`HorizontalAlignment`, `VerticalAlignment` (the latter three are `BlueprintReadOnly`).

Panel (`ScrollBox.h`), an unusually large surface:

| Property | Type | Line |
|---|---|---|
| `WidgetStyle` | `FScrollBoxStyle` (DisplayName "Style") | :55 |
| `WidgetBarStyle` | `FScrollBarStyle` (DisplayName "Bar Style") | :60 |
| `Orientation` | `TEnumAsByte<EOrientation>` | :65 |
| `ScrollBarVisibility` | `ESlateVisibility` | :70 |
| `ConsumeMouseWheel` | `EConsumeMouseWheel` | :75 |
| `ScrollbarThickness` | `FVector2D` | :80 |
| `ScrollbarPadding` | `FMargin` | :85 |
| `AlwaysShowScrollbar` | `bool` | :90 |
| `AlwaysShowScrollbarTrack` | `bool` | :95 |
| `AllowOverscroll` | `bool` | :100 |
| `BackPadScrolling` | `bool` | :105 |
| `FrontPadScrolling` | `bool` | :110 |
| `bAnimateWheelScrolling` | `bool` = false | :115 |
| `NavigationDestination` | `EDescendantScrollDestination` | :120 |
| `NavigationScrollPadding` | `float` | :128 |
| `ScrollWhenFocusChanges` | `EScrollWhenFocusChanges` | :133 |
| `bAllowRightClickDragScrolling` | `bool` | :138 |
| `WheelScrollMultiplier` | `float` = 1.0 | :143 |
| `ScrollAnimationInterpolationSpeed` | `float` = 15.0 | :32 |
| `bEnableTouchScrolling` | `bool` = true | :36 |
| `bConsumePointerInput` | `bool` = true | :40 |
| `AnalogMouseWheelKey` | `FKey` | :44 |
| `bIsFocusable` | `bool` | :48 |

Events: `OnUserScrolled`, `OnScrollBarVisibilityChanged`, `OnFocusReceived`, `OnFocusLost`,
`OnFocusUpdated` (`ScrollBox.h:257-274`).

`EDescendantScrollDestination { IntoView, TopOrLeft, Center, BottomOrRight, ... }` —
`Runtime/Slate/Public/Widgets/Layout/SScrollBox.h:38-58`.
`EScrollWhenFocusChanges { NoScroll, InstantScroll, AnimatedScroll }` — `SScrollBox.h:63-73`.

### 3.7 GridPanel — `UGridPanel` / `UGridSlot`

Panel (`GridPanel.h:26-31`): `ColumnFill` (`TArray<float>`), `RowFill` (`TArray<float>`) — per-track
fill coefficients; API `SetColumnFill(Index, Coefficient)` / `SetRowFill` (`:39-40`).
A coefficient of 0 = auto-sized track; > 0 = proportional share of leftover space.

Slot (`GridSlot.h:27-68`) — the richest slot in UMG:

| Property | Type | Line |
|---|---|---|
| `Padding` | `FMargin` | :28 |
| `HorizontalAlignment` | `EHorizontalAlignment` | :33 |
| `VerticalAlignment` | `EVerticalAlignment` | :38 |
| `Row` | `int32` (UIMin 0) | :43 |
| `RowSpan` | `int32` | :48 |
| `Column` | `int32` (UIMin 0) | :53 |
| `ColumnSpan` | `int32` | :58 |
| `Layer` | `int32` | :63 |
| `Nudge` | `FVector2D` | :68 |

`Layer` is a paint-order override within the grid; `Nudge` is a post-layout pixel offset.

### 3.8 UniformGridPanel — `UUniformGridPanel` / `UUniformGridSlot`

Panel (`UniformGridPanel.h:28-38`): `SlotPadding` (`FMargin`), `MinDesiredSlotWidth` (`float`),
`MinDesiredSlotHeight` (`float`).
Slot (`UniformGridSlot.h:27-42`): `HorizontalAlignment`, `VerticalAlignment`, `Row`, `Column`.
**No spans, no per-slot padding** — that is the difference from GridPanel.

### 3.9 WrapBox — `UWrapBox` / `UWrapBoxSlot`

Panel (`WrapBox.h:30-50`): `InnerSlotPadding` (`FVector2D`), `WrapSize` (`float`, edit-condition
`bExplicitWrapSize`), `bExplicitWrapSize` (`bool`), `HorizontalAlignment` (edit-condition
`Orientation == Orient_Horizontal`), `Orientation` (`EOrientation`, default `Orient_Horizontal`).
Slot (`WrapBoxSlot.h:25-53`): `Padding`, `FillSpanWhenLessThan` (`float`),
`HorizontalAlignment`, `VerticalAlignment`, `bFillEmptySpace` (`bool`), `bForceNewLine` (`bool`).

`FillSpanWhenLessThan`: if the remaining line span is below this value the slot fills the line.
`bForceNewLine` starts a new line before this child.

### 3.10 SizeBox — `USizeBox` / `USizeBoxSlot`

Panel (`SizeBox.h:32-108`) — 8 optional constraints, each with an `InlineEditConditionToggle`
override bit, exactly the "checkbox + value" pattern:

| Value | Line | Override bit | Line |
|---|---|---|---|
| `WidthOverride` | :33 | `bOverride_WidthOverride` | :73 |
| `HeightOverride` | :38 | `bOverride_HeightOverride` | :78 |
| `MinDesiredWidth` | :43 | `bOverride_MinDesiredWidth` | :83 |
| `MinDesiredHeight` | :48 | `bOverride_MinDesiredHeight` | :88 |
| `MaxDesiredWidth` | :53 | `bOverride_MaxDesiredWidth` | :93 |
| `MaxDesiredHeight` | :58 | `bOverride_MaxDesiredHeight` | :98 |
| `MinAspectRatio` | :63 | `bOverride_MinAspectRatio` | :103 |
| `MaxAspectRatio` | :68 | `bOverride_MaxAspectRatio` | :108 |

Slot (`SizeBoxSlot.h:25-40`): `Padding`, `HorizontalAlignment`, `VerticalAlignment`.
(Aspect-ratio constraints already present at `?ref=5.0.3-release`.)

### 3.11 ScaleBox — `UScaleBox` / `UScaleBoxSlot`

Panel (`ScaleBox.h:30-45`): `Stretch` (`EStretch::Type`), `StretchDirection`
(`EStretchDirection::Type`), `UserSpecifiedScale` (`float`), `IgnoreInheritedScale` (`bool`).

`EStretch` — `Runtime/Slate/Public/Widgets/Layout/SScaleBox.h:31-67`:
`None`, `Fill`, `ScaleToFit`, `ScaleToFitX`, `ScaleToFitY`, `ScaleToFill`, `ScaleBySafeZone`,
`UserSpecified`, `UserSpecifiedWithClipping`.
`EStretchDirection` — `SScaleBox.h:20-28`: `Both`, `DownOnly`, `UpOnly`.

Slot (`ScaleBoxSlot.h:31-37`): `HorizontalAlignment`, `VerticalAlignment` only —
`Padding` is `Padding_DEPRECATED` (`:26`).

### 3.12 Border — `UBorder` / `UBorderSlot`

Panel (`Border.h:36-106`): `HorizontalAlignment`, `VerticalAlignment`,
`bShowEffectWhenDisabled` (AdvancedDisplay), `ContentColorAndOpacity` (`FLinearColor`, sRGB),
`Padding` (`FMargin`), `Background` (`FSlateBrush`, DisplayName "Brush"), `BrushColor`
(`FLinearColor`, sRGB), `DesiredSizeScale` (`FVector2D`),
`bFlipForRightToLeftFlowDirection` (Localization category). Bindable delegates:
`ContentColorAndOpacityDelegate`, `BackgroundDelegate`, `BrushColorDelegate`.
Bindable events: `OnMouseButtonDownEvent`, `OnMouseButtonUpEvent`, `OnMouseMoveEvent`,
`OnMouseDoubleClickEvent` (`Border.h:96-106`).
Slot (`BorderSlot.h:44-55`): `Padding`, `HorizontalAlignment`, `VerticalAlignment`.

### 3.13 Spacer — `USpacer`

`Spacer.h:28`: `Size` (`FVector2D`). No children.

### 3.14 SafeZone — `USafeZone` / `USafeZoneSlot`

Panel (`SafeZone.h:54-69`): `PadLeft`, `PadRight`, `PadTop`, `PadBottom` (four `bool`s).
Slot (`SafeZoneSlot.h:22-38`): `bIsTitleSafe` (Getter `IsTitleSafe`), `SafeAreaScale` (`FMargin`),
`HAlign`, `VAlign`, `Padding`. See §5 for the platform side.

### 3.15 InvalidationBox — `UInvalidationBox`

`InvalidationBox.h:70`: `bCanCache` (`bool`) — the only knob. Caches the child subtree's draw
elements until invalidated.

### 3.16 RetainerBox — `URetainerBox`

`RetainerBox.h`:

| Property | Type | Line |
|---|---|---|
| `bRetainRender` | `bool` = true | :33 |
| `RenderOnInvalidation` | `bool` | :42 |
| `RenderOnPhase` | `bool` | :49 |
| `Phase` | `int32` (UIMin/ClampMin 0) | :60 |
| `PhaseCount` | `int32` (UIMin/ClampMin 1) | :72 |
| `EffectMaterial` | `UMaterialInterface*` | :157 |
| `TextureParameter` | `FName` | :164 |
| `bShowEffectsInDesigner` | `bool` (editor-only) | :171 |

Renders the subtree to an RT and re-uses it; the RT can be piped through a material.

### 3.17 WidgetSwitcher — `UWidgetSwitcher` / `UWidgetSwitcherSlot`

Panel (`WidgetSwitcher.h:24`): `ActiveWidgetIndex` (`int32`, `FieldNotify`, ClampMin 0).
Slot (`WidgetSwitcherSlot.h:33-43`): `Padding`, `HorizontalAlignment`, `VerticalAlignment`.

### 3.18 Panels the brief did not list

**BackgroundBlur — `UBackgroundBlur` / `UBackgroundBlurSlot`.**
Panel (`BackgroundBlur.h:24-73`): `Padding`, `HorizontalAlignment`, `VerticalAlignment`,
`bApplyAlphaToBlur`, `BlurStrength` (float, Clamp 0..100), `bOverrideAutoRadiusCalculation`,
`BlurRadius` (int32, Clamp 0..255, AdvancedDisplay, edit-condition on the override),
`CornerRadius` (`FVector4`, AdvancedDisplay), `LowQualityFallbackBrush` (`FSlateBrush`).
Slot (`BackgroundBlurSlot.h:44-54`): `Padding`, `HorizontalAlignment`, `VerticalAlignment`.

**WindowTitleBarArea — `UWindowTitleBarArea` / `UWindowTitleBarAreaSlot`.**
Slot (`WindowTitleBarAreaSlot.h:43-53`): `Padding`, `HorizontalAlignment`, `VerticalAlignment`.
Marks a region as OS window-drag / double-click-maximise.

**NamedSlot — `UNamedSlot`.** `NamedSlot.h:47` `bExposeOnInstanceOnly` (`bool`, editor-only),
`:65` `SlotGuid` (`FGuid`). The extension point that lets a parent widget inject content into a
child user-widget; the "instance only" flag makes the slot non-inheritable.

**Button — `UButton` / `UButtonSlot`.** A content widget with a real layout slot:
`ButtonSlot.h:26-36` `Padding`, `HorizontalAlignment`, `VerticalAlignment`.

**DynamicEntryBox / DynamicEntryBoxBase.** A pooled auto-populating container that *switches
layout mode*: `EDynamicBoxType { Horizontal, Vertical, Wrap, VerticalWrap, Radial, Overlay }`
(`DynamicEntryBoxBase.h:14-22`). Properties (`:45-80`): `EntrySpacing` (`FVector2D`),
`SpacingPattern` (`TArray<FVector2D>`), `EntryBoxType` (`meta=(DesignerRebuild)`),
`EntrySizeRule` (`FSlateChildSize`), `EntryHorizontalAlignment`, `EntryVerticalAlignment`,
`MaxElementSize` (`int32`), `RadialBoxSettings` (`FRadialBoxSettings`).

**`FRadialBoxSettings`** (`RadialBoxSettings.h:16-36`): `StartingAngle` (0..360),
`bDistributeItemsEvenly`, `bClockwiseOrder`, `AngleBetweenItems` (0..360, when not distributing),
`SectorCentralAngle` (0..360, when distributing), `MarginSize` (>= 0).
Note: `SRadialBox` exists in Slate (`Runtime/Slate/Public/Widgets/Layout/SRadialBox.h`) but there
is **no `URadialBox` UMG panel** — radial layout is only reachable through DynamicEntryBox.

Also present as panel-ish widgets: `UMenuAnchor`, `UExpandableArea`, `UListView`/`UTileView`/
`UTreeView` (`ListViewBase`), `UViewport`, `UNativeWidgetHost`.

---

## 4. The layout algorithm

### 4.1 Two passes

**Pass 1 — desired size, bottom-up.** `SWidget::SlatePrepass(float InLayoutScaleMultiplier)`
(`Runtime/SlateCore/Public/Widgets/SWidget.h:669,675` → `Prepass_Internal` at `:1782`) walks the
tree depth-first and calls `CacheDesiredSize(LayoutScaleMultiplier)` (`SWidget.h:759`), which
calls the pure virtual
`virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const = 0;` (`SWidget.h:774`).
The header states the contract explicitly (`SWidget.h:763-765`): *"CacheDesiredSize() guarantees
that the size of descendants is computed and cached before that of the parents, so it is safe to
call GetDesiredSize() for any children while implementing this method."*
Result is read back with `GetDesiredSize()` (`SWidget.h:695`).

**Pass 2 — arrange, top-down.** The parent receives an `FGeometry` and emits child geometries into
an `FArrangedChildren` via `OnArrangeChildren(const FGeometry&, FArrangedChildren&)`.

Note `ComputeDesiredSize` takes the **layout scale multiplier**: desired size is DPI-dependent,
which is why changing the UI scale forces a full prepass.

### 4.2 `FArrangedChildren`

`Runtime/SlateCore/Public/Layout/ArrangedChildren.h:14`. Holds
`TArray<FArrangedWidget, TInlineAllocator<4>>` (`:22`) plus a **visibility filter** set at
construction (`:31-35`):

```
FArrangedChildren ArrangedChildren( EVisibility::Visible );                       // only visible
FArrangedChildren ArrangedChildren( EVisibility::Collapsed | EVisibility::Hidden ); // only those
```

`AddWidget(VisibilityOverride, FArrangedWidget)` silently drops anything the filter rejects
(`:66-70`). This filter is the mechanism by which `Collapsed` children never even enter arrange.

### 4.3 `FGeometry`

`Runtime/SlateCore/Public/Layout/Geometry.h:39`. Carries `Size`, `AbsolutePosition`, `Scale`,
`AccumulatedRenderTransform`, and `bHasRenderTransform`. It keeps **two** accumulated transforms:

* `AccumulatedLayoutTransform` — scale + translation only (`Geometry.h:126-129`)
* `AccumulatedRenderTransform` — the full 2×2+translation chain including render transforms
  (`Geometry.h:111-122`)

`MakeChild(LocalSize, LayoutTransform)` (`:240`) produces a child geometry with no render
transform; `MakeChild(LocalSize, LayoutTransform, RenderTransform, RenderTransformPivot)` (`:225`)
produces one with. `ToPaintGeometry()` (`:315`) hands both to the renderer.

The split is the whole point: **layout transform affects hit-testing and child layout;
render transform affects pixels only.** `FGeometry::MakeChild(ChildWidget, ...)` at
`SWidget.h:2077-2081` pulls `GetRenderTransformWithRespectToFlowDirection()` off the child and
folds it into the render chain — the arranged *local size and position* are unchanged.

### 4.4 `FSlateRect`

`Runtime/SlateCore/Public/Layout/SlateRect.h` — `Left/Top/Right/Bottom` float rect used for
culling rects (`MyCullingRect` in `OnPaint`) and clipping zones.

### 4.5 `FMargin`

`Runtime/SlateCore/Public/Layout/Margin.h:21-34`: `Left`, `Top`, `Right`, `Bottom` (all `float`,
all `EditAnywhere, BlueprintReadWrite`). Constructors give the CSS-like 1/2/4-value forms.
`GetTotalSpaceAlong<Orient_Horizontal|Orient_Vertical>()` is what box panels consume.
Re-used, with different meaning, as: slot padding, 9-slice brush margin (in **UV space**,
`SlateBrush.h:271-272` `meta=(UVSpace="true")`), and canvas offsets (§2.4).

### 4.6 Alignment application

`Runtime/SlateCore/Public/Layout/LayoutUtils.h:642-673` (`AlignChild`), returning
`AlignmentArrangeResult { Offset, Size }`:

* `HAlign_Fill` → offset = padding-left, size = allotted − total padding
* `HAlign_Left` / `VAlign_Top` → offset = padding-left, size = child desired
* `HAlign_Center` / `VAlign_Center` → centred within the padded box
* `HAlign_Right` / `VAlign_Bottom` → flush to the far edge inside padding

RTL is handled by *swapping* the alignment enum and mirroring padding:
`LayoutPaddingWithFlow` swaps `Left`/`Right` when `EFlowDirection::RightToLeft`
(`LayoutUtils.h:1179-1189`), and `AlignChild` maps `HAlign_Left ↔ HAlign_Right`
(`LayoutUtils.h:567-570`).

### 4.7 `FSlateChildSize` / `ESlateSizeRule`

`Runtime/UMG/Public/Components/SlateWrapperTypes.h:155-177`:

```
UENUM(BlueprintType) namespace ESlateSizeRule {
  enum Type : int { Automatic, Fill };     // :113-122
}
USTRUCT(BlueprintType) struct FSlateChildSize {
  float Value;                              // UIMin 0, UIMax 1   :161
  TEnumAsByte<ESlateSizeRule::Type> SizeRule;                     //  :165
  // defaults: Value = 1.0f, SizeRule = Fill                       :167-171
};
```

`Automatic` = take the child's desired size. `Fill` = take `Value / SumOfFillValues` of whatever
space is left after all `Automatic` children are satisfied.

**Underneath, Slate has a third rule UMG does not expose.** `SBoxPanel::FSlot` supports
`AutoWidth()` / `FillWidth(coef)` / `FillContentWidth(grow, shrink)` plus `MinSize()` / `MaxSize()`
(`Runtime/SlateCore/Public/Widgets/SBoxPanel.h:181-224`, `:333-372`), and
`ArrangeChildrenInStack` implements a **flexbox-style grow/shrink solver** with per-item
`BasisSize`, `MinSize`, `MaxSize`, freezing, and up to 5 relaxation passes
(`LayoutUtils.h:940-1081`; shrink is weighted by `ShrinkStretchValue * BasisSize`,
"to emulate the flexbox behavior" — `LayoutUtils.h:1018-1019`). There is also
`bAllowShrink` (`LayoutUtils.h:930`).
**Version:** `SizeRule_StretchContent` is absent at `?ref=5.4.4-release` and present at
`?ref=5.5.4-release` → introduced in **UE 5.5**.
**Gap to note for parity work:** `UHorizontalBoxSlot`/`UVerticalBoxSlot` expose only
`Size`/`Padding`/`HorizontalAlignment`/`VerticalAlignment` (`HorizontalBoxSlot.h:27-41`) — no
min/max/shrink. The flexbox behaviour is C++-only in 5.8.

### 4.8 Render transform vs layout transform

`UWidget` (`Runtime/UMG/Public/Components/Widget.h`):

| Property | Type | Line | Details |
|---|---|---|---|
| `RenderTransform` | `FWidgetTransform` | :298 | Details category "Render Transform", DisplayName "Transform" |
| `RenderTransformPivot` | `FVector2D` | :306 | DisplayName "Pivot"; normalised, `(0.5,0.5)` = centre |
| `RenderOpacity` | `float` | :452 | UIMin 0, UIMax 1 |
| `PixelSnapping` | `EWidgetPixelSnapping` | :445 | **new in UE 5.3** (absent at `?ref=5.2.1-release`) |

`FWidgetTransform` (`Runtime/UMG/Public/Slate/WidgetTransform.h:23-35`):
`Translation` (`FVector2D`, Delta 1), `Scale` (`FVector2D`, UIMin −5 UIMax 5, Delta 0.05),
`Shear` (`FVector2D`, clamped ±89°, Delta 1), `Angle` (`float`, UIMin −180 UIMax 180, Delta 1).

`EWidgetPixelSnapping { Inherit, Disabled, SnapToPixel }` —
`Runtime/SlateCore/Public/Widgets/WidgetPixelSnapping.h:14-24`.

A render transform **never** feeds back into layout: it is applied only to the accumulated render
transform in `FGeometry` (`Geometry.h:225-227`, `SWidget.h:2077-2081`), so siblings do not move,
the desired size does not change, and hit-testing uses the render chain but the *arranged* box is
untouched.

### 4.9 `RenderOpacity` ≠ tint alpha

`RenderOpacity` is applied in `SWidget::Paint` as
`FWidgetStyle(...).BlendOpacity(RenderOpacity)` — `Runtime/SlateCore/Private/Widgets/SWidget.cpp:1490`,
where `FWidgetStyle::BlendOpacity(float)` does `ColorAndOpacityTint.A *= InOpacity;`
(`Runtime/SlateCore/Public/Styling/WidgetStyle.h:45-49`).

Why that is different from a tint alpha:

* It is a **multiplicative factor on the inherited style**, propagated to the whole subtree
  through `FWidgetStyle`, so it composes down the hierarchy exactly once per widget.
* A tint alpha (e.g. `UImage::ColorAndOpacity`, `UBorder::BrushColor`,
  `FSlateBrush::TintColor`) is a **per-draw-element colour**, applied only to that element's
  vertices. Two overlapping children each at tint alpha 0.5 double-darken; the same two children
  under a parent with `RenderOpacity = 0.5` still double-darken — **UMG's `RenderOpacity` is not a
  group-flatten**. The genuine group-flatten is `URetainerBox` (render to RT, then composite).
* `RenderOpacity` is not animatable through the brush/colour path and does not participate in
  `FSlateColor` foreground resolution.

### 4.10 `Visibility`

UMG enum — `SlateWrapperTypes.h:21-33`:

| Value | Drawn | Occupies layout space | Self hit-test | Children hit-test |
|---|---|---|---|---|
| `Visible` | yes | yes | yes | yes |
| `Collapsed` | no | **no** | no | no |
| `Hidden` | no | **yes** | no | no |
| `HitTestInvisible` (*"Not Hit-Testable (Self & All Children)"*) | yes | yes | no | no |
| `SelfHitTestInvisible` (*"Not Hit-Testable (Self Only)"*) | yes | yes | no | yes |

Slate's `EVisibility` is a **bitfield**, and that is what makes the Collapsed/Hidden distinction
mechanical — `Runtime/SlateCore/Public/Layout/Visibility.h:80-110`:

```
VISPRIVATE_Visible                = 1<<0
VISPRIVATE_Collapsed              = 1<<1
VISPRIVATE_Hidden                 = 1<<2
VISPRIVATE_SelfHitTestVisible     = 1<<3
VISPRIVATE_ChildrenHitTestVisible = 1<<4

VIS_Visible              = Visible | SelfHitTestVisible | ChildrenHitTestVisible
VIS_Collapsed            = Collapsed
VIS_Hidden               = Hidden
VIS_HitTestInvisible     = Visible
VIS_SelfHitTestInvisible = Visible | ChildrenHitTestVisible
VIS_All                  = Visible | Hidden | Collapsed | SelfHitTestVisible | ChildrenHitTestVisible
```

`Hidden` and `Collapsed` both clear the `Visible` bit, so neither paints. The difference is which
*filter* accepts them: panels arrange with `FArrangedChildren(EVisibility::Visible)` for paint but
box panels advance their layout cursor for anything **not** `Collapsed` —
`LayoutUtils.h:1141-1145`:

```
if (ChildVisibility != EVisibility::Collapsed) {
    PositionSoFar += (Orientation == Orient_Vertical) ? SlotSize.Y : SlotSize.X;
}
```

That single comparison *is* the difference: `Hidden` still advances the cursor (reserves space),
`Collapsed` does not. `Collapsed` children are also skipped when computing the panel's desired
size, so a collapsed child cannot inflate its parent.

Helpers: `AreChildrenHitTestVisible()`, `IsHitTestVisible()`, `IsVisible()`,
`DoesVisibilityPassFilter()` (`Visibility.h:56-74`). Visibility can be data-bound via
`UWidget::VisibilityDelegate` (`Widget.h:291`).

### 4.11 `Clipping`

`UWidget::Clipping` (`Widget.h:435`), type `EWidgetClipping` —
`Runtime/SlateCore/Public/Layout/Clipping.h:19-54`:

| Value | Details-panel label | Behaviour |
|---|---|---|
| `Inherit` | Inherit | does not clip; inherits the last clipping area set by an ancestor |
| `ClipToBounds` | Clip To Bounds | clips to own bounds, **intersected** with the existing clip |
| `ClipToBoundsWithoutIntersecting` | *Clip To Bounds - Without Intersecting (Advanced)* | pushes a **new** clip state, does not intersect — can render outside a clipping ancestor. Cannot escape an `Always` zone |
| `ClipToBoundsAlways` | *Clip To Bounds - Always (Advanced)* | intersects, and **cannot be ignored** by descendants |
| `OnDemand` | *On Demand (Advanced)* | clips only when the widget's desired size exceeds its allotted geometry. Added for text in resizable containers |

Backing runtime type `FSlateClippingZone` (`Clipping.h:60-119`) is a **quad** (TopLeft, TopRight,
BottomLeft, BottomRight) with `bIntersect`, `bAlwaysClip`, `bIsAxisAligned` — axis-aligned zones
take a cheap scissor path, rotated ones need a stencil/shader path.
The header warns that **Slate cannot batch across clipping areas** (`Clipping.h:14-16`), which is
the cost model to copy.

### 4.12 Flow direction

`UWidget::FlowDirectionPreference` (`Widget.h:311`), type `EFlowDirectionPreference` —
`Runtime/SlateCore/Public/Layout/FlowDirection.h:29-39`: `Inherit`, `Culture`, `LeftToRight`,
`RightToLeft`. Globals `GSlateFlowDirection` and
`GSlateFlowDirectionShouldFollowCultureByDefault` (`FlowDirection.h:42-43`).

### 4.13 Other author-facing `UWidget` properties

`Runtime/UMG/Public/Components/Widget.h`:

| Property | Type | Line |
|---|---|---|
| `Slot` | `UPanelSlot*` (Instanced, `ShowOnlyInnerProperties`) | :264 |
| `ToolTipText` | `FText` (MultiLine) | :277 |
| `ToolTipWidget` | `UWidget*` (AdvancedDisplay) | :282 |
| `bIsEnabled` | bit, `FieldNotify` | :327 |
| `bOverride_Cursor` / `Cursor` | `EMouseCursor::Type` (AdvancedDisplay) | :331 / :422 |
| `bIsVolatile` | bit, category "Performance" | :388 |
| `Navigation` | `UWidgetNavigation*` (Instanced) | :466 |
| `bIsVariable` | bit (Designer "Is Variable") | :318 |
| `bHiddenInDesigner`, `bExpandedInDesigner`, `bLockedInDesigner` | bits, editor-only | :408, :412, :416 |
| `DisplayLabel`, `CategoryName` | `FString`, editor-only | :1225, :1232 |
| Accessibility: `bOverrideAccessibleDefaults`, `bCanChildrenBeAccessible`, `AccessibleBehavior`, `AccessibleSummaryBehavior`, `AccessibleText`, `AccessibleSummaryText` | | :348-372 |

`ESlateAccessibleBehavior { NotAccessible, Auto, Summary, Custom, ToolTip }` —
`SlateWrapperTypes.h:37-54`.

Meta-specifiers the designer understands (`Widget.h:60-91`):
`DesignerRebuild`, `BindWidget`, `BindWidgetOptional`, `OptionalWidget`, `BindWidgetAnim`,
`BindWidgetAnimOptional`, `IsBindableEvent`.

### 4.14 Design-time flags

`EWidgetDesignFlags` — `Widget.h:174-184`:

```
None = 0, Designing = 1<<0, ShowOutline = 1<<1, ExecutePreConstruct = 1<<2, Previewing = 1<<3
```

`UWidget::IsDesignTime()` == `HasAnyDesignerFlags(Designing)` (`Widget.h:949-952`);
`IsPreviewTime()` == `HasAnyDesignerFlags(Previewing)` (`:969-971`). Outside the editor
`IsDesignTime()` is a compile-time `false` (`Widget.h:999`).
`SetDesignerFlags` is virtual and `UPanelWidget` propagates it to children
(`PanelWidget.h:105`). The stored value is `uint8 DesignerFlags` (`Widget.h:1221`).

**Property bindings are suppressed at design time.** The `OPTIONAL_BINDING` macro family expands
to `(Delegate.IsBound() && !IsDesignTime())` — `Widget.h:110, 117, 138, 145, 163`. So a bound
property shows its *literal* value in the Designer, not the bound value.

`FDesignerChangedEventArgs` (`Widget.h:193-204`): `bScreenPreview`, `Size`, `DpiScale` — pushed to
widgets via `OnDesignerChanged` when the preview screen size or DPI changes.

`UUserWidget` design-time fields (`Runtime/UMG/Public/Blueprint/UserWidget.h`):
`DesignTimeSize` (`FVector2D`, :1532), `DesignSizeMode` (`EDesignPreviewSizeMode`, :1535),
`PaletteCategory` (`FText`, :1539), `PreviewBackground` (`UTexture2D*`,
`EditDefaultsOnly, Category="Designer"`, :1545).
`EDesignPreviewSizeMode { FillScreen, Custom, CustomOnScreen, Desired, DesiredOnScreen }` —
`UserWidget.h:246-253`.
`PreConstruct(bool IsDesignTime)` is the BlueprintImplementable hook (`UserWidget.h:523`), native
counterpart `NativePreConstruct()` (`:1583`); the header notes its design-time execution is gated
by a Widget Designer setting (`:520`).

---

## 5. DPI / resolution independence

### 5.1 `UUserInterfaceSettings`

`Runtime/Engine/Classes/Engine/UserInterfaceSettings.h:116-119` —
`UCLASS(config=Engine, defaultconfig, meta=(DisplayName="User Interface"), MinimalAPI)`,
base `UDeveloperSettings`, `SectionName = "UI"` (`Private/UserInterfaceSettings.cpp:27`) →
*Project Settings → Engine → User Interface*.
Engine defaults, `Engine/Config/BaseEngine.ini:1473-1477`:

```
[/Script/Engine.UserInterfaceSettings]
UIScaleRule=ShortestSide
UIScaleCurve=(EditorCurveData=(Keys=((Time=480,Value=0.444),(Time=720,Value=0.666),(Time=1080,Value=1.0),(Time=8640,Value=8.0))))
bLoadWidgetsOnDedicatedServer=True
bAllowHighDPIInGameMode=False
```

The curve is **linear keys on raw pixel extent**, normalised so 1080 → 1.0.

| Property | Type | Decl | Default | Category |
|---|---|---|---|---|
| `RenderFocusRule` | `ERenderFocusRule` | `.h:126` | `NavigationOnly` (`.cpp:16`) | Focus |
| `HardwareCursors` | `TMap<EMouseCursor::Type, FHardwareCursorReference>` | `.h:129` | empty | Hardware Cursors |
| `SoftwareCursors` | `TMap<EMouseCursor::Type, FSoftClassPath>` | `.h:132` | empty | Software Cursors, `MetaClass=UserWidget` |
| `*Cursor_DEPRECATED` ×7 | `FSoftClassPath` | `.h:135-161` | null | migrated in `PostInitProperties` (`.cpp:34-74`), deprecated 4.16 |
| `ApplicationScale` | `float` | `.h:167` | `1` (`.cpp:17`) | DPI Scaling |
| `UIScaleRule` | `EUIScalingRule` | `.h:173` | `ShortestSide` | DPI Scaling, DisplayName "DPI Scale Rule" |
| `CustomScalingRuleClass` | `FSoftClassPath` | `.h:179` | null | DPI Scaling, `MetaClass=DPICustomScalingRule` |
| `UIScaleCurve` | `FRuntimeFloatCurve` | `.h:185` | see ini | DPI Scaling, DisplayName "DPI Curve", axes "Resolution"/"Scale" |
| `bAllowHighDPIInGameMode` | `bool` | `.h:195` | `False` | DPI Scaling |
| `DesignScreenSize` | `FIntPoint` | `.h:199` | `(1920,1080)`, `ClampMin=1` | DPI Scaling \| Scale To Fit Rule |
| `bLoadWidgetsOnDedicatedServer` | `bool` | `.h:205` | `true` (`.cpp:18`) | Widgets |
| `bAuthorizeAutomaticWidgetVariableCreation` | `bool` | `.h:213` | `true` (`.cpp:19`) | Widgets — **added 5.2** |
| `CustomFontDPI` (private) | `uint32` | `.h:259` | `FontConstants::RenderDPI` = **96** | UMG Fonts, DisplayName "Font Resolution", `ClampMin=1 ClampMax=1000` |
| `FontDPIPreset` (private) | `EFontDPI` | `.h:265` | `EFontDPI::Unreal` | UMG Fonts |
| `bUseCustomFontDPI` (private) | `bool` | `.h:271` | `false` | UMG Fonts |
| `bEnableDistanceFieldFontRasterization` | `bool` | `.h:278` | `false` (`.cpp:25`) | UMG Fonts — **added 5.5** |
| `CursorClasses`, `CustomScalingRuleClassInstance`, `CustomScalingRule` | transient | `.h:283-290` | — | GC roots / caches |
| `LastViewportSize`, `CalculatedScale` | non-UPROPERTY mutable | `.h:292-293` | — | scale cache |

**There is no `bUseHighDPIStyle`.** The editor equivalent is
`UEditorStyleSettings::bEnableHighDPIAwareness`.

Enums:

* `ERenderFocusRule` (`.h:17-28`): `Always`, `NonPointer`, `NavigationOnly`, `Never`.
* **`EUIScalingRule` (`.h:31-46`) — complete, and unchanged since 4.26**: `ShortestSide`,
  `LongestSide`, `Horizontal`, `Vertical`, `ScaleToFit`, `Custom`.
* `EFontDPI` (`.h:49-58`): `Standard` ("72 DPI (Default)"), `Unreal` ("96 DPI (Unreal Engine)"),
  `Custom` (Hidden). Mapping `.cpp:173-184`: `Unreal→96`, everything else `→72`.
  `FontConstants::RenderDPI = 96` — `Runtime/SlateCore/Public/Fonts/SlateFontInfo.h:12-16`.

`FHardwareCursorReference` (`.h:66-111`): `FName CursorPath` (partial content path, **no
extension**) + `FVector2D HotSpot` (normalised 0..1). Platform search order documented at
`.h:82-100`: Windows `.ani → .cur → .png`; Mac `.tiff → .png`; Linux `.png`; multi-res png fallback
`Pointer.png`, `Pointer@1.25x.png`, `@1.5x`, `@1.75x`, `@2x`.

**Trap (5.8):** the three font-DPI properties are now declared unconditionally (`.h:259-272`) but
the constructor initialisers that set them are still inside `#if WITH_EDITORONLY_DATA`
(`.cpp:20-24`). A cooked build with no ini entry zero-inits → `FontDPIPreset = Standard` → **72
DPI**, while the editor CDO defaults to **96**. In ≤5.7 the properties themselves were
editor-only so the asymmetry did not exist.

### 5.2 `GetDPIScaleBasedOnSize` — the actual implementation

`UserInterfaceSettings.cpp:87-110`:

```cpp
float UUserInterfaceSettings::GetDPIScaleBasedOnSize(FIntPoint Size) const
{
    float Scale = 1.0f;
    if (LastViewportSize.IsSet() && Size == LastViewportSize.GetValue())
        Scale = CalculatedScale;                 // cache hit
    else {
        bool bOutError;
        Scale = CalculateScale(Size, bOutError);
        if (!bOutError) {
            CalculatedScale = Scale;
#if !WITH_EDITOR
            LastViewportSize = Size;             // cache ONLY outside the editor
#endif
        }
    }
    return FMath::Max(Scale * ApplicationScale, 0.01f);
}
```

* The cache key is the exact `FIntPoint`; **`LastViewportSize` is never set in editor builds**
  (`.cpp:102-105`), so the curve re-evaluates every call and designers see live edits.
* On error (missing custom rule class) nothing is cached, so it retries — and re-logs — every call.
* **`ApplicationScale` is a post-multiplier applied after the rule**, then floored at `0.01f`, and
  is *not* part of the cached value, so changing it takes effect even on a cache hit.
* `PostEditChangeProperty` (`.cpp:230-244`) resets `LastViewportSize` and nulls the cached rule
  class/CDO when `CustomScalingRuleClass` changes.

`CalculateScale` (`.cpp:112-162`) — how each rule picks its evaluated size:

| Rule | Evaluated value | Line |
|---|---|---|
| `ShortestSide` | `FMath::Min(Size.X, Size.Y)` | `:143-145` |
| `LongestSide` | `FMath::Max(Size.X, Size.Y)` | `:146-148` |
| `Horizontal` | `Size.X` | `:149-151` |
| `Vertical` | `Size.Y` | `:152-154` |
| `ScaleToFit` | **returns early, curve not used** | `:155-156` |
| `Custom` | delegates to the rule CDO | `:116-137` |

Curve rules end with
`UIScaleCurve.GetRichCurveConst()->Eval((float)EvalPoint, /*Default*/ 1.0f)` (`.cpp:159-160`).

`ScaleToFit` (`.cpp:155-156`):

```cpp
return DesignScreenSize.X > 0 && DesignScreenSize.Y > 0
    ? FMath::Min((float)Size.X / DesignScreenSize.X, (float)Size.Y / DesignScreenSize.Y)
    : 1.f;
```

i.e. exactly `SScaleBox::ScaleToFit` semantics — uniform min-of-axes ratio, 1.0 at the design
resolution, and **it cannot be combined with the curve**.

`Custom` (`.cpp:116-137`): lazily `TryLoadClass<UDPICustomScalingRule>()`; on failure emits
`FMessageLog("MapCheck").Error()` *"Project Settings - User Interface Custom Scaling Rule '{0}'
could not be found."*, sets `bError`, returns 1. Otherwise takes the class **CDO**
(`GetDefaultObject<UDPICustomScalingRule>()`, `.cpp:133`) and calls
`GetDPIScaleBasedOnSize(Size)` — **so the rule must be stateless.**
Interface: `Runtime/Engine/Classes/Engine/DPICustomScalingRule.h:14-26`,
`UCLASS(Abstract, MinimalAPI)`, one `virtual float GetDPIScaleBasedOnSize(FIntPoint) const`;
base returns 1 (`DPICustomScalingRule.cpp:9-12`).

`ForceLoadResources(bool bForceLoadEverything = false)` (`.h:221`, `.cpp:248-290`), called from
`PostInitProperties` (`.cpp:77-84`): skips when `IsRunningCommandlet()`, honours
`bLoadWidgetsOnDedicatedServer` on a server (`.cpp:252-259`), loads every `SoftwareCursors` value,
`AddToRoot()`s each outside the editor (`.cpp:276-279`), and eagerly resolves
`CustomScalingRuleClassInstance` (`.cpp:288`).

Font helpers (5.8, `.cpp:186-199`):
`ConvertFontSizeFromNativeToDisplay(x) = x * 96 / GetFontDisplayDPI()`;
`ConvertFontSizeFromDisplayToNative(x) = GridSnap(x * GetFontDisplayDPI() / 96, 0.01f)`.

### 5.3 Where the scale is applied — and why the OS DPI cancels

**Game:** the whole game-UI tree sits inside an `SDPIScaler` bound to `GetGameViewportDPIScale`
— `Runtime/Engine/Private/Slate/SGameLayerManager.cpp:113-115`, inserted at `:178`.
`SGameLayerManager::GetGameViewportDPIScale()` (`:467-499`):

```cpp
const FIntPoint ViewportSize = Viewport->GetSize();
if (bUseScaledDPI) {
    float DPIValue      = UISettings->GetDPIScaleBasedOnSize(ScaledDPIViewportReference);
    float ViewportScale = FMath::Min((float)ViewportSize.X / ScaledDPIViewportReference.X,
                                     (float)ViewportSize.Y / ScaledDPIViewportReference.Y);
    GameUIScale = DPIValue * ViewportScale;
} else {
    GameUIScale = UISettings->GetDPIScaleBasedOnSize(ViewportSize);
}
const float FinalUIScale = GameUIScale / Viewport->GetCachedGeometry().Scale;   // :496
return FinalUIScale;
```

**Line `:496` is the whole trick**: the platform/monitor DPI scale already baked into the viewport
geometry is *divided out*, because the UI-scale curve is authored against **raw pixel resolution**
assuming a platform scale of 1.
`SetUseFixedDPIValue(bool, FIntPoint RefViewportSize)` / `IsUsingFixedDPIValue()`
(`SGameLayerManager.h:171-172`, `.cpp:456-465`) is the "render as if the viewport were N, then
letterbox-scale" path. Split-screen player layers are positioned in unscaled space via
`InverseDPIScale = 1/GetGameViewportDPIScale()` (`.cpp:637-643`, `:670-671`).
Blueprint entry point: `UWidgetLayoutLibrary::GetViewportScale`
(`Runtime/UMG/Private/WidgetLayoutLibrary.cpp:87-127`, frame-cached via `TFrameValue<float>`,
**cache bypassed under `WITH_EDITOR`** at `:91`); the comment at `:119-124` restates the rule.

`SDPIScaler` — `Runtime/Slate/Public/Widgets/Layout/SDPIScaler.h`:
`SLATE_ATTRIBUTE(float, DPIScale)` (`:33`), default `1.0f` (`:23`), default visibility
`SelfHitTestInvisible` (`:26`), and it implements `GetRelativeLayoutScale` (`:58`) so the scale
enters **layout**, not just paint.

**Two independent multipliers:**

* **Slate application scale** — `FSlateApplication::SetApplicationScale(float)`
  (`Runtime/Slate/Public/Framework/Application/SlateApplication.h:908`, *"Sets the ratio SlateUnit
  / ScreenPixel"*), read via `GetApplicationScale()` (`:1645`).
  **There is no `Slate.ApplicationScale` CVar.** It is driven by
  `UEditorStyleSettings::ApplicationScale`
  (`Editor/UnrealEd/Classes/Settings/EditorStyleSettings.h:51-55`, `ClampMin=0.5 ClampMax=3.0`,
  default 1.0; pushed at `EditorStyleClasses.cpp:73-76` and `:126-132`) and by the Widget
  Reflector's slider (`Developer/SlateReflector/Private/Widgets/SSlateOptions.cpp:262-270`).
* **OS/monitor DPI scale** — per window: `SWindow::GetDPIScaleFactor()`
  (`SlateCore/Private/Widgets/SWindow.cpp:1222-1230`) → `NativeWindow->GetDPIScaleFactor()`;
  source of truth `FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(X,Y)`
  (`ApplicationCore/Public/GenericPlatform/GenericPlatformApplicationMisc.h:208`).
  **Correction to the brief: `FSlateApplication::GetDPIScaleFactorForWindow` does not exist in
  5.8.** The composition rule is spelled out at the call sites instead:
  `LayoutScaleMultiplier = FSlateApplicationBase::Get().GetApplicationScale() * SWindow::GetDPIScaleFactor()`
  (`SWindow.cpp:825, 830, 909, 1444, 1932, 2137`; `SlateApplication.cpp:1362`).

So the full chain for a game widget is
**`(OS DPI × ApplicationScale)`** (window layout transform) **×**
**`(GetDPIScaleBasedOnSize(viewport) ÷ OS DPI)`** (the `SDPIScaler`) — the OS term cancels by
design. Implementing only one half produces double-scaled UI on hi-DPI monitors.

**High-DPI awareness:** CVar `EnableHighDPIAwareness` (int, default 1, `ECVF_ReadOnly`) —
`ApplicationCore/Private/GenericPlatform/GenericPlatformApplicationMisc.cpp:25-32`, queried by
`IsHighDPIAwarenessEnabled()` (`:120-123`).
`FSlateApplication::InitHighDPI(bool bForceEnable)` (`SlateApplication.h:332`,
`SlateApplication.cpp:1010-1040+`): in editor reads `GEditorSettingsIni [HDPI]
EnableHighDPIAwareness`; **in game reads `GEngineIni [/Script/Engine.UserInterfaceSettings]
bAllowHighDPIInGameMode`** (`:1019-1024`); honours `-nohighdpi`; then
`FPlatformApplicationMisc::SetHighDPIMode()` (`:1032`). Comment at `:1030`: *"High DPI must be
enabled before any windows are shown"*.
Editor toggle `UEditorStyleSettings::bEnableHighDPIAwareness` (`EditorStyleSettings.h:48-49`,
`ConfigRestartRequired`).

**`r.MobileContentScaleFactor`** — `Runtime/Core/Private/HAL/ConsoleManager.cpp:4387-4391`, float,
default 1.0. It changes the **backbuffer resolution**, not the Slate scale; the UI scale follows
because `GetDPIScaleBasedOnSize` then sees a bigger viewport. It also feeds the editor device
preview: `FPlayScreenResolution::PostInitProperties`
(`UnrealEd/Private/Settings/SettingsClasses.cpp:544-555`) → `RescaleForMobilePreview` (`:843-871`).

### 5.4 `SSafeZone`

`Runtime/Slate/Public/Widgets/Layout/SSafeZone.h`, `Private/Widgets/Layout/SSafeZone.cpp`.
Derives from **`SBox`** (`.h:29`).

| Argument | Kind | Default | Notes |
|---|---|---|---|
| `HAlign` | `SLATE_ARGUMENT(EHorizontalAlignment)` `.h:49` | `HAlign_Fill` | forwarded to `SBox::Construct` (`.cpp:56-62`) |
| `VAlign` | `SLATE_ARGUMENT(EVerticalAlignment)` `.h:52` | `VAlign_Fill` | ditto |
| `Padding` | `SLATE_ATTRIBUTE(FMargin)` `.h:55` | `0.0f` | **added to** the safe margin (`.cpp:176`) |
| `Content` | `SLATE_DEFAULT_SLOT` `.h:58` | — | |
| `IsTitleSafe` | `SLATE_ARGUMENT(bool)` `.h:61` | `false` | see the defect below |
| `SafeAreaScale` | `SLATE_ARGUMENT(FMargin)` `.h:67` | `FMargin(1,1,1,1)` | per-side multiplier; 0 opts a side out (`.cpp:176`) |
| `PadLeft` / `PadRight` / `PadTop` / `PadBottom` | `SLATE_ARGUMENT(bool)` `.h:70/73/76/79` | all `true` | zero individual sides (`.cpp:137`) |
| `OverrideScreenSize` | `SLATE_ARGUMENT(TOptional<FVector2D>)` `.h:83` | unset | **`#if WITH_EDITOR` only** |
| `OverrideDpiScale` | `SLATE_ARGUMENT(TOptional<float>)` `.h:86` | unset | **`#if WITH_EDITOR` only** |

There is **no `bSafeAreaTitle` argument**; the member is `bIsTitleSafe` (`.h:124`).
API: `SetTitleSafe(bool)` (`.h:98`), `SetSafeAreaScale(FMargin)` (`.h:99`),
`SetSidesToPad(4×bool)` (`.h:101`), `GetSafeMargin(float InLayoutScale)` (`.h:103`), editor-only
`SetOverrideScreenInformation(...)` (`.h:106`) and `DebugSafeAreaUpdated(const FMargin&, bool)`
(`.h:107`), statics `SetGlobalSafeZoneScale`/`GetGlobalSafeZoneScale` (`.h:113-114`).

Computation:

1. `Construct` (`.cpp:54-82`) caches args, subscribes (editor) to
   `FSlateApplication::OnDebugSafeZoneChanged` (`.cpp:76`) and to
   `FCoreDelegates::OnSafeFrameChangedEvent` (`.cpp:81`).
2. `UpdateSafeMargin()` (`.cpp:94-141`) — **editor**: if `OverrideScreenSize` is set and non-zero →
   `GetSafeZoneSize(SafeMargin, OverrideScreenSize)` (`.cpp:99-102`), else walk
   `FSlateApplication::GetGameViewport()->GetSize()` and use *that*, not the display
   (`.cpp:105-125`). With no game viewport it **returns without clearing
   `bSafeMarginNeedsUpdate`** (`.cpp:118-124`) and retries lazily.
   **Non-editor**: `GetSafeZoneSize(SafeMargin, {})` — the override is ignored (`.cpp:126-129`).
   Then multiplies by the global `SafeZone.Scale` (`.cpp:131-135`), zeroes disabled sides
   (`.cpp:137`), `Invalidate(Layout)`.
3. `ComputeScaledSafeMargin(float Scale)` (`.cpp:185-199`) — **the margin comes back in screen
   pixels**, so it is divided by the layout scale and rounded per side.
4. `GetSafeMargin(InLayoutScale)` (`.cpp:169-178`):
   `Padding.Get() + (ComputeScaledSafeMargin(InLayoutScale) * SafeAreaScale)`.
5. `OnArrangeChildren` (`.cpp:201-218`) uses it as slot padding via `AlignChild`;
   `ComputeDesiredSize` (`.cpp:220-233`) adds `SlotPadding.GetDesiredSize()` and short-circuits to
   `(0,0)` for a `Collapsed` child.

Global CVars (`SSafeZone.cpp:9-23`): `SafeZone.Scale` (float, default 1.0) and
`SafeZone.EnableScale` (bool, default false). `SetGlobalSafeZoneScale` (`.cpp:32-43`) clamps
negatives to 1 and broadcasts `FCoreDelegates::OnSafeFrameChangedEvent`.

> **Defect: `IsTitleSafe` is inert in 5.8.**
> `void SSafeZone::SetTitleSafe(bool InIsTitleSafe) { UpdateSafeMargin(); }` —
> `SSafeZone.cpp:89-92`. The parameter is unused; `bIsTitleSafe` is written only in `Construct`
> (`.cpp:66`) and never read in the margin path, which always goes
> `GetSafeZoneSize → GetSafeZoneRatio` and reads **`TitleSafePaddingSize` only**
> (`SlateApplicationBase.cpp:95`). Consequently `FDisplayMetrics::ActionSafePaddingSize` is
> **written by four platforms and read by nobody**. The "two overlaid safe zones" pattern
> documented at `SSafeZone.h:16-26` does not work. Do not port it.

### 5.5 `USafeZone` / `USafeZoneSlot` (UMG)

`Runtime/UMG/Public/Components/SafeZone.h`, `Private/Components/SafeZone.cpp`.
`UContentWidget`; ctor sets `bCanHaveMultipleChildren = false` and visibility
`SelfHitTestInvisible` (`.cpp:12-20`); palette category "Panel" (`.cpp:24-27`).
**Its class comment is the canonical documentation for the debug CVars** (`SafeZone.h:18-25`):
`r.DebugSafeZone.TitleRatio 0.96`, `r.DebugActionZone.ActionRatio 0.96`, `r.DebugSafeZone.Mode`.
Properties `PadLeft/PadRight/PadTop/PadBottom` (`.h:51-69`, all `UE_DEPRECATED(5.2)` direct access,
defaults `true`), BP-callable `SetSidesToPad` (`.h:46-47`, `.cpp:91-102`).
`RebuildWidget` (`.cpp:104-136`) feeds the `SSafeZone` from the **slot** (`IsTitleSafe`,
`SafeAreaScale`, `HAlign`, `VAlign`, `Padding`) plus the widget-level pad flags, and under
`#if WITH_EDITOR` passes `OverrideScreenSize(DesignerSize)` / `OverrideDpiScale(DesignerDpi)`
(`.cpp:122-125`).
`OnDesignerChanged` (`.cpp:29-46`) stores `EventArgs.Size` when `bScreenPreview` (else zero) and
`EventArgs.DpiScale`, and forwards via `SetOverrideScreenInformation`.
`USafeZoneSlot` (`SafeZoneSlot.h:14-38`): `bIsTitleSafe`, `SafeAreaScale` (`FMargin`), `HAlign`,
`VAlign`, `Padding` — all `UE_DEPRECATED(5.2)` for direct access.

CommonUI's only safe-zone participant is **`UCommonBorder`** (not `UCommonUISettings`):
`bReducePaddingBySafezone` (`CommonBorder.h:50`) + `FMargin MinimumPadding` (`.h:53-54`). It
**subtracts** the safe margin from its own padding, floored by `MinimumPadding`
(`CommonBorder.cpp:106-138`).

### 5.6 `FDisplayMetrics` and the ratio→pixel plumbing

`Runtime/ApplicationCore/Public/GenericPlatform/GenericApplication.h` (**not** `GenericWindow.h`);
impl `Private/GenericPlatform/GenericApplication.cpp`.

* `FPlatformRect` — `:319-333`.
* `FMonitorInfo` — `:339-358`: `Name`, `FriendlyName`, `ID`, `NativeWidth`/`NativeHeight`,
  `MaxResolution`, `DisplayRect`, `WorkArea`, `bIsPrimary`, `int32 DPI = 0`, `NativeHandle`.
* `FDisplayMetrics` — `:364-421`: `PrimaryDisplayWidth`/`Height` (`:373,376`),
  `TArray<FMonitorInfo> MonitorInfo` (`:379`, PC only), `PrimaryDisplayWorkAreaRect` (`:382`),
  `VirtualDisplayRect` (`:385`),
  **`FVector4 TitleSafePaddingSize`** (`:387-394`, component order documented in place:
  `Left=X, Top=Y, Right=Z, Bottom=W`), **`FVector4 ActionSafePaddingSize`** (`:396-397`),
  `static void RebuildDisplayMetrics(FDisplayMetrics&)` (`:399`, per-platform — Windows at
  `Private/Windows/WindowsApplication.cpp:2125-2161`, ending with `ApplyDefaultSafeZones()` at
  `:2160`), `GetMonitorWorkAreaFromPoint` (`:402`), `GetClosestMonitorFromIDAndIndex` (`:405`),
  `PrintToLog` (`:408`), `static float GetDebugTitleSafeZoneRatio()` (`:411`, public), and
  protected `TryGetTitleSafeZoneOverwrite` (`:414`), `GetDebugActionSafeZoneRatio` (`:417`),
  `ApplyDefaultSafeZones` (`:420`).

`ApplyDefaultSafeZones()` (`GenericApplication.cpp:234-254`):

```cpp
TitleSafePaddingSize = FVector4(0,0,0,0);
if (!TryGetTitleSafeZoneOverwrite(TitleSafePaddingSize)) {
    const float SafeZoneRatio = GetDebugTitleSafeZoneRatio();
    if (SafeZoneRatio < 1.0f) {
        const float HalfUnsafeRatio = (1.0f - SafeZoneRatio) * 0.5f;
        TitleSafePaddingSize = FVector4(W*HalfUnsafeRatio, H*HalfUnsafeRatio,
                                        W*HalfUnsafeRatio, H*HalfUnsafeRatio);
    }
}
// same for ActionSafePaddingSize — but it is NOT pre-zeroed here
```

Platform overrides after the defaults: **iOS/tvOS** (`Private/IOS/IOSApplication.cpp:258-315`) uses
`UIEdgeInsets` (notch/home bar), swaps L/R for `LandscapeRight` (`:285-298`), allows CVars
`SafeZone.Landscape.{Left,Top,Right,Bottom}` (default `-1` = use OS, `:249-252`), multiplies by
`contentScaleFactor` (`:313`), and mirrors into `ActionSafePaddingSize` (`:315`).
**Android** (`Private/Android/AndroidApplication.cpp:197-226`) calls `ApplyDefaultSafeZones()`
first, then `FAndroidWindow::GetSafezone(bIsPortrait)`, then config-rules
`SafeZone_Portrait`/`SafeZone_Landscape` (comma-separated 4-tuple of fractions), overwriting only
components ≥ 0.

Ratio→pixel conversion — `Runtime/SlateCore/Private/Application/SlateApplicationBase.cpp`:
`GetSafeZoneSize(FMargin& SafeZone, OverrideSize)` (`.h:279`, `.cpp:60-81`) — `OverrideSize` is
honoured **only `#if WITH_EDITOR`** (`.cpp:64-66`); if zero, falls back to cached primary-display
extents (`.cpp:68-73`); then `SafeZone.Left = Ratio.Left * ContainerSize.X / 2.0f` etc.
(`.cpp:77-80`) — note the **/2**, because the ratio is a fraction of the *half*-extent.
`GetSafeZoneRatio(FMargin&)` (`.h:319`, `.cpp:83-101`) returns `CustomSafeZoneRatio` when
`IsCustomSafeZoneSet()`, else `TitleSafePaddingSize` over half-extents.
Custom-safe-zone state machine `ECustomSafeZoneState { Unset, Set, Debug }` (`.h:533-538`) with
`UpdateCustomSafeZone` (`.cpp:162-176`), `SwapSafeZoneTypes` (`.cpp:178-191`, `WITH_EDITOR`),
`ResetCustomSafeZone` (`.cpp:193-197`), `IsCustomSafeZoneSet` (`.cpp:199-203`),
`SetCustomSafeZone` (`.cpp:205-209`).
Delegate `DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDebugSafeZoneChanged, const FMargin&, bool)`
(`.h:15`); the instance is `#if WITH_EDITORONLY_DATA` (`.h:674-676`). Wired in the
`FSlateApplication` ctor (`SlateApplication.cpp:956-958`).

### 5.7 Safe-zone CVars and console commands — the complete set

| CVar | Type / default | Declared | Meaning |
|---|---|---|---|
| `r.DebugSafeZone.TitleRatio` | float `1.0` | `GenericApplication.cpp:108, 123-127` | Safe-zone ratio returned on platforms with no defined safe zone (0..1). On change broadcasts `FCoreDelegates::OnSafeFrameChangedEvent` (`:134-141`) |
| `r.DebugActionZone.ActionRatio` | float `1.0` | `GenericApplication.cpp:109, 128-132` | Same for the action zone |
| `r.DebugSafeZone.Mode` | int `0` | **`Runtime/Engine/Private/HUD.cpp:39-45`** | 0 = disabled, 1 = show title-safe overlay |
| `r.DebugSafeZone.OverlayAlpha` | float `0.2` | `HUD.cpp:47-52` | Tint alpha of the unsafe region |
| `r.DebugSafeZone.MaxDebugTextStringsPerActor` | int `128` | `HUD.cpp:54-57` | Unrelated despite the prefix |
| `SafeZone.EnableOverrides` | bool `false` | `GenericApplication.cpp:111` | Enables the four ratio overrides |
| `SafeZone.Ratio.Left` / `.Top` / `.Right` / `.Bottom` | float `1.0` | `GenericApplication.cpp:112-115` | Used by `TryGetTitleSafeZoneOverwrite` (`:210-227`); comment at `:236` — *"Used by streaming and mobile PIE."* |
| `SafeZone.Scale` | float `1.0` | `SSafeZone.cpp:9-15` | Global safe-zone multiplier |
| `SafeZone.EnableScale` | bool `false` | `SSafeZone.cpp:17-23` | Enables the above |
| `SafeZone.Landscape.{Left,Top,Right,Bottom}` | float `-1.0` | `IOS/IOSApplication.cpp:249-252` | iOS only; −1 = use the OS value |
| `r.CustomUnsafeZones` | string `""` | `Core/Private/HAL/ConsoleManager.cpp:4702-4709` | Per-device notch rects, format `(P:fixed[x1,y1][w,h])`, semicolon-separated; ±values offset from 0 or from Width/Height |

**Correction to the brief: `r.DebugSafeZone.MaxDebugTitleSafeZoneRatio` does not exist** in 5.8 —
the only `Max…` CVar under that prefix is `MaxDebugTextStringsPerActor`.

Console command: `UGameViewportClient::ShowTitleSafeArea()` —
`Engine/Classes/Engine/GameViewportClient.h:136-139`,
`UFUNCTION(exec, meta=(DeprecatedFunction, DeprecationMessage="Use the cvar \"r.DebugSafeZone.Mode=1.\""))`;
impl `Private/GameViewportClient.cpp:2766-2781` — it just toggles the CVar.

Who draws the overlay:

* **With a HUD:** `AHUD::DrawSafeZoneOverlay()` — `HUD.cpp:264-308`, called from `PostRender`
  (`:238`), guarded by `ENABLE_DRAW_DEBUG`; colour `FLinearColor(1.0, 0.5, 0.5, OverlayAlpha)`
  (`:277`), four `FCanvasTileItem` bars.
* **PIE with no HUD:** `UGameViewportClient::DrawTitleSafeArea(UCanvas*)`
  (`GameViewportClient.h:466-470`, `.cpp:3238-3309`, `#if WITH_EDITOR`). Colour is
  `FLinearColor(1,0,0, alpha)` — **red, unlike the HUD's pink** (`:3262-3267`). Uniform path when
  `GetDebugTitleSafeZoneRatio() < 1` (`:3272-3296`), device-emulation path via
  `ULevelEditorPlaySettings::CalculateCustomUnsafeZones` when
  `FSlateApplication::IsCustomSafeZoneSet()` (`:3297-3308`).

Split-screen helpers on `UGameViewportClient`: `HasTop/Bottom/Left/RightSafeZone`
(`.h:439-448`, `.cpp:2995-3100`), `GetPixelSizeOfScreen` (`.cpp:3102-3160` — multiplies the canvas
clip by the split factor so the zone is computed against the *full* screen),
`CalculateSafeZoneValues` (`.h:457`, `.cpp:3163-3170`; note its `bUseMaxPercent` parameter is
**ignored**), `CalculateDeadZoneForAllSides` (`.cpp:3173-3236`).

### 5.8 How the UMG Designer previews DPI and safe zones

* DPI is applied by a real `SDPIScaler`:
  `SAssignNew(PreviewSurface, SDPIScaler).DPIScale(this, &SDesignerView::GetPreviewDPIScale)` —
  `Editor/UMGEditor/Private/Designer/SDesignerView.cpp:324-325`.
* `GetPreviewDPIScale()` (`:1161-1164`) → `FWidgetBlueprintEditorUtils::GetWidgetPreviewDPIScale`
  (`Private/WidgetBlueprintEditorUtils.cpp:2276-2289`), which **returns 1.0 for `Custom` and
  `Desired`** (*"If the user is using a custom size then we disable the DPI scaling logic"*) and
  otherwise `GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(PreviewSize)`.
* `GetPreviewScale() = GetZoomAmount() * GetPreviewDPIScale()` (`:993-996`) — rulers invert it
  (`:2446-2447`), the grid uses it (`:1182-1185`), cursor→local divides by it (`:3471`).
* Readouts: `"DPI Scale {0}"` with a warning when `UIScaleRule == Custom` and the rule class fails
  to load (`:3300-3336`); `"Device Content Scale {0}"` (`:3338-3347`);
  `"No Device Safe Zone Set"` / `"Uniform Safe Zone: {0}"` from
  `FDisplayMetrics::GetDebugTitleSafeZoneRatio()` (`:3349-3358`).
* The **DPI Scaling** button (`:761-767`) calls
  `ISettingsModule::ShowViewer("Project", "Engine", "UI")` (`:3514-3519`).
* Selecting a screen size (`HandleOnCommonResolutionSelected`, `:3521-3600`) converts the pixel
  margin to a ratio (`/= Preview{Width,Height}/2`) and **broadcasts
  `FSlateApplication::Get().OnDebugSafeZoneChanged.Broadcast(SafeZoneRatio, true)`**
  (`:3563-3568`) — that is what makes every `SSafeZone`/`UCommonBorder` in the preview re-pad. The
  same block appears at `:974-990` (settings load) and `:3914-3920` (flip).
* Safe-zone overlay: `DrawSafeZone` (`:2187-2287`), only for `CustomOnScreen`, `DesiredOnScreen`,
  `FillScreen`; alpha 0.2, colour `(1.0, 0.5, 0.5, 0.2)`. No device profile → four bars from
  `GetSafeZoneSize` (`:2214-2258`); device profile → per-rect boxes from
  `CustomSafeZoneStarts`/`Dimensions`, flipped when `bSafeZoneFlipped` (`:2260-2285`).
* The preview reaches `USafeZone` through `FDesignerChangedEventArgs`
  (`bScreenPreview`, `Size`, `DpiScale`) built at `:2360-2373`.
* `FPlayScreenResolution` — `UnrealEd/Classes/Settings/LevelEditorPlaySettings.h:157-197`:
  `Description`, `Width=1920`, `Height=1080`, `AspectRatio`, `bCanSwapAspectRatio=true`,
  `ProfileName`, and three **transient** derived fields `ScaleFactor`, `LogicalHeight`,
  `LogicalWidth` filled from the device profile's `r.MobileContentScaleFactor`.
  `CalculateCustomUnsafeZones` / `FlipCustomUnsafeZones` (`.h:660-661`,
  `SettingsClasses.cpp:704-800+`) parse `r.CustomUnsafeZones` off the device profile, scale by the
  content-scale factor, transpose fixed zones when the orientation disagrees, and resolve negative
  starts as offsets from the far edge.

### 5.9 Version history for this area

| Change | Version | Evidence |
|---|---|---|
| `EUIScalingRule::ScaleToFit` + `DesignScreenSize` | **4.26** | absent at `?ref=4.25`, present at `?ref=4.26` (`.h:42-43`, `:186-188`); unchanged 4.26 → 5.8.2 |
| `bAllowHighDPIInGameMode` | ≤ **4.22** | present in every tag checked |
| `bAuthorizeAutomaticWidgetVariableCreation` | **5.2** | absent at `5.1.1`, present at `5.2.1` |
| `EFontDPI`, `FontDPIPreset`, `CustomFontDPI`, `bUseCustomFontDPI` (editor-only) | **5.3** | absent at `5.2.1`, added at `5.3.2` |
| class changed to `MinimalAPI` + per-member `ENGINE_API` | **5.3** | same diff |
| `bEnableDistanceFieldFontRasterization` | **5.5** | absent at `5.4.4`, present at `5.5.4` |
| header byte-identical 5.6.1 ↔ 5.7.4 | — | `diff` empty |
| Font DPI made **runtime** (properties + `GetFontDisplayDPI` / `ConvertToFontDPI` leave `WITH_EDITOR`); new `ConvertFontSizeFromNativeToDisplay` / `…FromDisplayToNative` | **5.8.0** | diff 5.7.4 → 5.8.0 |
| 5.8.0 → 5.8.2 in these files | cosmetic only (`UE_LOG` → `UE_LOGF`) | diff |
| `GetDPIScaleBasedOnSize` / `CalculateScale` algorithm | **unchanged since 4.26** | bodies identical across all fetched tags |
| `USafeZone`/`USafeZoneSlot` direct field access deprecated | **5.2** | `UE_DEPRECATED(5.2, …)` on `SafeZone.h:51,56,61,66`, `SafeZoneSlot.h:20,24,28,32,36` |

---

## 6. Styling and theming

### 6.1 `FSlateBrush`

`Runtime/SlateCore/Public/Styling/SlateBrush.h`

| Property | Type | Line | Notes |
|---|---|---|---|
| `ResourceObject` | `UObject*` | :474 | DisplayName "Image"; allowed classes `Texture`, `MaterialInterface`, `SlateTextureAtlasInterface`; `MediaTexture` disallowed |
| `TintColor` | `FSlateColor` | :247 | DisplayName "Tint", sRGB |
| `DrawAs` | `ESlateBrushDrawType::Type` | :251 | |
| `Tiling` | `ESlateBrushTileType::Type` | :255 | |
| `Mirroring` | `ESlateBrushMirrorType::Type` | :260 | |
| `ImageType` | `ESlateBrushImageType::Type` | :264 | AdvancedDisplay |
| `ImageSize` | `FVector2f` | :268 | the brush's own desired size |
| `Margin` | `FMargin` | :272 | `meta=(UVSpace="true")` — **9-slice, in UV 0..1** |
| `OutlineSettings` | `FSlateBrushOutlineSettings` | :479 | |
| `UVRegion` | `FBox2f` | :487 | sub-rect of the source texture |
| `ResourceName` | `FName` | :508 | AdvancedDisplay |
| `bIsDynamicallyLoaded` | bit | :491 | |

Enums:

* `ESlateBrushDrawType` (`SlateBrush.h:17-36`): `NoDrawType` (label "None"), **`Box`** (9-slice:
  corners fixed, edges stretched, centre stretched), **`Border`** (9-slice **without** the centre),
  **`Image`** (straight stretch), **`RoundedBox`**.
* `ESlateBrushTileType` (`:43-58`): `NoTile`, `Horizontal`, `Vertical`, `Both`.
* `ESlateBrushMirrorType` (`:66-81`): `NoMirror`, `Horizontal`, `Vertical`, `Both`.
* `ESlateBrushImageType` (`:89-105`): `NoImage`, `FullColor`, `Linear`, **`Vector`** (SVG-style,
  rasterised and cached at the requested size/scale).
* `ESlateBrushRoundingType` (`:112-123`): `FixedRadius` (use `CornerRadii`),
  `HalfHeightRadius` (always a pill).

`FSlateBrushOutlineSettings` (`SlateBrush.h:130-217`), used when `DrawAs == RoundedBox`:

| Property | Type | Line |
|---|---|---|
| `CornerRadii` | `FVector4` (TL, TR, BR, BL) | :200 |
| `Color` | `FSlateColor` (DisplayName "Outline", sRGB) | :204 |
| `Width` | `float` | :208 |
| `RoundingType` | `ESlateBrushRoundingType::Type` | :212 |
| `bUseBrushTransparency` | `bool` | :216 |

`RoundedBox` + outline settings already exist at `?ref=5.0.3-release`.

### 6.2 `FSlateColor` and foreground inheritance

`Runtime/SlateCore/Public/Styling/SlateColor.h`

`ESlateColorStylingMode` (`SlateColor.h:17-33`):

| Value | Details label | Meaning |
|---|---|---|
| `UseColor_Specified` | "Specified Color" | literal `SpecifiedColor` |
| `UseColor_ColorTable` | *(Hidden)* | index into the style colour table (`EStyleColor`) |
| `UseColor_Foreground` | "Foreground Color" | inherit the ambient foreground |
| `UseColor_Foreground_Subdued` | *(Hidden)* | ambient foreground × `SubdueAmount` |
| `UseColor_UseStyle` | *(Hidden)* | the foreground defined by a widget-specific style |

Properties: `SpecifiedColor` (`FLinearColor`, :233), `ColorUseRule` (:237).
Factories: `FSlateColor::UseForeground()` (:198), `UseSubduedForeground()` (:204),
`UseStyle()` (:210). Resolution happens in
`GetColor(const FWidgetStyle&)` (`SlateColor.h:104-120`).

**The inheritance chain.** `FWidgetStyle`
(`Runtime/SlateCore/Public/Styling/WidgetStyle.h`) is passed down through every `OnPaint` call and
carries:

* `ColorAndOpacityTint` (`:116`) — multiplied by `BlendColorAndOpacityTint` (`:33-36`) and
  alpha-multiplied by `BlendOpacity` (`:45-49`, this is where `RenderOpacity` lands).
* `ForegroundColor` — set by `SetForegroundColor(FLinearColor)` (`:58-63`) or
  `SetForegroundColor(TAttribute<FSlateColor>)` (`:77`).
* `SubduedForeground` (`:118`) — derived automatically as `Foreground * SubdueAmount` with the
  alpha further scaled (`:62-63`).

Any widget can inject a new foreground by overriding `virtual FSlateColor GetForegroundColor()`
(`SWidget.h:1582`) — `SBorder`, `SButton`, `SCompoundWidget` all do. There is also
`GetDisabledForegroundColor()` (`SWidget.h:1585`). Descendants that use
`FSlateColor::UseForeground()` then pick up whatever the nearest such ancestor set. At the UMG
level the entry point is `UUserWidget::ForegroundColor` (`UserWidget.h:1007`) alongside
`ColorAndOpacity` (`:996`) and `Padding` (`:1024`).

### 6.3 `USlateWidgetStyleAsset`

`Runtime/SlateCore/Public/Styling/SlateWidgetStyleAsset.h:24`: a `UObject` asset holding a single
`USlateWidgetStyleContainerBase* CustomStyle` (Instanced, EditAnywhere). Typed access via the
template `GetStyle<WidgetStyleType>()` (`:26`). Companion:
`USlateWidgetStyleContainerBase` / `ISlateWidgetStyleContainerInterface`
(`Styling/SlateWidgetStyleContainerBase.h`, `.../SlateWidgetStyleContainerInterface.h`) — the
container is the `UObject` wrapper that makes a `USTRUCT` style editable as an asset.

### 6.4 The concrete style structs

All in `Runtime/SlateCore/Public/Styling/SlateTypes.h`, all deriving `FSlateWidgetStyle`, all with
fluent `SetXxx()` chaining methods next to each property.

**`FButtonStyle`** (`SlateTypes.h:508`) — brushes `Normal` (:526), `Hovered` (:531),
`Pressed` (:536), `Disabled` (:541); colours `NormalForeground` (:546), `HoveredForeground` (:551),
`PressedForeground` (:556), `DisabledForeground` (:561); `NormalPadding` (:571),
`PressedPadding` (:579); sounds `PressedSlateSound` (:586), `ClickedSlateSound` (:593),
`HoveredSlateSound` (:600).

**`FTextBlockStyle`** (`:325`) — `Font` (`FSlateFontInfo`, :342), `ColorAndOpacity`
(`FSlateColor`, :371), `ShadowOffset` (:376), `ShadowColorAndOpacity` (:381),
`SelectedBackgroundColor` (:386), `HighlightColor` (:391), `HighlightShape` (:396),
`StrikeBrush` (:401), `UnderlineBrush` (:406), `TransformPolicy`
(`ETextTransformPolicy`, :411), `OverflowPolicy` (`ETextOverflowPolicy`, :416).

**`FCheckBoxStyle`** (`:104`) — `CheckBoxType` (`ESlateCheckBoxType::Type`, :121) plus **nine**
brushes: `UncheckedImage` (:126), `UncheckedHoveredImage` (:131), `UncheckedPressedImage` (:136),
`CheckedImage` (:141), `CheckedHoveredImage` (:146), `CheckedPressedImage` (:151),
`UndeterminedImage` (:156), `UndeterminedHoveredImage` (:161), `UndeterminedPressedImage` (:166);
`Padding` (:171); foregrounds `ForegroundColor` (:191), `HoveredForeground` (:196),
`PressedForeground` (:201), `CheckedForeground` (:206), `CheckedHoveredForeground` (:211),
`CheckedPressedForeground` (:216), `UndeterminedForeground` (:221); `BorderBackgroundColor` (:226);
sounds `CheckedSlateSound` (:233), `UncheckedSlateSound` (:240), `HoveredSlateSound` (:247).

**`FScrollBarStyle`** (`:931`) — `HorizontalBackgroundImage` (:948),
`VerticalBackgroundImage` (:953), `VerticalTopSlotImage` (:958), `HorizontalTopSlotImage` (:963),
`VerticalBottomSlotImage` (:968), `HorizontalBottomSlotImage` (:973), `NormalThumbImage` (:978),
`HoveredThumbImage` (:983), `DraggedThumbImage` (:988), `Thickness` (:992).

**`FEditableTextBoxStyle`** (`:1018`) — `BackgroundImageNormal` (:1036),
`BackgroundImageHovered` (:1041), `BackgroundImageFocused` (:1046),
`BackgroundImageReadOnly` (:1051), `Padding` (:1056), `TextStyle` (`FTextBlockStyle`, :1072),
`ForegroundColor` (:1077), `BackgroundColor` (:1082), `ReadOnlyForegroundColor` (:1087),
`FocusedForegroundColor` (:1092), `HScrollBarPadding` (:1097), `VScrollBarPadding` (:1102),
`ScrollBarStyle` (:1107).

Also available and directly usable by an author (each `EditAnywhere` on a UMG widget's
`WidgetStyle` property): `FComboButtonStyle` (:644), `FComboBoxStyle` (:741),
`FHyperlinkStyle` (:831), `FEditableTextStyle` (:867), `FInlineEditableTextBlockStyle` (:1133),
`FProgressBarStyle` (:1164, incl. `EnableFillAnimation` :1197), `FExpandableAreaStyle` (:1217),
`FSearchBoxStyle` (:1263), `FSliderStyle` (:1339), `FVolumeControlStyle` (:1408),
`FInlineTextImageStyle` (:1458), `FSpinBoxStyle` (:1488), `FSplitterStyle` (:1574),
`FTableViewStyle` (:1605), `FTableRowStyle` (:1640, incl. even/odd row brushes :1702/:1712 and
drop indicators :1727-1737), `FTableColumnHeaderStyle` (:1782), `FHeaderRowStyle` (:1848),
`FDockTabStyle` (:1910), `FScrollBoxStyle` (:2022), `FScrollBorderStyle` (:2105),
`FWindowStyle` (:2136).

### 6.5 Style sets — where `FCoreStyle` / `FAppStyle` / `FUMGCoreStyle` fit

* **`ISlateStyle`** (`Styling/ISlateStyle.h`) is the lookup interface: `GetBrush(Name)`,
  `GetWidgetStyle<T>(Name)`, `GetColor`, `GetSlateColor`, `GetMargin`, `GetFloat`, `GetVector`,
  `GetSound`. `FSlateStyleSet` is the concrete registry, registered in
  `FSlateStyleRegistry`.
* **`FCoreStyle`** (`Styling/CoreStyle.h`) — the style set Slate itself falls back to for core
  widgets. `FCoreStyle::Get()` (:33), `GetCoreStyle()` (:39), `GetDefaultFont()` (:45),
  `GetIconFont()` (:48), `GetDefaultFontStyle(Typeface, Size, Outline)` (:51),
  `RegularTextSize = 9`, `SmallTextSize = 8` (:70-71). `IsStarshipStyle()` is deprecated in **5.6**
  and now always returns true (`CoreStyle.h:73-74`) — the legacy pre-Starship style is gone.
* **`FAppStyle`** (`Styling/AppStyle.h:23`) — the *application* style, i.e. whatever style set the
  running app registered. The header states the intent (`AppStyle.h:14-16`): all core Slate
  application widgets should use `FAppStyle::Get()`, and both `FEditorStyle::Get` and
  `FCoreStyle::Get` accessors should be replaced by it. `SetAppStyleSetName`/`SetAppStyleSet`
  (:30-33) let a game swap the whole set at runtime. Thin static forwarders for every
  `ISlateStyle` getter (:36-88).
* **`FUMGCoreStyle`** (`Styling/UMGCoreStyle.h:12`) — the **runtime/game** counterpart of
  `FCoreStyle`, so a packaged game does not drag in editor styling. Same API surface
  (`Get()` :18, `GetDefaultFont()` :24, `GetDefaultFontStyle` :27, `ResetToDefault` :29,
  `SetSelectorColor`/`SetSelectionColor`/`SetInactiveSelectionColor`/`SetPressedSelectionColor`
  :32-35, `SetFocusBrush` :36) but `RegularTextSize = 10`, `SmallTextSize = 8` (:38-39).
* **`FStyleColors` / `EStyleColor`** (`Styling/StyleColors.h`) is the semantic colour table that
  `ESlateColorStylingMode::UseColor_ColorTable` indexes — the theming layer proper.
  `FStyleDefaults` (`Styling/StyleDefaults.h`) supplies the null/fallback brush and colours.

### 6.6 Style overrides on a Widget Blueprint

There is no single "style override" record on `UWidgetBlueprint`: a UMG widget stores its style
**by value** in a `UPROPERTY` on the widget itself (e.g. `UScrollBox::WidgetStyle`
(`ScrollBox.h:55`), `UScrollBox::WidgetBarStyle` (:60), `UBorder::Background`
(`Border.h:65`)), so editing it in the Details panel *is* the override, serialised into the
Widget Blueprint's CDO. `USlateWidgetStyleAsset` is the opt-in way to share one instead.

---

## Capability checklist

A flat, dense list of every author-facing capability and parameter found, for diffing against
another engine's UI system. `[!]` marks something the source shows is broken, dead, or a trap.

### A. Authoring surface — canvas

- Three application modes: Designer, Graph, Preview (Preview gated by `UMG.EnablePreviewMode`)
- Dockable tabs: Designer, Palette, Library, Hierarchy, Details, Animations, Bind Widgets,
  Navigation Simulation, Compiler Results, Tool Palette; per-tab enable flags in project settings
- Fixed 31-step zoom table 0.15 → 13.0, default 1:1; Ctrl gate above 1:1
- Wheel zoom about cursor; RMB/MMB pan; RMB+drag zoom; trackpad magnify and scroll gestures
- Zoom to fit (button + instant-on-open), animated scroll, 100 px fit padding
- Grid rendering with snap size, rule period 10, DPI-aware cell scale, antialias toggle
- Horizontal + vertical rulers in widget units, live cursor tick; **no user-placed guides**
- Live overlay HUD: zoom text, cursor position, selection dimensions, resolution, device content
  scale, safe-zone text, DPI-scale readout, transient message bar, PIE/recording/animation tints
- Preview background texture per user widget (`UUserWidget::PreviewBackground`)
- Widget Reflector launch button

### B. Authoring surface — screen size / DPI / safe zone preview

- Screen-size dropdown sourced from the shared PIE common-resolutions ToolMenu (device profiles)
- Screen fill rule: `FillScreen`, `Custom`, `CustomOnScreen`, `Desired`, `DesiredOnScreen`
- Explicit Width/Height entry (Custom modes only); persisted per project to `[UMGEditor.Designer]`
- Landscape↔portrait swap; safe-zone mirror/flip
- DPI preview via a real `SDPIScaler` (1.0 in Custom/Desired modes); "DPI Scaling" shortcut to
  Project Settings → Engine → User Interface
- Safe-zone overlay (uniform ratio or per-device unsafe rects), broadcast to live widgets
- Debug-resolution rects with 10 px snap while dragging the area handle
- `[!]` No "Apply DPI Scale" control, no "Show Safe Zone" toggle, no play-in-designer

### C. Authoring surface — selection, transform, extensions

- Selection outline (2 px green) and hover outline (2 px azure, 0.15 s fade)
- Eight resize handles; anchor-aware resize; Layout vs Render transform mode (W / E)
- Arrow-key nudge by grid snap size (`UPanelSlot::NudgeByDesigner`)
- Drag to move; Alt breaks parent lock; Shift locks to dominant axis; grid snap
- Anchor medallion: 9 draggable pieces, snaps to 0.1 increments (Shift disables), Ctrl zeroes
  offsets, live percentage guide lines
- Per-slot designer extensions: box shift arrows, grid/uniform-grid row/column arrows
- Extension API: 10 layout locations, offset + alignment attributes, paint hook, transactions
- `[!]` Sibling edge-snap guides exist but are `#if 0`-disabled

### D. Authoring surface — palette, hierarchy, details

- Palette: search, categories from `GetPaletteCategory`, Favorites section, star toggle,
  unloaded-asset templates, category hide lists, per-directory permission lists
- Library tab: tile/list view, thumbnails + thumbnail size
- Drag from palette → canvas with a **live instantiated drop preview**; drag → hierarchy
- Hierarchy: multi-select tree, rename in place, bold for variables, navigation-override and
  flow-direction-override glyphs, lock toggle (Shift = non-recursive), eye/hide toggle, named-slot
  rows, 0.3 s drag-hover auto-expand, expand/collapse all
- Context menu: Cut/Copy/Paste/Duplicate/Delete/Rename, Find References, Edit Widget Blueprint,
  **Wrap With…**, **Replace With…** (palette selection / child / named slot / any panel class)
- Details: category injection as `Slot (<SlotClass>)`, Is Variable checkbox, Category field,
  per-property **Bind** dropdown, Events category with add/jump buttons, keyframe (Sequencer)
  buttons, `DesignerRebuild` metadata forces preview rebuild
- Designer settings: grid snap enable + size {1,2,3,4,5,10,15,25}, lock-to-panel-on-drag,
  default preview resolution, show dashed outlines, execute PreConstruct, respect locks,
  compile-message create/dismiss policy, favourites

### E. Design-time semantics

- `EWidgetDesignFlags`: `None`, `Designing`, `ShowOutline`, `ExecutePreConstruct`, `Previewing`
- `IsDesignTime()`, `IsPreviewTime()`, `SetDesignerFlags` (propagates to children)
- `PreConstruct(bool IsDesignTime)` / `NativePreConstruct()`
- `OnDesignerChanged(FDesignerChangedEventArgs { bScreenPreview, Size, DpiScale })`
- Property bindings are suppressed at design time (`OPTIONAL_BINDING` gates on `!IsDesignTime()`)
- Two-object model: serialized template tree vs live preview instance; every edit writes both
- Per-widget editor state: `bHiddenInDesigner`, `bExpandedInDesigner`, `bLockedInDesigner`,
  `bIsVariable`, `DisplayLabel`, `CategoryName`
- Slot designer hooks: `NudgeByDesigner`, `DragDropPreviewByDesigner`, `SynchronizeFromTemplate`

### F. Anchors (canvas slot)

- `FAnchors { FVector2D Minimum, Maximum }` — normalised parent fractions
- `IsStretchedHorizontal()` / `IsStretchedVertical()` (min != max)
- `FAnchorData { FMargin Offsets, FAnchors Anchors, FVector2D Alignment }`
- `UCanvasPanelSlot`: `LayoutData`, `bAutoSize` ("Size To Content"), `ZOrder`
- Per-axis reinterpretation of `Offsets`: Position X/Y + Size X/Y when docked, Offset
  Left/Top/Right/Bottom when stretched — **independently per axis**
- Alignment = widget pivot (0..1), ignored on a stretched axis
- 16 anchor presets: 9-point grid, Top/Center/Bottom Fill, Fill Left/Center/Right, Fill/Fill
- Preset modifiers: Shift = also set Alignment, Ctrl = also reset Offsets
- Anchor rebasing on change (`SaveBaseLayout` / `RebaseLayout(PreserveSize)`)
- Canvas Z-order sorting; project setting `bExplicitCanvasChildZOrder` for layer batching

### G. Layout panels and per-slot parameters

| Panel | Panel params | Slot params |
|---|---|---|
| CanvasPanel | — | Offsets, Anchors, Alignment, Size To Content, ZOrder |
| Overlay | — | Padding, HAlign, VAlign |
| HorizontalBox | — | Size (`FSlateChildSize`), Padding, HAlign, VAlign |
| VerticalBox | — | Size, Padding, HAlign, VAlign |
| StackBox (5.1+) | Orientation | Padding, Size (default Automatic), HAlign/VAlign (default Fill) |
| ScrollBox | 23 properties (style, bar style, orientation, bar visibility/thickness/padding, consume-wheel, overscroll, front/back pad, animate wheel + interp speed, wheel multiplier, navigation destination + scroll padding, scroll-when-focus-changes, right-click drag, touch scrolling, consume pointer input, analog wheel key, focusable) | Size, Padding, HAlign, VAlign |
| GridPanel | ColumnFill[], RowFill[] | Padding, HAlign, VAlign, Row, RowSpan, Column, ColumnSpan, Layer, Nudge |
| UniformGridPanel | SlotPadding, MinDesiredSlotWidth, MinDesiredSlotHeight | HAlign, VAlign, Row, Column |
| WrapBox | InnerSlotPadding, WrapSize, bExplicitWrapSize, HAlign, Orientation | Padding, FillSpanWhenLessThan, HAlign, VAlign, bFillEmptySpace, bForceNewLine |
| SizeBox | Width/Height override, Min/Max desired width/height, Min/Max aspect ratio (each with an override bit) | Padding, HAlign, VAlign |
| ScaleBox | Stretch, StretchDirection, UserSpecifiedScale, IgnoreInheritedScale | HAlign, VAlign (`[!]` Padding deprecated) |
| Border | HAlign, VAlign, bShowEffectWhenDisabled, ContentColorAndOpacity, Padding, Background brush, BrushColor, DesiredSizeScale, bFlipForRightToLeftFlowDirection, 4 mouse events | Padding, HAlign, VAlign |
| Spacer | Size | — |
| SafeZone | PadLeft/Right/Top/Bottom | bIsTitleSafe, SafeAreaScale, HAlign, VAlign, Padding |
| InvalidationBox | bCanCache | — |
| RetainerBox | bRetainRender, RenderOnInvalidation, RenderOnPhase, Phase, PhaseCount, EffectMaterial, TextureParameter, bShowEffectsInDesigner | — |
| WidgetSwitcher | ActiveWidgetIndex | Padding, HAlign, VAlign |
| BackgroundBlur | Padding, HAlign, VAlign, bApplyAlphaToBlur, BlurStrength (0–100), bOverrideAutoRadiusCalculation, BlurRadius (0–255), CornerRadius (FVector4), LowQualityFallbackBrush | Padding, HAlign, VAlign |
| WindowTitleBarArea | (OS drag region) | Padding, HAlign, VAlign |
| NamedSlot | bExposeOnInstanceOnly, SlotGuid | — |
| Button | (style, click behaviour) | Padding, HAlign, VAlign |
| DynamicEntryBox | EntryBoxType {Horizontal, Vertical, Wrap, VerticalWrap, Radial, Overlay}, EntrySpacing, SpacingPattern, EntrySizeRule, EntryHAlign, EntryVAlign, MaxElementSize, RadialBoxSettings | — |
| `FRadialBoxSettings` | StartingAngle, bDistributeItemsEvenly, bClockwiseOrder, AngleBetweenItems, SectorCentralAngle, MarginSize | — |

- Not exposed as a UMG panel: `SRadialBox` (Slate only, reachable via DynamicEntryBox)

### H. Layout algorithm

- Two passes: `SlatePrepass(LayoutScaleMultiplier)` → `CacheDesiredSize` → `ComputeDesiredSize`
  bottom-up; `OnArrangeChildren(FGeometry, FArrangedChildren)` top-down
- `ComputeDesiredSize` takes the layout scale — desired size is DPI-dependent
- `FArrangedChildren` with a visibility filter; `AddWidget` drops rejected children
- `FGeometry` keeps two accumulated transforms: layout (scale+translate, affects hit-test and
  children) and render (full 2×2, affects pixels only); `MakeChild`, `ToPaintGeometry`
- `FSlateRect` (Left/Top/Right/Bottom) for culling and clip zones
- `FMargin` (Left/Top/Right/Bottom) reused as: slot padding, 9-slice brush margin (UV space),
  canvas offsets
- `EHorizontalAlignment { Fill, Left, Center, Right }`, `EVerticalAlignment { Fill, Top, Center, Bottom }`
- `AlignChild` returns `{ Offset, Size }`; RTL swaps both the alignment enum and the padding
- `FSlateChildSize { float Value (0..1), ESlateSizeRule::{Automatic, Fill} }`, defaults `1.0, Fill`
- Slate-only (not exposed in UMG): `SizeRule_StretchContent` with grow/shrink coefficients,
  per-slot `MinSize`/`MaxSize`, `bAllowShrink`, and a 5-pass flexbox-style relaxation solver (5.5+)
- Render transform: `FWidgetTransform { Translation, Scale (±5), Shear (±89°), Angle (±180°) }`
  + `RenderTransformPivot` (normalised); never affects layout
- `RenderOpacity` (0..1) → `FWidgetStyle::BlendOpacity` — multiplies the inherited tint alpha per
  widget; **not** a group flatten (use RetainerBox for that), and distinct from brush/tint alpha
- `EWidgetPixelSnapping { Inherit, Disabled, SnapToPixel }` (5.3+)
- Visibility: `Visible`, `Collapsed`, `Hidden`, `HitTestInvisible`, `SelfHitTestInvisible`;
  bitfield-backed; `Collapsed` alone skips the layout cursor advance and the desired-size
  contribution, `Hidden` still reserves space
- Clipping: `Inherit`, `ClipToBounds`, `ClipToBoundsWithoutIntersecting`, `ClipToBoundsAlways`,
  `OnDemand`; quad clip zones with axis-aligned fast path; **no batching across clip areas**
- Flow direction: `Inherit`, `Culture`, `LeftToRight`, `RightToLeft`
- Other per-widget knobs: ToolTipText / ToolTipWidget, bIsEnabled, Cursor override, bIsVolatile,
  Navigation, accessibility behaviour/text/summary + children-accessible flag

### I. DPI / resolution independence

- `EUIScalingRule { ShortestSide, LongestSide, Horizontal, Vertical, ScaleToFit, Custom }`
- `UIScaleCurve` (resolution → scale), default keys 480→0.444, 720→0.666, 1080→1.0, 8640→8.0
- `ApplicationScale` post-multiplier, floored at 0.01
- `DesignScreenSize` (ScaleToFit only), default 1920×1080
- `CustomScalingRuleClass` → `UDPICustomScalingRule::GetDPIScaleBasedOnSize` (called on the CDO)
- `RenderFocusRule { Always, NonPointer, NavigationOnly, Never }`
- `bAllowHighDPIInGameMode`, `bLoadWidgetsOnDedicatedServer`,
  `bAuthorizeAutomaticWidgetVariableCreation`, `bEnableDistanceFieldFontRasterization`
- Font DPI: `EFontDPI { Standard(72), Unreal(96), Custom }`, `CustomFontDPI`, `bUseCustomFontDPI`,
  native↔display font-size conversion (`[!]` cooked-vs-editor default asymmetry in 5.8)
- Cursors: `HardwareCursors` map (path + normalised hotspot, per-platform format search order),
  `SoftwareCursors` map (UMG widget per cursor type)
- Scale caching keyed on exact viewport size; **never cached in the editor**
- Application of scale: `SGameLayerManager` `SDPIScaler`, which **divides out the OS DPI scale**
- `SetUseFixedDPIValue(bool, FIntPoint)` for fixed-reference letterboxed UI
- `FSlateApplication::SetApplicationScale` (editor slider 0.5–3.0; no CVar)
- OS DPI: `SWindow::GetDPIScaleFactor`, `GetDPIScaleFactorAtPoint`, `EnableHighDPIAwareness` CVar,
  `InitHighDPI`, `-nohighdpi`
- `r.MobileContentScaleFactor` (backbuffer resolution, feeds device preview)

### J. Safe zones

- `SSafeZone` args: HAlign, VAlign, Padding, IsTitleSafe, SafeAreaScale (per-side multiplier),
  PadLeft/Right/Top/Bottom, OverrideScreenSize + OverrideDpiScale (editor only)
- `USafeZone` (PadLeft/Right/Top/Bottom) + `USafeZoneSlot` (bIsTitleSafe, SafeAreaScale, HAlign,
  VAlign, Padding)
- `FDisplayMetrics`: `TitleSafePaddingSize` (FVector4 L,T,R,B), `ActionSafePaddingSize`,
  `PrimaryDisplayWorkAreaRect`, `VirtualDisplayRect`, `MonitorInfo[]` (incl. per-monitor DPI),
  `RebuildDisplayMetrics`, `ApplyDefaultSafeZones`, `GetDebugTitleSafeZoneRatio`,
  `GetDebugActionSafeZoneRatio`
- Ratio→pixel conversion uses **half**-extents
- Safe margin arrives in screen pixels and must be divided by the layout scale
- CVars: `r.DebugSafeZone.TitleRatio`, `r.DebugActionZone.ActionRatio`, `r.DebugSafeZone.Mode`,
  `r.DebugSafeZone.OverlayAlpha`, `SafeZone.EnableOverrides`, `SafeZone.Ratio.{L,T,R,B}`,
  `SafeZone.Scale`, `SafeZone.EnableScale`, `SafeZone.Landscape.{L,T,R,B}` (iOS),
  `r.CustomUnsafeZones` (per-device notch rects)
- Console command `ShowTitleSafeArea` (deprecated → the CVar)
- Invalidation spine: `FCoreDelegates::OnSafeFrameChangedEvent` +
  `FSlateApplication::OnDebugSafeZoneChanged` (editor only)
- `[!]` `IsTitleSafe` / action-safe is dead: `SetTitleSafe` ignores its argument and
  `ActionSafePaddingSize` is read by nothing. Do not port the two-overlaid-zones pattern.
- `[!]` `r.DebugSafeZone.MaxDebugTitleSafeZoneRatio` does not exist
- CommonUI `UCommonBorder` subtracts (rather than adds) the safe margin, floored by
  `MinimumPadding`

### K. Styling and theming

- `FSlateBrush`: ResourceObject (texture / material / atlas), TintColor, DrawAs, Tiling, Mirroring,
  ImageType, ImageSize, Margin (UV-space 9-slice), OutlineSettings, UVRegion, ResourceName
- `ESlateBrushDrawType { NoDrawType, Box, Border, Image, RoundedBox }`
- `ESlateBrushTileType { NoTile, Horizontal, Vertical, Both }`
- `ESlateBrushMirrorType { NoMirror, Horizontal, Vertical, Both }`
- `ESlateBrushImageType { NoImage, FullColor, Linear, Vector }`
- `FSlateBrushOutlineSettings { CornerRadii (FVector4), Color, Width, RoundingType, bUseBrushTransparency }`
- `ESlateBrushRoundingType { FixedRadius, HalfHeightRadius }`
- `FSlateColor { SpecifiedColor, ColorUseRule }` with
  `ESlateColorStylingMode { UseColor_Specified, UseColor_ColorTable, UseColor_Foreground,
  UseColor_Foreground_Subdued, UseColor_UseStyle }`; factories `UseForeground()`,
  `UseSubduedForeground()`, `UseStyle()`
- Foreground chain: `FWidgetStyle { ColorAndOpacityTint, ForegroundColor, SubduedForeground }`
  threaded through `OnPaint`; `BlendColorAndOpacityTint`, `BlendOpacity`, `SetForegroundColor`;
  any widget can re-root it by overriding `SWidget::GetForegroundColor()` /
  `GetDisabledForegroundColor()`; UMG entry points `UUserWidget::ForegroundColor` /
  `ColorAndOpacity` / `Padding`
- `USlateWidgetStyleAsset` (holds one `USlateWidgetStyleContainerBase`, typed `GetStyle<T>()`)
- Style structs: `FButtonStyle` (4 brushes, 4 foregrounds, 2 paddings, 3 sounds),
  `FTextBlockStyle` (font, colour, shadow offset/colour, selected-bg, highlight colour + shape,
  strike, underline, transform policy, overflow policy), `FCheckBoxStyle` (type + 9 brushes +
  padding + 7 foregrounds + border bg + 3 sounds), `FScrollBarStyle` (9 brushes + thickness),
  `FEditableTextBoxStyle` (4 background brushes, padding, text style, 4 colours, 2 scrollbar
  paddings, scrollbar style); plus ComboButton, ComboBox, Hyperlink, EditableText,
  InlineEditableTextBlock, ProgressBar, ExpandableArea, SearchBox, Slider, VolumeControl,
  InlineTextImage, SpinBox, Splitter, TableView, TableRow, TableColumnHeader, HeaderRow, DockTab,
  ScrollBox, ScrollBorder, Window
- Style sets: `ISlateStyle` lookup (`GetBrush`, `GetWidgetStyle<T>`, `GetColor`, `GetSlateColor`,
  `GetMargin`, `GetFloat`, `GetVector`, `GetSound`); `FCoreStyle` (Slate fallback, RegularTextSize 9),
  `FUMGCoreStyle` (runtime/game counterpart, RegularTextSize 10), `FAppStyle` (the app's registered
  set — the one everything should use; swappable at runtime via `SetAppStyleSet`),
  `FStyleColors`/`EStyleColor` (semantic colour table), `FStyleDefaults`
- Widget-blueprint style overrides are stored **by value** on the widget's own `UPROPERTY`;
  `USlateWidgetStyleAsset` is the opt-in shared alternative
