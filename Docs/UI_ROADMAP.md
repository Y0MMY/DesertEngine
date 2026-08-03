# DesertEngine — Production UI Roadmap (UE/Unity-parity, 2026)

Goal: a complete, production-grade in-engine UI toolkit — authored WYSIWYG in the 3D
viewport, rendered identically at runtime, on par with Unity UGUI/UI Toolkit and UE UMG.

Rendering baseline today (2026-08-03): UI is drawn by the engine's own **`Render2D`** batcher
(`Engine/Graphic/Render2D/` — `DrawList2D` CPU geometry + GPU batcher, custom `UI2D`/`UIText`
SDF shaders), NOT ImGui. The scene walker is `Engine/UI/UICanvasRenderer2D`. The **runtime is
fully ImGui-free** (own swapchain present + Render2D UI/splash); the editor still uses ImGui only
for authoring chrome. Runtime dispatches `UIButton` actions. Assets are cooked; `TextureAsset`
exists; there is a Sequencer panel; Lua scripting exists.

Legend: [x] done · [~] partial · [ ] todo · (dep) needs a third-party dependency (must be
approved per the engine's "no magic dependencies" rule).

---

## A. Content types (what can live in a UI element)
- [x] Text (custom font asset, word-wrap, rich text — see E)
- [~] Image / Sprite (PNG/JPG/TGA), **9-slice**, tint — done on Panel/Button/Canvas via `AssetHandle`; atlas/tiling/flip todo
- [ ] Icon fonts / vector icons
- [ ] **GIF** (animated image, per-frame) — stb_image can decode GIF frames (likely no dep)
- [ ] **Video** — mp4/H.264 (dep: ffmpeg/libav, heavy) OR MPEG1/webm (dep: pl_mpeg/libvpx, light) + audio sync
- [ ] **SVG** (vector) — (dep: nanosvg, tiny) rasterized to texture on import
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
- [ ] Content Size Fitter, Aspect Ratio Fitter, Layout Element (min/preferred/flex)
- [ ] Constraints (pin/relative), safe-area (mobile notches), responsive breakpoints
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
- [~] State set: normal / hover / pressed / focused done; disabled / selected todo
- [~] **Focus + navigation** — keyboard Tab-order + Enter-activate done; gamepad D-pad, back button todo
- [ ] **Touch + gestures** (tap/long-press/swipe/drag/pinch), multi-touch
- [ ] **Drag & drop** between UI elements (inventory), drop targets
- [~] Event system: click + scroll + text-typed wired; per-element callbacks, bubbling/capture, enter/exit todo
- [ ] Raycast/hit blocking, interactable/raycast-target flags

## E. Text & fonts (production)
- [~] Custom fonts — TTF asset via `AssetHandle` (preloaded dropdown + `.ttf` drag-drop), on 3D Text + UIText; font fallback chain / emoji todo
- [~] DPI/scale sizing, word-wrap, ellipsis, auto-size, line spacing, vertical align done; full justification todo
- [~] **Rich text** (BBCode) — `[color=#rrggbb]`, `[b]` faux-bold done; italic / links / inline sprites todo
- [ ] Complex scripts: RTL/bidi, CJK, shaping (dep: HarfBuzz) — later
- [x] Text effects: outline, shadow, gradient, glow

## F. Animation
- [ ] Tween/easing (from->to, curves), triggers (on-show/hover/click)
- [ ] Transitions between screens/states, UI **state machine**
- [ ] **UI Sequencer** (reuse engine Sequencer): tracks on position/color/opacity/scale
- [ ] Spring/physics motion; Lottie playback (dep)

## G. Rendering & effects
- [~] **Masking / clipping** — rect mask (`PushClipRect` → scissor) done; alpha/sprite mask todo
- [ ] Sorting: z-order, sorting layers, multiple canvases (overlay / world-space / camera-space)
- [~] **World-space canvas** (billboarded UI at a 3D pos) done; UI on an arbitrary 3D surface + camera-space todo
- [~] Effects: drop shadow, outline, gradient, glow done; **backdrop blur / glass**, blend modes todo
- [ ] Custom UI shaders/materials; post-processing scoped to UI
- [x] Batching to a real UI mesh — `Render2D` merges same-state quads into batched draw calls
- [ ] Offscreen culling, element pooling for lists

## H. Data & scripting
- [ ] Data binding (MVVM-lite): bind text/value/visibility/color to game data
- [ ] **Lua hooks**: on-click -> Lua fn, `ui.find()`, set text/color, spawn lists from data
- [ ] Data-driven lists (bind a collection -> virtualized list)

## I. Editor authoring (WYSIWYG, prod UX)
- [x] In-scene render + create menu + click-select + anchor presets + 2D mode
- [x] **Drag-move + resize handles + anchor handles** (mouse) + snapping to parent edges/centre
- [ ] Multi-select, copy/paste/duplicate, align/space tools, rulers/guides, alignment guides + distribute
- [ ] **UI Prefabs** (reusable widgets) + nested prefabs + variants
- [~] anchor-preset widget in Details (`UIAnchorControls`) done; widget palette/library todo
- [ ] Undo/redo for all UI edits; device-preset preview (phone/tablet/desktop/4K)
- [ ] Styles/Themes as assets (colors/fonts/spacing), theme switching

## J. App/runtime level
- [x] **Splash screen** (image, duration, fade) in `SceneSettings`, shown on scene load
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
6. 🔶 **Auto-layout containers** — VBox/HBox/Grid done; **fitters + safe-area next**. (B)
7. 🔶 **Controls + input** — Toggle/Slider/ProgressBar/InputField/ScrollView/Dropdown + Tab-focus done; disabled/selected states, drag&drop, gamepad nav todo. (C/D)
   — Also landed early, out of order: ✅ custom `Render2D` GPU backend (UI no longer ImGui) + ImGui-free runtime + batching (G/L partial).
8. **3D model / render-texture element** + particles-in-UI. (A)
9. **GIF** *(likely no dep)* → then **SVG** (nanosvg) → **Video** (pl_mpeg light, or ffmpeg) — dep sign-off. (A)
10. **UI animation / Sequencer** (tweens, state machine, transitions, Lottie). (F)
11. **Data binding + Lua** hooks; data-driven virtualized lists. (H)
12. **Prefabs / themes / undo-redo / device-preview** (editor polish). (I)
13. **Localization + accessibility**. (K)
14. **Perf pass**: batch to UI mesh, culling, pooling, atlases. (G/L)

**Next up (dependency-free):** B fitters (Content Size Fitter / Aspect Ratio Fitter) + safe-area,
then finish C/D (disabled/selected states, drag & drop between elements, pointer enter/exit events).

Dependencies to approve when reached: nanosvg (SVG), pl_mpeg/libvpx or ffmpeg (video),
rlottie (Lottie), HarfBuzz (complex text). Each: justify size/license/compile-time first.
