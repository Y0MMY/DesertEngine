# UI rebuild — gap analysis against Unreal's Slate/UMG

Owner's criterion, in their words: *«копируем функциональность и возможности (параметры, контроллы итд)
то есть чтобы мы могли повторить UI не важно поверх 3d сцены или нет»*. So this is **capability
parity on our own RHI**, not a port of Slate. Where Slate's answer is a consequence of its own
history rather than of the problem, we are free to answer differently — but we have to answer.

Source material: `Docs/UI/UE_RESEARCH/` (UE 5.8.2, quoted from the source tree via `gh`, not from
docs). Our side: `Engine/Graphic/Render2D/`, `Engine/UI/`, `Docs/UI_ROADMAP.md`.

---

## 1. What our roadmap already names, and what it does not

`Docs/UI_ROADMAP.md` is a good and honest feature list — it is Unity-flavoured (RectTransform,
UGUI naming) and it tracks widgets, content types and authoring well. Two things it does **not**
name are structural, and both cost more the later they are answered:

### 1.1 There is no invalidation model — and the research says that is UE's default too

`UI::RenderCanvas2D` walks the whole ECS canvas tree and refills a `DrawList2D` from scratch on
every frame. There is no caching, no dirty tracking, and no notion of an element that did not change.

Slate has an entire subsystem for the alternative: `EInvalidateWidgetReason` (Layout / Paint /
Volatility / ChildOrder / RenderTransform / Prepass / …), `FSlateInvalidationRoot`,
`SInvalidationPanel`, plus a *volatility* concept for widgets that must repaint anyway.

**I first wrote this section as our largest gap. The source says otherwise, and the correction is
the useful part.** With both invalidation CVars at their defaults and no `SInvalidationPanel` —
which is the shipping engine configuration — Slate does a **full prepass of the whole tree, a full
paint producing a new draw element per visible widget, and a full re-batch, every frame**, and
`SWidget::Invalidate()` is literally a no-op (`SWidget.cpp:1346-1367`) because nothing needs
recording when everything repaints regardless. That is precisely what we do.

So we are not behind UE's behaviour; we lack UE's *option*. And the research is unusually clear
about what that option costs (`UE_RESEARCH/01_invalidation_and_batching.md` §A7–A8):

- per-widget memory for `FWidgetProxy` (≤32 B, static-asserted) **plus** a much larger
  `FSlateWidgetPersistentState` **plus** a retained cached-element list per widget;
- a **slow-path cliff**: any `InvalidateRootChildOrder()` forces `ClearAllFastPathData` +
  `BuildFastPathWidgetList` + `PaintSlowPath` — a full rebuild *and* a full repaint, strictly worse
  for that frame than having no invalidation at all. Under global invalidation the root is the whole
  window;
- a one-frame hitch Epic documents in its own source comment, two live `ensureMsgf` failure modes,
  and a set of widgets that must opt out — large enough that Epic grew an automatic recursive
  detector (`SupportsInvalidationRecursive`, 5.6+) to find them.

**Revised finding.** The decision is unmade, not wrong, and the likely correct answer is to keep
immediate mode. What is missing is the measurement that would let us say so honestly: what the full
walk costs as a function of canvas size, and at what element count it stops being free. If the
number says immediate mode carries the UI sizes we target, that is a **measured refusal** and it
gets recorded — which is worth more than the feature, because it stops the question being reopened
every time someone reads about Slate.

### 1.2 The layout model is axis-aligned rectangles, with no transform. This forecloses several roadmap items.

`Engine/UI/UILayout.hpp` resolves everything to `Rect{X,Y,W,H}`. There is no rotation, no scale, no
pivot, no render transform anywhere in the layout model — grep for `Rotation`/`Transform` in that
header returns nothing.

Slate separates **layout transform** from **render transform** (`RenderTransform`,
`RenderTransformPivot`), and the second is what lets a widget rotate or scale without disturbing the
layout of its siblings.

The consequences interlock, which is why this belongs in the plan rather than in a backlog:

- a rotated element is not expressible at all;
- our clipping is scissor-only (`DrawList2D::PushClipRect` → a GPU scissor rect), which is *correct
  precisely because* nothing can be rotated. Slate carries both methods
  (`FSlateClippingOp::EClippingMethod::Scissor | Stencil`) for exactly this reason;
- hit testing is `rect contains point`, which stops being valid the moment a transform exists;
- `DrawList2D` quads are min/max corner pairs, not four points.

