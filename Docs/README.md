# Desert Engine — documentation

| Document | What it covers |
| --- | --- |
| [UNITS.md](UNITS.md) | **Read this first.** One world unit is one centimetre, everywhere — and how a metres-era scene migrates. |
| [Scripting.md](Scripting.md) | Lua reference: lifecycle, entity/transform/component API, input, timers, world blackboard, the `ui` data bridge. |
| [UI_ROADMAP.md](UI_ROADMAP.md) | The in-engine UI toolkit: what ships today and what remains, section by section (content, layout, controls, input, text, animation, rendering, data, editor, platform). |
| [EDITOR_DETAILS_ROADMAP.md](EDITOR_DETAILS_ROADMAP.md) | Plan for bringing the Details panel to UE parity: live asset previews and per-component visuals for lights, sky, camera and the rest. |
| [CUBEGRID_TODO.md](CUBEGRID_TODO.md) | UE-style CubeGrid blockout tool: the implemented model (fixed-base voxels, committed layers, Corner Mode) and the remaining UE features. |

Reference screenshots for the modeling tools live in `ModelingImages/`.

## Conventions worth knowing before contributing

* **Units** — centimetres. A new default size is written `Common::Units::Metres( x )` when a metre figure
  reads better; reflected distance fields are marked `PROPERTY( ..., Length )`.
* **Formatting** — the CI gate runs **clang-format 18**, which disagrees with newer versions on several
  constructs. Verify with it directly, not with whatever is on `PATH`:
  `/opt/homebrew/opt/llvm@18/bin/git-clang-format --binary /opt/homebrew/opt/llvm@18/bin/clang-format --diff <base>`
* **Reflection** — `REFLECT()` / `PROPERTY(...)` in a header; `DesertHeaderTool` regenerates
  `Engine/Generated/Reflection.gen.cpp` as a prebuild step. Vector-of-struct fields are not supported by
  that path — those components get a hand-written serializer in `ComponentRegistry`.
* **Editor panels** — a tool panel that only applies to a particular selection or mode declares
  `IsContextual()` + `IsRelevant()`; the editor opens and closes it with its context, unless the user
  pinned it open by hand.
* **Never render offscreen from `OnUIRender()`** — defer GPU work to `OnPreUpdate()`, or descriptor pools
  get destroyed while their sets are bound to the recording command buffer.
