# DesertEngine — Production UI Roadmap (UE/Unity-parity, 2026)

Goal: a complete, production-grade in-engine UI toolkit — authored WYSIWYG in the 3D
viewport, rendered identically at runtime, on par with Unity UGUI/UI Toolkit and UE UMG.

Rendering baseline today (2026-08-04): UI is drawn by the engine's own **`Render2D`** batcher
(`Engine/Graphic/Render2D/` — `DrawList2D` CPU geometry + GPU batcher, custom `UI2D`/`UIText`
SDF shaders), NOT ImGui. The scene walker is `Engine/UI/UICanvasRenderer2D`. The **runtime is
fully ImGui-free** (own swapchain present + Render2D UI/splash); the editor still uses ImGui only
for authoring chrome. Runtime dispatches `UIButton` actions. Assets are cooked; `TextureAsset`
exists; there is a Sequencer panel; Lua scripting exists.

Since the 2026-08-03 sync we also shipped: **media** (animated GIF + MPEG1 video, video as an
`AssetHandle` w/ drag-drop), **icons** (`UIIcon` vector set + `UIImage` sprite blocks), **shapes**
(rounded corners, circle/ring), **Phase B fitters** (Content Size / Aspect Ratio / Layout Element,
Canvas safe-area), **Phase D** disabled/selected states, **Phase F** first tweens (pulse / ticker /
eased hover), and editor authoring — **Design↔Preview** play-in-editor, click-selects-control +
Alt-drill, reveal-in-Outliner, picking inside auto-layout groups — plus a **MainMenu** example scene.