**The forcing case is not hypothetical, and it is small.** Nothing in `UI_ROADMAP.md` asks for
rotation — `UITween` animates Offset / Size / Opacity / Color and no more — so it would be easy to
refuse this on the grounds that nobody has asked. But every UI toolkit ships a spinner, and UE ships
two (`UThrobber`, `UCircularThrobber`). A spinner can be built without a transform, by regenerating
its geometry each frame the way `AddRing` already does, so it is not by itself proof that we need
one. That is exactly why the question has to be asked deliberately rather than settled by the first
widget that needs it: the cheap answer for a spinner is a special case, and special cases are how a
system ends up with a transform anyway, spelled five different ways.

So "add rotation" is not a knob — it is a change to the layout model, the draw list, the batcher and
hit testing at once. **Decide before building more on top**, and if the decision is "we do not want
rotated UI", write that down as a refusal with its reason, because four subsystems are currently
relying on it silently.

---

### 1.3 Visibility is one bit on the canvas. UE has five states per widget, and they are load-bearing.

The widget-catalogue research flags this first, and our tree confirms it exactly:

- **Authored visibility.** `PROPERTY( DisplayName( "Visible" ) )` appears **once** in the whole UI
  component set, on `UICanvasComponent` (`Components.hpp:962`). `UILayoutData` has no visibility
  field at all. Hiding a single element requires either a `UIBinding` to a data-store key
  (`UICanvasRenderer2D.cpp:1091`) or putting it inside a `UIScreen`. An author cannot simply hide a
  panel.
- **Hit testing does not propagate.** `UICanvasRenderer2D.cpp:1112-1113` reads `Interactable` and
  `RaycastTarget` from *this* element only, and the walk descends into children regardless
  (`:1122`, `:1129`). So `RaycastTarget = false` gives us precisely UE's **SelfHitTestInvisible**,
  and UE's **HitTestInvisible** — this element *and its whole subtree* invisible to the pointer —
  **cannot be expressed at all**. Nor can "disable this entire panel": `Interactable` is per-element,
  so a modal dialog or a greyed-out form has to have the flag cleared on every descendant by hand.

UE's `ESlateVisibility` covers four independent axes with five values: drawn or not, participates in
layout or not, hit-tests itself or not, hit-tests its children or not. The layout axis is the one
that has no workaround here — **Collapsed removes the element from its parent's layout while Hidden
keeps its space** (one comparison, `LayoutUtils.h:1141-1145`), and inside a VBox/HBox that
distinction *is* the feature. We have neither word.

The research also explains why this is not cosmetic: **most Slate containers set themselves to
`SelfHitTestInvisible` in their own constructor**, so a system without the distinction has every
decorative panel eating clicks. We avoid that today only because our default is per-element rather
than inherited — i.e. we got the common case right by accident of the model, and the uncommon cases
are unreachable.

This is the single highest-value item in the whole catalogue against the owner's criterion, and it
is small: a visibility state on `UILayoutData` plus propagating two flags down the walk. Queued as У4.

**And it should not be a copy of `ESlateVisibility`.** That enum conflates two independent axes into
one five-valued word, which is why its names are awkward. Decomposed, UE's five states are:

| `ESlateVisibility` | drawn | takes layout space | self hit-tests | children hit-test |
|---|---|---|---|---|
| `Visible` | yes | yes | yes | yes |
| `Hidden` | no | yes | no | no |
| `Collapsed` | no | no | no | no |
| `HitTestInvisible` | yes | yes | no | no |
| `SelfHitTestInvisible` | yes | yes | no | yes |

So the underlying model is **Visibility {Visible, Hidden, Collapsed} × HitTest {All, ChildrenOnly,
None}** — two orthogonal fields, nine combinations, of which UE exposes five. Two fields are simpler
to author, simpler to serialize, strictly more expressive, and they match what our walk already does
(per-element flags). The owner's criterion is *capability* parity, and this reaches it without
inheriting the wart. `RaycastTarget`/`Interactable` fold into the second axis; keep whichever names
read better in the Details panel, but say in the migration which old flag became which.

## 2. Two defects found while surveying, both of the shape this project keeps hitting

### 2.1 The UI Editor panel draws through a second, obsolete renderer — and says otherwise

Filed as task У2, briefed. In short: `Engine/UI/UICanvasRenderer.{hpp,cpp}` (ImGui) handles **6** of
the **22** `UI*Component` types; its only remaining caller is
`Editor/Source/Editor/Panels/UI/UIEditorPanel.cpp:132`, whose comment on line 128 claims the preview
is "pixel-identical to what ships". The runtime and the editor's real pass both moved to
`RenderCanvas2D` and this panel did not. The panel's own create menu offers Slider, Toggle,
Dropdown, ScrollView, InputField and ProgressBar — its preview draws none of them.

### 2.2 The canvas walker keeps all of its runtime state in file-scope globals, keyed by entity id

