# DesertEngine — Production UI Roadmap (UE/Unity-parity, 2026)

Goal: a complete, production-grade in-engine UI toolkit — authored WYSIWYG in the 3D
viewport, rendered identically at runtime, on par with Unity UGUI/UI Toolkit and UE UMG.

Rendering baseline today: `Engine/UI/UICanvasRenderer` draws the canvas tree via an ImGui
draw list (batched, supports textures via `ImTextureID`, clipping). Runtime dispatches
`UIButton.OnClickMessage`. Assets are cooked; `TextureAsset` exists; there is a Sequencer
panel; Lua scripting exists.

Legend: [x] done · [~] partial · [ ] todo · (dep) needs a third-party dependency (must be
approved per the engine's "no magic dependencies" rule).

---

## A. Content types (what can live in a UI element)
- [x] Text (basic, default font)
- [ ] Image / Sprite (PNG/JPG/TGA), sprite atlas, **9-slice**, tiling, tint, flip
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
- [ ] Auto-layout containers: **VBox / HBox / Grid / Flex / Wrap**, padding/spacing/align
- [ ] Content Size Fitter, Aspect Ratio Fitter, Layout Element (min/preferred/flex)
- [ ] Constraints (pin/relative), safe-area (mobile notches), responsive breakpoints
- [ ] Pixel-perfect mode

## C. Controls / widgets
- [x] Panel, Text, Button
- [ ] Image, RawImage (render-texture), Toggle/Checkbox, Radio group
- [ ] Slider, Range, ProgressBar/Spinner, Stepper
- [ ] InputField (single/multi-line, password, IME), Dropdown/ComboBox, ColorPicker
- [ ] ScrollView + ScrollBar, **virtualized ListView / GridView / TreeView / Table** (huge lists)
- [ ] TabView, Accordion, Splitter, Drawer/Sidebar, MenuBar/ContextMenu, Tooltip
- [ ] Modal/Dialog/Window, Toast/Notification, Carousel, Rating, Chips, Breadcrumb, Pagination

## D. Interaction & input
- [x] Mouse click-select (editor), button hover/press (runtime)
- [ ] Full state set: normal / hover / pressed / disabled / selected / focused
- [ ] **Focus + navigation** (keyboard Tab-order, gamepad D-pad, back button)
- [ ] **Touch + gestures** (tap/long-press/swipe/drag/pinch), multi-touch
- [ ] **Drag & drop** between UI elements (inventory), drop targets
- [ ] Event system: per-element callbacks, bubbling/capture, pointer enter/exit/down/up/scroll
- [ ] Raycast/hit blocking, interactable/raycast-target flags

## E. Text & fonts (production)
- [ ] Custom fonts (TTF/OTF asset), font fallback chain, emoji
- [ ] Correct DPI/scale sizing, word-wrap, ellipsis, auto-size, justification, line spacing
- [ ] **Rich text** (color/bold/italic/links/inline sprites), markdown/BBCode
- [ ] Complex scripts: RTL/bidi, CJK, shaping (dep: HarfBuzz) — later
- [ ] Text effects: outline, shadow, gradient, glow

## F. Animation
- [ ] Tween/easing (from->to, curves), triggers (on-show/hover/click)
- [ ] Transitions between screens/states, UI **state machine**
- [ ] **UI Sequencer** (reuse engine Sequencer): tracks on position/color/opacity/scale
- [ ] Spring/physics motion; Lottie playback (dep)

## G. Rendering & effects
- [ ] **Masking / clipping** (rect mask, alpha/sprite mask)
- [ ] Sorting: z-order, sorting layers, multiple canvases (overlay / world-space / camera-space)
- [ ] **World-space canvas** (UI on a 3D surface) + **camera-space**
- [ ] Effects: drop shadow, outline, gradient, **backdrop blur / glass**, glow, blend modes
- [ ] Custom UI shaders/materials; post-processing scoped to UI
- [ ] Batching to a real UI mesh (draw-call reduction) for perf at scale
- [ ] Offscreen culling, element pooling for lists

## H. Data & scripting
- [ ] Data binding (MVVM-lite): bind text/value/visibility/color to game data
- [ ] **Lua hooks**: on-click -> Lua fn, `ui.find()`, set text/color, spawn lists from data
- [ ] Data-driven lists (bind a collection -> virtualized list)

## I. Editor authoring (WYSIWYG, prod UX)
- [x] In-scene render + create menu + click-select + anchor presets + 2D mode
- [ ] **Drag-move + resize handles + anchor handles** (mouse), snapping + alignment guides + distribute
- [ ] Multi-select, copy/paste/duplicate, align/space tools, rulers/guides
- [ ] **UI Prefabs** (reusable widgets) + nested prefabs + variants
- [ ] Widget palette/library; anchor-preset widget in Details (inspector-style)
- [ ] Undo/redo for all UI edits; device-preset preview (phone/tablet/desktop/4K)
- [ ] Styles/Themes as assets (colors/fonts/spacing), theme switching

## J. App/runtime level
- [ ] **Splash screen** (configurable: image/logo, duration, fade) on boot + scene load
- [ ] **Screen/navigation stack** (pages, push/pop, back), scene transitions (fade/slide)
- [ ] Button actions: SendMessage / LoadScene / Quit / OpenURL / Custom(Lua)
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

1. **Interactive editing** — drag-move, resize handles, anchor handles, snapping + guides. (I)
2. **Sprites / images** — Image on Panel/Button/Canvas, 9-slice, atlas, tint. (A/C) *(no dep)*
3. **Button actions + scene switch + splash screen** + audio feedback. (J)
4. **Masking/clipping + effects** (shadow/outline/gradient) + world/camera-space canvas. (G)
5. **Fonts & rich text** (custom TTF, wrap, auto-size, rich text). (E)
6. **Auto-layout containers** (VBox/HBox/Grid + fitters + safe-area). (B)
7. **Controls + input system** (Toggle/Slider/InputField/ScrollView/Dropdown + focus/nav/drag&drop). (C/D)
8. **3D model / render-texture element** + particles-in-UI. (A)
9. **GIF** *(likely no dep)* → then **SVG** (nanosvg) → **Video** (pl_mpeg light, or ffmpeg) — dep sign-off. (A)
10. **UI animation / Sequencer** (tweens, state machine, transitions, Lottie). (F)
11. **Data binding + Lua** hooks; data-driven virtualized lists. (H)
12. **Prefabs / themes / undo-redo / device-preview** (editor polish). (I)
13. **Localization + accessibility**. (K)
14. **Perf pass**: batch to UI mesh, culling, pooling, atlases. (G/L)

Dependencies to approve when reached: nanosvg (SVG), pl_mpeg/libvpx or ffmpeg (video),
rlottie (Lottie), HarfBuzz (complex text). Each: justify size/license/compile-time first.