Legend: [x] done · [~] partial · [ ] todo · (dep) needs a third-party dependency (must be
approved per the engine's "no magic dependencies" rule).

---

## A. Content types (what can live in a UI element)
- [x] Text (custom font asset, word-wrap, rich text — see E)
- [~] Image / Sprite (PNG/JPG/TGA), **9-slice**, tint — done on Panel/Button/Canvas via `AssetHandle`; empty Image draws nothing; atlas/tiling/flip todo
- [x] **Icons** — built-in vector icon set (`UIIcon`, drawn with Render2D primitives) + `UIImage` sprite icon blocks (used by the MainMenu). *(SDF icon atlas — the replacement for SVG — still todo)*
- [x] **Shapes** — rounded-rect corner radius (`AddRectFilled`), **circular panels + gradient ring** (avatars / status rings)
- [x] **GIF** (animated image, per-frame) — decoded frames driven on the atlas (no new dep)
- [~] **Video** — **MPEG1 as UI content** + **video as an `AssetHandle` (drag-drop)** done; H.264/webm + audio sync todo
- ~~SVG~~ — **dropped 2026-08-04.** Rasterising an SVG on import just produces the PNG you could have
      exported from the design tool, and nanosvg only parses a subset (no CSS/filters/text), so real
      files break. Where resolution independence actually matters, use `UIIcon` (vector primitives) or
      the planned **SDF icon atlas**, which reuses the existing font SDF pipeline and gets tint /
      outline / glow for free. If designers ever hand over SVGs, convert them offline in the asset
      pipeline — the engine stays PNG/SDF only.
- [ ] **Lottie** (After-Effects JSON vector animation) — (dep: rlottie) — modern motion UI
- [ ] **3D model / render-texture** — render a model or a whole scene into a UI element
      (inventory/char-select/turntable). Reuse the per-instance SceneRenderer -> offscreen
      target -> ImTextureID. Interactive (orbit) optional.
- [ ] **Particles / VFX in UI** (screen-space emitters over/under UI)
- [ ] **Custom shader / material on UI** (gradient, glass/backdrop blur, masked reveal, glow)
- [ ] Procedural/dynamic textures (minimap, charts/graphs, health bars as shaders)

## B. Layout
- [x] RectTransform: anchors (Anchor Min/Max), offsets, pivot
- [x] Anchor presets (Unity 4x4) + Fill
- [x] Canvas Scale Mode (Stretch / ScaleWithScreen / Letterbox)
- [~] Auto-layout containers — **VBox / HBox / Grid** done (`UILayoutGroup`, padding/spacing, unit-tested); Flex / Wrap todo
- [x] **Content Size Fitter** (hug children), **Aspect Ratio Fitter**, **Layout Element** flexible grow (in VBox/HBox)
- [~] **Canvas safe-area insets** done; constraints (pin/relative), responsive breakpoints todo
- [ ] Pixel-perfect mode

## C. Controls / widgets
- [x] Panel, Text, Button
- [~] Image, Toggle/Checkbox done; RawImage (render-texture), Radio group todo
- [~] Slider, ProgressBar done; Range, Spinner, Stepper todo
- [~] InputField (single-line), Dropdown/ComboBox done; multi-line/password/IME, ColorPicker todo
- [~] ScrollView (+ wheel) done; ScrollBar visual, **virtualized ListView / GridView / TreeView / Table** todo
- [ ] TabView, Accordion, Splitter, Drawer/Sidebar, MenuBar/ContextMenu, Tooltip
- [ ] Modal/Dialog/Window, Toast/Notification, Carousel, Rating, Chips, Breadcrumb, Pagination

## D. Interaction & input
- [x] Mouse click-select (editor), button hover/press (runtime)
- [x] State set: normal / hover / pressed / focused / **disabled / selected** (per-state button sprites + tints)
- [~] **Focus + navigation** — keyboard Tab-order + Enter-activate done; gamepad D-pad, back button todo
- [ ] **Touch + gestures** (tap/long-press/swipe/drag/pinch), multi-touch
- [x] **Drag & drop** between UI elements — `UIDraggable` (payload + ghost) / `UIDropTarget` (prefix filter,
      drop message, live highlight of every target that accepts); press-and-move threshold so a click stays a click
- [~] Event system: click + scroll + text-typed + **pointer enter/exit/press/release** (`UIPointerEvents`,
      dispatched through the same channel as button actions); bubbling/capture still todo
- [x] Raycast/hit blocking — one HOT element elected per frame in draw order (topmost wins, clipped-away
      pixels excluded), with **Interactable** / **Raycast Target** flags on every UILayout

## E. Text & fonts (production)
- [~] Custom fonts — TTF asset via `AssetHandle` (preloaded dropdown + `.ttf` drag-drop), on 3D Text + UIText;
      **UTF-8 throughout** (Cyrillic/CJK/emoji render if the font carries them — glyphs are baked on demand);
      font fallback chain todo
- [~] DPI/scale sizing, word-wrap, ellipsis, auto-size, line spacing, vertical align done; full justification todo
- [~] **Rich text** (BBCode) — `[color=#rrggbb]`, `[b]` faux-bold done; italic / links / inline sprites todo
- [ ] Complex scripts: RTL/bidi, CJK, shaping (dep: HarfBuzz) — later
- [x] Text effects: outline, shadow, gradient, glow

## F. Animation
- [~] Tween/easing + triggers — **pulse, marquee ticker, eased hover transitions** done; generic from→to curve tracks todo
- [ ] Transitions between screens/states, UI **state machine**
- [ ] **UI Sequencer** (reuse engine Sequencer): tracks on position/color/opacity/scale
- [ ] Spring/physics motion; Lottie playback (dep)

## G. Rendering & effects
- [~] **Masking / clipping** — rect mask (`PushClipRect` → scissor) done; alpha/sprite mask todo
- [ ] Sorting: z-order, sorting layers, multiple canvases (overlay / world-space / camera-space)
- [~] **World-space canvas** (billboarded UI at a 3D pos) done; UI on an arbitrary 3D surface + camera-space todo
- [~] Effects: drop shadow, outline, gradient, glow done; **backdrop blur / glass**, blend modes todo
- [x] Primitives: **rounded-rect corner radius**, circle/ring, `AddLine` (used by icons/shapes)
- [ ] Custom UI shaders/materials; post-processing scoped to UI
- [x] Batching to a real UI mesh — `Render2D` merges same-state quads into batched draw calls
- [ ] Offscreen culling, element pooling for lists

## H. Data & scripting
- [ ] Data binding (MVVM-lite): bind text/value/visibility/color to game data
- [ ] **Lua hooks**: on-click -> Lua fn, `ui.find()`, set text/color, spawn lists from data
- [ ] Data-driven lists (bind a collection -> virtualized list)

## I. Editor authoring (WYSIWYG, prod UX)
- [x] In-scene render + create menu + anchor presets + 2D mode
- [x] **Click selects the control** (not the Scrim), **Alt-click drills into children**; pick/handles work **inside auto-layout groups**
- [x] **Design ↔ Preview toggle** — play-in-editor for UI (buttons interactive, authoring overlays hidden), like UE
- [x] **Reveal selection in the Outliner** — expand ancestors + scroll into view (works for hidden/nested UI)
- [x] **Drag-move + resize handles + anchor handles** (mouse) + snapping to parent edges/centre
- [ ] Multi-select, copy/paste/duplicate, align/space tools, rulers/guides, alignment guides + distribute
- [ ] **UI Prefabs** (reusable widgets) + nested prefabs + variants
- [~] anchor-preset widget in Details (`UIAnchorControls`) done; widget palette/library todo
- [ ] Undo/redo for all UI edits; device-preset preview (phone/tablet/desktop/4K)
- [ ] Styles/Themes as assets (colors/fonts/spacing), theme switching

## J. App/runtime level
- [x] **Splash screen** (image, duration, fade) in `SceneSettings`, shown on scene load
- [x] **Example: MainMenu** authored as a loadable `.desce` (sprite icon blocks, buttons, layout) — reference scene
- [ ] **Screen/navigation stack** (pages, push/pop, back), scene transitions (fade/slide)
- [x] Button actions: SendMessage / LoadScene / Quit / OpenURL (Custom-Lua todo)
- [ ] Audio feedback (click/hover sounds), haptics (mobile)
- [ ] In-game debug/console UI, pause menu, HUD system

## K. Localization & accessibility
- [ ] i18n: string tables, runtime language switch, plurals/gender, number/date/currency
- [ ] Per-locale fonts, RTL layout mirroring
- [ ] Accessibility: font scaling, high-contrast/colorblind, screen-reader hints, focus outlines

## L. Platform & performance
- [ ] Mobile: touch, safe-area, on-screen/virtual keyboard, orientation
- [ ] Perf: batching, culling, list virtualization/pooling, atlas management, texture streaming
- [ ] Multi-resolution/DPI validation; memory budget

---

## Suggested phased order (value / risk / dependency-free first)

1. ✅ **Interactive editing** — drag-move, resize handles, anchor handles, snapping. (I)
2. ✅ **Sprites / images** — Image on Panel/Button/Canvas, 9-slice, tint. (A/C) *(atlas todo)*
3. ✅ **Button actions + scene switch + splash screen**. (J) *(audio feedback todo)*
4. ✅ **Masking/clipping + effects** (shadow/outline/gradient/glow) + world-space canvas. (G)
5. ✅ **Fonts & rich text** — custom TTF asset, wrap/multi-line/auto-size/overflow, BBCode. (E) *(RTL/CJK/emoji deferred — HarfBuzz)*
6. ✅ **Auto-layout + fitters** — VBox/HBox/Grid + Content Size / Aspect Ratio / Layout Element + Canvas safe-area. (B) *(Flex/Wrap todo)*
7. 🔶 **Controls + input** — Toggle/Slider/ProgressBar/InputField/ScrollView/Dropdown + Tab-focus + disabled/selected states + **z-ordered hit testing, pointer events, drag & drop** done; gamepad nav + bubbling todo. (C/D)
   — Also landed early, out of order: ✅ custom `Render2D` GPU backend (UI no longer ImGui) + ImGui-free runtime + batching (G/L partial).
8. ✅ **Media** — animated GIF + MPEG1 video, video as an `AssetHandle` (drag-drop). (A) *(H.264/webm + audio sync todo)*
9. ✅ **Icons + shapes** — `UIIcon` vector set + sprite icon blocks; rounded corners, circle/ring. (A/G)
10. ✅ **Editor authoring polish** — Design↔Preview play-in-editor, click-selects-control + Alt-drill, reveal-in-Outliner, pick inside layout groups; **MainMenu** example scene. (I/J)
11. 🔶 **UI animation / Sequencer** — first tweens (pulse/ticker/eased hover) done; generic curve tracks, state machine, transitions, Lottie todo. (F)
12. **3D model / render-texture element** + particles-in-UI; richer **Video** (H.264/audio). (A)
13. **Data binding + Lua** hooks; data-driven virtualized lists. (H)
14. **Prefabs / themes / undo-redo / device-preview** (editor polish). (I)
15. **Localization + accessibility** (K); **Perf pass** — culling, pooling, atlases (G/L).

**Next up (dependency-free):** gamepad/D-pad navigation + event bubbling to finish D; then F generic
tween tracks + screen-transition state machine; A render-texture element (reuse per-instance
SceneRenderer → offscreen target).

Dependencies to approve when reached: pl_mpeg/libvpx or ffmpeg (video), rlottie (Lottie),
HarfBuzz (complex text). Each: justify size/license/compile-time first. (nanosvg was considered for
SVG and rejected — see A.)