`Engine/UI/UICanvasRenderer2D.cpp` holds, at namespace scope: `s_Tint`, `s_Hot`, `s_HotNext`,
`s_HotNextRect`, `s_PrevDown`, `s_Drag`, `s_FrameDt`, `s_HoverT`, `s_TweenT`, `s_Screen`,
`s_ScreenFrom`, `s_ScreenStack`, `s_ScreenT`, `s_ScreenBack`, `s_ScreenReq` and the transition
parameters. That is the entire runtime state of the UI — hover clocks, tween clocks, drag, focus
election, and the screen navigation stack — in one set of process-wide variables.

Two independent reasons this is wrong, and the second does not depend on the first:

**Multi-viewport.** `EditorLayer.cpp:1037` builds a `RenderRegistry` per scene document and each one
constructs its own `EditorUIPass`, so with two scene views open two `RenderCanvas2D` calls run per
frame over two different registries, both mutating the same globals. This is the same rule the
engine already states for renderer state — per (frame × renderer slot), `Docs/RENDERER_FRAME_STATE.md`
— applied to a system that predates nobody checking.

**Entity ids are not unique across registries.** `s_HoverT` and `s_TweenT` are
`unordered_map<entt::entity, float>`. Entity 7 of scene A and entity 7 of scene B are the same key.
Two unrelated elements in two unrelated scenes share one hover clock and one tween clock. This is
the recurring keying defect — a map keyed by something that is not unique over the domain it is used
in, exactly as in Д13 (the pipeline cache dropping five key fields).

Note what is **not** wrong here: keeping runtime state *outside the ECS components* was a deliberate
and correct decision (it is why navigating in the editor never rewrites the scene, `UI_ROADMAP.md`
F). And `Editor::Core::UIPreview` is a documented, reasoned singleton — preview follows the focused
viewport, which is what a single set of input state should do. The defect is only that the canvas
walker's state went into file statics instead of a per-canvas context.

---

### 2.3 The part of the UI that holds all the state is compiled by no test suite

`scripts/CI/UnreachedSources.sh` on the current tree: 95 suites, 50 sources compiled by at least
one, **275 by none**. Among the 275 are `Engine/UI/UICanvasRenderer2D.cpp`,
`Engine/UI/UICanvasRenderer.cpp`, `Engine/UI/UIDataStore.cpp`, `Engine/Graphic/Render2D/Render2D.cpp`
and `Engine/Scripting/UIBindings.cpp`.

The split is not random, and it is worth naming because it says where to aim. The **pure** pieces are
covered: `UILayout.hpp`'s layout maths is unit-tested, and `DrawList2D.cpp` does not appear in the
unreached list — it was deliberately built free of GPU, Vulkan and ECS dependencies so it could be
tested, and that paid off. What is untested is the **walker** — the 92 KB file that holds every flag
read, the hit-test election, the screen stack, the drag state and all the globals of §2.2. Every
defect in §2 lives in the one file nothing executes.

So the relation tests owed by У3 and У4 are not extra credit: they would be the **first coverage this
subsystem has ever had**, and the reason they are reachable at all is that `RenderCanvas2D` takes a
plain `entt::registry` and a GPU-free `DrawList2D`. That is a testable seam that already exists and
nobody has used.

## 3. Capability gaps that are real but ordinary

These are feature work, not architecture, and belong in the roadmap rather than ahead of it. Listed
here only where the UE research changed what we thought.

- **Text is single-channel SDF; UE moved to MSDF.** `EFontRasterizationMode` = Bitmap / Msdf / Sdf /
  SdfApproximation, shipped in 5.5, selected per device profile, with per-font-face ppem tiers
  (Min/Mid/Max, separately for SDF and MSDF) and a dedicated atlas content type. Our roadmap section
  E does not mention MSDF at all. MSDF is what keeps corners sharp — the exact complaint SDF text
  produces. Details in `UE_RESEARCH/05_rhirenderer_and_text.md`.
- **Backdrop blur.** Ours is a pyramid-LOD pick off the bloom downsample chain, which is cheap and
  was the right call. UE's default is Dual-Kawase with a closed-form sigma→levels curve, with the
  separable Gaussian kept as the legacy path, and `SBackgroundBlur` auto-derives radius as
  3×strength with downsample tiers at kernel ≥9/64/96. Worth reading before anyone tunes ours.
- **Batch key.** Ours is {Texture, Text, ClipRect, Glass} — four fields. Slate's two-stage keys carry
  9 and 13. The gap is mostly features we do not have yet (materials on widgets, blend modes, custom
  shaders), so it is a consequence, not a defect. *(Minor: the doc comment on
  `DrawList2D::CurrentCommand` says the key is "texture + text mode" while the code at
  `DrawList2D.cpp:49-50` also keys on clip rect and glass. Fix in passing.)*
- **No material/custom shader per element** (roadmap G, already `[ ]`). Slate has UI-domain materials
  and `FSlateMaterialShader`. This is the thing that makes "custom widget look" open-ended instead of
  a fixed effect list, and it is worth more than several of the remaining widget checkboxes.
- **No render-to-texture widget path.** Slate has `FWidgetRenderer` / `ISlate3DRenderer` /
  `RetainerBox` / `UWidgetComponent`. Roadmap A has "3D model / render-texture" as `[ ]` and G has
  "UI on an arbitrary 3D surface" as todo — the UE research says these are one mechanism, not two.
- **No introspection at all, and our roadmap does not have a line for it.** UE ships a Widget
  Reflector (pick an element on screen, see its tree, geometry, paint counts), a
  `SlateDebugger.*` console family, `stat Slate`, and Slate Insights traces —
  `UE_RESEARCH/02_debugging_and_introspection.md`. We have none of it. This matters more than its
  position in a feature list suggests: the four most expensive defects in this engine shipped
  *built, tested and unseen*, and a UI toolkit with no way to ask "why is this element here / this
  size / drawn in this batch" reproduces exactly that failure mode in a new subsystem. Cheap to
  start: our batcher already knows batch counts and clip rects, and `s_Hot` already elects a
  picked element.

---

## 3a. Three places where we already match, and it is worth writing down

Parity work goes wrong when it treats every difference as a gap. These came back from the research
as *confirmations*, and each one closes a question rather than opening one:

- **Opacity down the tree.** Slate's `RenderOpacity` is not a group flatten either — it multiplies
  the inherited style tint alpha per widget, so overlapping children double-darken there exactly as
  they do here with `s_Tint`. UE's answer to true flattening is `URetainerBox`, i.e. render the
  subtree to a texture. So our approach is the same as theirs, and the missing piece is not a fix to
  opacity but the render-to-texture path already listed in §3.
- **Batching cannot cross a clip.** Slate cannot batch across clipping areas; our `ClipRect` is part
  of the batch key for the same reason. Same cost model, arrived at independently.
- **Anchor presets.** Ours (Unity's 4×4 + Fill) and UE's 16 are the same grid.

And one deliberate divergence worth stating rather than drifting into: UE's designer keeps **two
object trees** — a serialized template and a live preview instance — and every designer edit writes
both. We author directly in the ECS scene, which is simpler and fits a scene-based engine. The cost
is that UE's *widget templates* fall out of that two-object model for free, and our equivalent
(roadmap I, "UI Prefabs") does not. If prefabs are wanted, that is where the price is paid.

## 3b. Traps named by the research, to spend once rather than twice

- **OS DPI must cancel out.** `SGameLayerManager.cpp:496` divides the platform DPI scale back out
  because the UI scale curve is authored against raw pixels. Get one half right and hi-DPI
  double-scales. Relevant the moment roadmap B's DPI work starts.
- **Slate's own dead code**: `SSafeZone::IsTitleSafe` ignores its argument and
  `FDisplayMetrics::ActionSafePaddingSize` is written by four platforms and read by nobody — so the
  two-overlaid-safe-zones pattern in Slate's own header comment does not work. Do not copy it.
- **Slate exposes more than UMG does.** Slate 5.5+ has a flexbox grow/shrink solver with per-slot
  `MinSize`/`MaxSize` and `SizeRule_StretchContent`; UMG's box slots expose only
  Size/Padding/HAlign/VAlign. Our roadmap B lists "Flex / Wrap todo" — building it means matching
  *Slate*, not UMG, and the owner's criterion is capability, so Slate is the right target.

## 4. What this changes about the plan

Nothing in section 3 should start before section 1 is decided and section 2 is fixed. The order is:

1. **У2** — one canvas renderer, the obsolete one deleted, the census made compile-time. *(briefed)*
2. **У3** — the walker's runtime state moves out of file statics into a per-canvas context keyed
   properly. Small, mechanical, and it stops a class of bug we cannot currently see. *(to brief;
   sequenced after У2 because both touch `RenderCanvas2D`'s call sites)*
2a. **У4** — visibility as a state per element (§1.3), and the two hit-test flags made to propagate.
   Independent of У2/У3 in files: it is the component plus the walk's flag reads.
3. **Measure the full-walk cost** as a function of canvas size, and decide invalidation on the
   number. Note the expected answer after §1.1: immediate mode is what UE ships by default, so the
   likely deliverable here is a **recorded refusal with its measurement**, not a subsystem.
4. **Decide the transform question** (§1.2) explicitly, either way, in writing.
5. Then feature parity, from the roadmap, informed by the widget-catalogue and designer reports.
