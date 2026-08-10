# Requirements — Sky, Atmosphere, Sun, Migration

**Version 2. The six open questions of v1 have been DECIDED BY THE ARCHITECT and are now requirements, not
recommendations.** Every decision is recorded in §9 with the requirement it produced; §9 is a record, not a
question list. Nothing in this document is awaiting an answer.

Analyst A. Scope: `SkyAtmosphereComponent`, the reduced `SkyboxComponent`, the atmosphere-sun rules on the
directional light, and the scene migration. Volumetric clouds (shape, lighting, noise, cloud presets) are
Analyst B's; where the two meet the interface is specified here and marked **[CONTRACT B]**.

Fixed inputs from the architect (not re-litigated): two separate components; all procedural-sky settings
leave `SkyboxComponent`; UE-style "Atmosphere Sun Light" + index on the directional light; no legacy, an
explicit versioned migration, repository scenes converted in the same change; 1 world unit = 1 cm.

Architect's decisions folded into v2: sun colour/intensity stays split but must be **semantically** split
(SKY-35); `AtmosphereEnv` stays narrow and the sky is shared as a **computation, not a representation**
(SKY-03); the time-of-day model is approved unchanged (SKY-09); presets are one hardcoded table for both
halves with a `Custom` fallback (SKY-12, SKY-37); the autosave scenes are deleted and ignored (SKY-28);
the resolution ladder is approved and `High` must announce its memory cost (SKY-36).

Sources: `Docs/Clouds/RESEARCH_ENGINE.md` (RE), `Docs/Clouds/RESEARCH_REFERENCE.md` (RR),
`Docs/Clouds/DEV_CONTRACT.md` (DC). Every `file:line` below was either cited in RE/RR or re-read in the
working tree on `dev` @ `b02730b`; claims verified by me directly are marked **(verified here)**.

---

## 0. Correction to the research documents — `std::string` IS supported by reflection

**`RESEARCH_ENGINE.md` §2.2 is wrong where it lists `std::string` among the types "not supported by the
reflection path". Reflected `std::string` fields work end to end today.** Verified in the working tree:

* `Engine/Reflection/ReflectionTypes.hpp:15-30` — `FieldType::String` exists in the enumeration.
* `Engine/Generated/Reflection.gen.cpp` — the codegen already emits **17** `FieldType::String` fields,
  among them `UIInputField::Text` / `Placeholder`, `UIDropdown::Options`, `UIBinding::Key` / `Format`,
  `UIScreen::Name`, `AudioComponent::Clip`.
* `Engine/Reflection/ReflectionSerializer.cpp:97` (write) and `:169` (read) — both directions.
* `Editor/.../PropertyEditor/PropertyEditorBuilder.cpp:565-575` — drawn as `ImGui::InputText`; `:205-209`
  — summarised (truncated at 24 chars) in the collapsed-header line.

The research's own citation, `Docs/README.md:20-22`, says only that **vector-of-struct** fields are
unsupported — it says nothing about strings. The claim was an over-reading of that line, and it reached v1
of this document unverified.

**Consequence for anyone reading RE §2.2 — including Analyst B:** do not decline a string field on the
belief that reflection cannot carry it. The genuine restriction is **vector-of-struct** (and those
components get a hand-written serializer in `ComponentRegistry`). RE itself has not been corrected — this
document may not modify it — so the erroneous line is still there for the next reader; it should be fixed
at the source.

**Audit of this document against the false premise.** Every non-string choice here was re-checked against
the corrected fact, not against the earlier belief. There are exactly two enums, and neither was chosen
because strings were thought impossible: `SkyPreset` (SKY-37 — validity, build-time failure, the right
widget, no allocation) and `SkyEnvironmentResolution` (SKY-22 — so the bake dispatch can never be handed a
size that is not a multiple of the 32×32 work group, `ComputeImages.cpp:83-84`). No other field in the
component is a name, label or identifier, so no other requirement changes. The one occurrence of the false
claim was in SKY-37's rationale and is corrected there.

---

## 1. Goals and non-goals

### Goals

1. The sky becomes an authorable, first-class scene feature with **one** owner: an entity component named
   *Sky Atmosphere*, findable in Add Component, removable, undoable, duplicable, prefab-safe.
2. The sun stops being "whatever directional light `entt` happened to visit first"
   (`SkyboxECSSystem.hpp:34-42`, `Scene.cpp:343-344` — two different iteration orders, RE §7.2) and becomes
   a **designated** light, exactly as in Unreal.
3. `SkyboxComponent` becomes honest: an HDRI/cubemap backdrop and nothing else.
4. Every existing `.desce` in the repository keeps its sky across the move, by a **tested** migration —
   not by luck. Today the move would silently blank every sky (RE §6.4, `EntitySerializer.cpp:91-96`).
5. The sky publishes a small, stable per-frame state (`AtmosphereEnv`) that the volumetric cloud pass
   consumes, so cloud lighting and sky lighting can never disagree. **[CONTRACT B]**
6. The IBL bake stops being permanently stale: moving the sun currently never rebakes
   (`SkyboxRenderer.hpp:41-44`), and `m_BakedSunDir` is written and never read
   (`SkyboxRenderer.cpp:109`, `.hpp:90` — **verified here**, a dead member).
7. Optional, opt-in day/night driving of the sun, as a pure function that can be unit-tested with no GPU.

### Non-goals (explicit, so they are not discovered in week 3)

* **No physically-based scattering model in v1** — see SKY-01. No Preetham, no Rayleigh/Mie coefficients,
  no turbidity. *Architect's note:* if Analyst B establishes that scattering is required for credible cloud
  **ambient** lighting, the question returns as a separate requirement with its own acceptance — it does
  **not** reopen or rewrite this half. Nothing is to be built in anticipation of it.
* **No second directional light.** The engine supports exactly one (`Scene.cpp:356-373`); widening it is a
  separate project (RE §7.3).
* **No god rays / light shafts.** RR §G.3 documents the reference's version as 100 full-res taps per pixel
  with no downsample; it is a post-process feature, not a sky feature.
* **No *numeric* unification of `DirectionalLightData::Color/Intensity` with the sky's
  `SunColor/SunIntensity`** in v1. The two are separated **by meaning** instead (SKY-35), which is a
  requirement of this version, not a deferral. Full unification — one number driving both the disk and the
  shading — is **v2 work, and it carries the re-authoring of all four sky-bearing scenes**, because every
  one of them was lit under the old split.
* **No preset asset format.** Presets are a hardcoded table (SKY-12). A serialized preset would be a new
  asset type, and that means a new `AssetService`, a new branch in `MakeAssetResolver`
  (`ComponentRegistry.cpp:189+`), a thumbnail path and a migration of its own — an entire subsystem to
  avoid recompiling for a sixth preset. Explicitly out of scope for v1, for both halves.
* **No aerial perspective / height fog.** Not present today, not required by the cloud pass.
* **No automatic flipping of user scenes' sun direction** — see SKY-21.
* Cloud shape, cloud lighting, cloud presets, noise volumes, the cloud render pass — Analyst B.

---

## 2. Sky model

**SKY-01 — The sky model remains the existing artistic gradient in `Editor/Resources/Shaders/Common/Atmosphere.glslh`. No analytic scattering model is introduced in this programme.**

*Why:* the gradient already supplies all four quantities the cloud pass needs (sun direction, sun radiance,
ambient sky radiance above/below, night factor); switching model changes their *values*, not their
availability, so it does not unblock B — while it deletes the artistic palette that two shipped editor
paths depend on (`PreviewViewport.cpp:151-160` authors a deliberately dark neutral studio dome;
`AssetThumbnailRenderer.cpp:72-85` authors a blue-tinted *ground* hemisphere because that camera looks
down), and RR §H.7 shows a Preetham port carries **28 magic constants** (`#84`–`#111`) that cannot be tuned
without looking at the result — which DC §2.3 says we cannot do here.

*Accept:* `git grep -n "rayleigh\|turbidity\|mieCoefficient" Editor/Resources/Shaders Desert Editor/Source`
returns no new hits; `Atmosphere.glslh`'s `EvaluateSky` signature is unchanged except for the
sun-angular-diameter unit change of SKY-08.

**SKY-02 [CONTRACT B] — `Atmosphere.glslh` stays the single sky evaluation entry point, shared by the screen pass, the IBL bake and the cloud pass. No second copy of the sky maths may exist in GLSL or C++. When the cloud pass needs sky radiance in any direction — for ambient, for compositing, for anything — it `#include`s `Common/Atmosphere.glslh` and calls `EvaluateSky`. It is never handed the palette layout in C++.**

**We share the computation, not the representation.** This is the architect's ruling and the reason for it
is load-bearing: if B received a `SkyConfig` struct, every future change to the sky palette — a field added,
reordered, or given a different meaning — would break the cloud shader's binary layout, and the two halves
would be permanently coupled at their most volatile surface. Sharing the *function* means the sky can be
re-authored, re-tuned, or (per the note in §1) eventually replaced by a scattering model without B changing
a line. It is also the only arrangement in which SKY-01's model choice is genuinely reversible.

*Why:* the sky and the light it casts are already required to be identical (`Atmosphere.glslh:1-5`); adding a
cloud pass that re-derives sky colour is how the two drift apart.

*Accept:* `grep -rl "EvaluateSky" Editor/Resources/Shaders` lists exactly the sky shader, the bake compute
shader, and (once B lands) the cloud shader — and `Atmosphere.glslh` itself. No `SkyConfig` literal is
constructed anywhere but from the shared packing helper of SKY-16. `grep -n "SkyConfig\|SkySettings" <B's
C++ files>` is **empty** — a positive hit is a contract violation, not a style note.

**SKY-03 [CONTRACT B] — The engine publishes a per-frame evaluated atmosphere state, `Graphic::AtmosphereEnv`, retrieved as `SceneRenderer::GetAtmosphere()`, mirroring `WindEnv` / `GetWind()` (`Graphic/WindEnv.hpp`, `SceneRenderer.hpp:143-146`).**

Contents, fixed by this document (owner: A; consumer: B):

| Member | Type | Meaning |
|---|---|---|
| `SunDirection` | `glm::vec3` | normalized, **TOWARD the sun** — the engine's one negation (RE §7.2) |
| `SunIrradiance` | `glm::vec3` | linear RGB = `SunColor * SunIntensity` |
| `ZenithRadiance` | `glm::vec3` | linear RGB ambient from above (the day/night-blended zenith) |
| `GroundRadiance` | `glm::vec3` | linear RGB ambient from below |
| `SunAngularRadius` | `float` | radians — `radians(SunAngularDiameter) * 0.5` |
| `NightFactor` | `float` | `1 - smoothstep(-0.10, 0.20, SunDirection.y)`, matching `Atmosphere.glslh:61-62` |
| `Valid` | `bool` | `false` when there is no enabled sky component or no atmosphere sun |
| `ParamsBuffer` | `ShaderResources::StorageBuffer*` | **opaque handle** to the packed sky-parameter SSBO (SKY-39). B binds it; B never reads its layout in C++ (SKY-40) |

**The struct is closed.** Members are added only by a change to this document. In particular it does **not**
carry a `SkyConfig`, a `SkySettings`, or any palette — that is SKY-02's ruling, and `AtmosphereEnv` is the
C++ half of the same principle: it carries **evaluated quantities**, never the authoring representation
they came from. `ParamsBuffer` is consistent with that: a handle is not a layout. B can bind it and, in
GLSL, unpack it through the shared loader (SKY-38); B cannot see a field of it from C++.

*Why:* B needs exactly this and nothing more; giving B the component would couple the cloud pass to the sky's
authoring model. `WindEnv` is the precedent the engine already established for "evaluated per-frame
environment" (`WindEnv.hpp:8-10` names clouds as its future consumer).

*Accept:* a unit test constructs a `SkyAtmosphereData` + a sun direction and asserts every member of the
returned `AtmosphereEnv`, including `NightFactor` at sun elevations `sin(y) = -0.2, -0.10, 0.05, 0.20, 0.9`
against values computed by hand in the test; `GetAtmosphere().Valid == false` for an empty scene;
`ParamsBuffer` is `nullptr` exactly when `Valid` is `false`.

### 2.1 Making "share the computation" actually reachable from a compute pass

The three requirements below exist because the architect's ruling (SKY-02) has a hard consequence: the
cloud pass evaluates the sky **from a compute shader**, and `ComputePipeline` has **no `SetUniformBuffer`**
— its whole binding surface is `SetInput` / `SetOutput` / `SetStorageBuffer` / `SetPushConstants`
(**verified here**, `Graphic/Pipeline.hpp:163-173`). A uniform block is therefore unreachable from compute,
and without these three changes the contract could be stated but not implemented.

**SKY-38 [CONTRACT B] — `Atmosphere.glslh` gains a stage-agnostic packed-parameter loader, so any pass can fill a `SkyConfig` without re-deriving the layout.**

Added to the include, next to `SkyConfig` / `DefaultSkyConfig()`:

```glsl
#define SKY_PACKED_VEC4_COUNT 9
struct SkyPacked { vec4 v[SKY_PACKED_VEC4_COUNT]; };

SkyConfig UnpackSkyConfig      ( SkyPacked s );  // palette + scalars
vec3      UnpackSunDirection   ( SkyPacked s );  // TOWARD the sun, normalized
float     UnpackSunIntensity   ( SkyPacked s );
float     UnpackSunAngularRadius( SkyPacked s ); // RADIANS (SKY-08 converts on the CPU side)
```

Each consuming pass declares its own buffer at its own binding, copies it into a local `SkyPacked`, and
calls the loaders. The **layout lives in exactly one place** — the include — while the **binding stays each
pass's business, which is what lets the sky pass and the cloud pass have different descriptor sets.

*Why:* this is the mechanism that makes SKY-02 true rather than aspirational. Handing B a GLSL `SkyConfig`
would have been the representation; handing B an unpack function is the computation. It also means adding a
palette field is one edit to the include plus one to the C++ mirror, and B recompiles unchanged.

*Accept:* `DShaderTool Editor/Resources/Shaders` parses; the sky fragment shader and
`BakeProceduralSky.shader` both build their `SkyConfig` **only** via `UnpackSkyConfig` — the hand-written
20-line assignment blocks at `ProceduralSky.shader:41-52` and `BakeProceduralSky.shader:47-58` are deleted,
not left beside it; a C++ `static_assert` ties `SKY_PACKED_VEC4_COUNT` to the payload struct's size.

**SKY-39 — The sky parameter block is a std430 SSBO, non-persistent, created explicitly — not a uniform block, not push constants, and not the reflection-created SSBO.**

| Option | Verdict | Reason |
|---|---|---|
| Uniform block (today's `SkyUB`) | **rejected** | unreachable from `ComputePipeline` (no `SetUniformBuffer`) |
| Push constants | **rejected** | the payload is 9 × `vec4` = **144 bytes**, over the 128-byte guaranteed minimum; both existing users in the engine sit exactly at 128 (`ComputeImages.cpp:56-57`, `ParticleRenderer.hpp:49-60`) |
| Reflection-created SSBO | **rejected** | that path hardcodes a 36-byte size (`ShaderResources/StorageBuffer.hpp:20-22`) |
| `ShaderResources::StorageBuffer::Create( name, size, binding, /*persistent=*/false )` | **required** | public API for exactly this (`StorageBuffer.hpp:29-30`) |

`persistent = false` is load-bearing and satisfies the per-frame-state rule with no extra work: the Vulkan
backend allocates `framesInFlight * slots` copies and resolves the recording slot on every write
(**verified here**, `ShaderResources/API/Vulkan/VulkanStorageBuffer.cpp:68`, `:123`, `:130` —
`m_Persistent ? 0u : CopyIndex()`). A `persistent = true` buffer would be shared across renderer slots by
design and the mesh preview would overwrite the viewport's sky (`Docs/RENDERER_FRAME_STATE.md:73-76`).

**The graphics side keeps working**, via the precedent the engine already set for "a correctly-sized,
externally-owned SSBO bound to a material": `StorageBufferProperty::SetBuffer`
(`Materials/Properties/StorageBufferProperty.hpp:36-41`, whose comment names exactly this use), driven like
`MaterialParticleBillboard::Update` (`:37-38`). Byte-wise nothing moves: for an array of `vec4`, std140 and
std430 agree on a 16-byte stride, so the payload the sky pass uploads is unchanged — only the descriptor
type changes.

⚠️ **Binding trap, to be written into the code as a comment.** The graphics descriptor write uses the
buffer's **own** binding, while `ComputePipeline::SetStorageBuffer( binding, … )` uses the **explicit
argument**. `ParticleRenderer.cpp:114-118` records what happens when they disagree: the buffer aliased the
camera UB at binding 0 and tripped `VUID-VkWriteDescriptorSet-descriptorType-00319`. The sky buffer is
therefore created with the same binding number the sky shader declares, and the cloud pass passes its own
binding explicitly.

*Accept:* the buffer is created through the public `Create(...)` with `persistent = false` and a size equal
to `SKY_PACKED_VEC4_COUNT * sizeof(glm::vec4)` (asserted); `DShaderTool` parses both shaders; the sky
renders through `StorageBufferProperty`, and a unit test asserts the payload size against
`SKY_PACKED_VEC4_COUNT`. Bindings are written with explicit `(n)` in the DSL, never the auto-allocated
paren-less form, because they are bound by a fixed number from C++ (`DShaderParser.cpp:822-826`).

**SKY-40 [CONTRACT B] — `AtmosphereEnv::ParamsBuffer` is an opaque handle. B binds it; B never reads a field of it in C++.**

*Why:* the architect's narrowness ruling was about not exporting the sky's **palette representation**, and a
handle exports nothing — it is the C++ equivalent of the GLSL unpack function. The sky may re-pack, reorder
or extend the block at will; B's C++ is unaffected and B's GLSL follows through SKY-38's loaders.

*Accept:* `git grep -n "ParamsBuffer" <B's files>` shows only `SetStorageBuffer( n, env.ParamsBuffer )`;
AC-29's "no `SkyConfig`/`SkySettings` in B's C++" still holds unchanged.

---

## 3. Component specification — `SkyAtmosphereComponent`

**SKY-04 — The component uses the `Data`-sub-block shape: `struct SkyAtmosphereData { REFLECT() … };` and `struct SkyAtmosphereComponent { SkyAtmosphereData Data; bool RequestBake = false; };`, serialized under key `"SkyAtmosphere"`, reflected type name `"SkyAtmosphereData"`, display name `"Sky Atmosphere"`.**

*Why:* RE §2.1 — the flat shape is exactly why `SkyboxComponent` is **not** Lua-bindable
(`ReflectionBindings.cpp:36-52` hard-assumes `.Data`) and why it needs a custom serializer entry. The
`Data` shape gives Lua, the one-line editor registration and the one-line serializer registration for free.

*Accept:* `ComponentRegistry.cpp` gains exactly one line using `MakeReflected<…>`; `kReflectedComponents[]`
in `Scripting/ReflectionBindings.cpp:55-64` gains one entry; the component appears in
`Generated/Reflection.gen.cpp` after a `Desert` build and the diff is committed.

### 3.1 Field table

23 reflected fields + 1 transient. Columns: **Src** = `P` preset-driven / `A` per-scene authored /
`Q` quality-or-performance knob (**never** preset-driven, DC §1.3 read together with RR §H.12's warning
that quality belongs in a separate tier) / `D` derived (no field). Colours are **linear** RGB.

| # | Field | C++ type | Unit | Range | Default | Controls | Replaces | Src |
|---|---|---|---|---|---|---|---|---|
| 1 | `Enabled` | `bool` | – | – | `true` | Sky pass renders the atmosphere and it feeds IBL | `SkyboxComponent::Procedural` (`Components.hpp:1458-1459`) | A |
| 2 | `SkyBrightness` | `float` | ×  | 0 – 4 | `1.0` | overall sky radiance multiplier (`Atmosphere.glslh:81`) | `SkyboxComponent::SkyBrightness` (`:1475-1476`) | P |
| 3 | `HorizonFalloff` | `float` | – | 0.1 – 2 | `0.85` | how far up the horizon colour reaches (`Atmosphere.glslh:70`) | `:1477-1478` | P |
| 4 | `ZenithColor` | `glm::vec3` | linear RGB | 0 – 1 | `{0.08,0.26,0.70}` | sky straight up, day | `:1467-1468` | P |
| 5 | `HorizonColor` | `glm::vec3` | linear RGB | 0 – 1 | `{0.50,0.66,0.92}` | sky at the horizon, day | `:1469-1470` | P |
| 6 | `GroundColor` | `glm::vec3` | linear RGB | 0 – 1 | `{0.16,0.19,0.24}` | hazy tone below the horizon (`Atmosphere.glslh:111-113`) | `:1471-1472` | P |
| 7 | `NightColor` | `glm::vec3` | linear RGB | 0 – 1 | `{0.010,0.020,0.050}` | night zenith tint | `:1473-1474` | P |
| 8 | `SunIntensity` | `float` | × | 1 – 50 | `22.0` | sun disk + IBL sun radiance scale | `:1460-1461` | P |
| 9 | `SunColor` | `glm::vec3` | linear RGB | 0 – 1 | `{1.00,0.96,0.88}` | sun disk/glow tint at noon | `:1480-1481` | P |
| 10 | `SunAngularDiameter` | `float` | **degrees** | 0.1 – 10 | `2.2918` | apparent size of the sun disk | `SkyboxComponent::SunDiskRadius` (radians, **radius**) `:1462-1463` | P |
| 11 | `SunGlow` | `float` | × | 0 – 5 | `1.0` | broad halo strength around the sun | `:1482-1483` | P |
| 12 | `SunsetColor` | `glm::vec3` | linear RGB | 0 – 1 | `{1.00,0.42,0.18}` | warm horizon reddening near the sun | `:1484-1485` | P |
| 13 | `SunsetIntensity` | `float` | × | 0 – 3 | `1.0` | strength of that reddening | `:1486-1487` | P |
| 14 | `StarIntensity` | `float` | × | 0 – 5 | `1.0` | night star brightness (`Atmosphere.glslh:96-109`) | `:1488-1489` | P |
| 15 | `DriveSunFromTimeOfDay` | `bool` | – | – | `false` | when true, the sky drives the atmosphere sun's transform | *(new)* | A |
| 16 | `TimeOfDay` | `float` | hours | 0 – 24 | `12.0` | solar time; sun elevation/azimuth derive from it | *(new)* | A |
| 17 | `DayLengthSeconds` | `float` | seconds | 0 – 86400 | `600.0` | real seconds per in-game day; **0 = frozen at `TimeOfDay`** | *(new; RR §H.10 #132 uses 30 s — a demo value we do not copy)* | A |
| 18 | `Latitude` | `float` | degrees | −90 – 90 | `45.0` | tilt of the solar path (how high noon gets) | *(new)* | A |
| 19 | `NorthOffset` | `float` | degrees | 0 – 360 | `0.0` | world azimuth of solar north | *(new; RR §G.1 records the reference's `phi = 45` radians-for-degrees bug — we take degrees and convert explicitly)* | A |
| 20 | `AutoRebakeEnvironment` | `bool` | – | – | `true` | rebake the sky IBL when the sun/sky changed enough | *(new)* | **Q** |
| 21 | `RebakeSunAngleThreshold` | `float` | degrees | 0.25 – 45 | `5.0` | how far the sun must move before an automatic rebake | *(new)* | **Q** |
| 22 | `EnvironmentResolution` | `enum class SkyEnvironmentResolution : uint8_t { Low, Medium, High }` | – | – | `Medium` | equirect bake size: 512×256 / **1024×512 (today's fixed value, `SceneEnvironment.cpp:55-56`)** / 2048×1024 | *(new; replaces the hardcoded constant)* | **Q** |
| 23 | `ActivePreset` | `enum class SkyPreset : uint8_t { Custom, ClearNoon, GoldenHour, OvercastGrey, Night, StudioNeutral }` | – | – | `Custom` | **display only** — which preset the palette last came from; reverts to `Custom` on the first edit of any `P` field (SKY-37) | *(new)* | A |
| — | `RequestBake` | `bool` | – | – | `false` | one-shot bake request from the editor button; **no `PROPERTY`** ⇒ transient | `SkyboxComponent::RequestBake` (`:1505-1506`) | A |

`ActivePreset` is an **enum, not a string** — and not because strings are unsupported (they are fully
supported; see the correction in §0). The reasons are:

1. **A string can hold a preset that does not exist.** `"Clea Noon"` from a typo, a stale name after a
   preset is renamed, or arbitrary text from a hand-edited `.desce` — and there is no validation layer
   anywhere between the file and the widget that would catch it. An enumerator can only ever be one of the
   declared presets.
2. **Removing or renaming a preset becomes a build error instead of a load-time surprise.** SKY-12's
   "one table row per enumerator" assertion fires at test time; a dangling string label would survive
   silently in every scene that used it.
3. **The editor draws the right control for free.** A reflected enum becomes a combo box; a reflected
   `std::string` becomes a free-text `ImGui::InputText` (**verified here**, `PropertyEditorBuilder.cpp:565-575`)
   — exactly the wrong affordance for "which of five presets", since the user could type anything into it.
4. **One byte, comparable with `==`, no allocation.** The component's `Data` is copied wholesale by the
   duplicate / undo / prefab paths (RE §2.7), and it is compared in SKY-37's revert logic.

Reflected enums round-trip end to end (`ReflectionSerializer.cpp:109`, `:182`; `Reflection.gen.cpp` emits
`EnumValues` — **verified here** against `SceneSettings::RenderingPath`).

**Derived, never authored (D):** sun direction (from the atmosphere sun light, SKY-17), `NightFactor`,
sun elevation and azimuth *when* `DriveSunFromTimeOfDay` is false, `SunAngularRadius` (radians, SKY-03).

**SKY-05 — Fields 2–9 and 11–14 keep their C++ member names byte-identical to the `SkyboxComponent` fields they replace.**

*Why:* RE §6.4 "the rename trap" — there is **no** field-alias mechanism in the reflection serializer, and
the project's own precedent (`ComponentRegistry.cpp:1010-1015`) is to accept the data loss. Keeping names
identical makes the migration a *move*, and every mapping in SKY-23 a straight copy that cannot mistype.

*Accept:* the migration test T1 asserts all 12 same-named values carried with no per-field conversion code.

**SKY-06 — Every field in the table is wired end to end: component → serializer → Details UI → the render command → the GPU payload (screen pass **and** IBL bake) → a visible term in `Atmosphere.glslh` or in the driving system.**

*Why:* DC §1.3 — a slider that moves nothing is a TODO wearing a costume; DC §3.4 says the reviewer picks
three parameters at random and traces them.

*Accept:* a per-field trace table is included in the developer report, and the test of SKY-13 asserts that
every reflected field of `SkyAtmosphereData` is read by at least one of: `MakeSkySettings` (SKY-16),
`ComputeImages::BakeProceduralPanorama`'s push block, `AtmosphereEnv`, or the time-of-day driver — by
enumerating `ReflectionRegistry::Get().Find("SkyAtmosphereData")->Fields` and comparing against an
explicit expected list, so adding a field without wiring it fails the test.

**SKY-07 — Fields 20, 21 and 22 are quality/performance knobs and must not appear in any preset table. Fields 15–19 and 23 are authored state and are likewise untouched by a preset.**

`ActivePreset` (23) is deliberately excluded from what `Apply` writes: the pure function changes the palette
only, and the **caller** (the editor widget) records which preset it applied. That keeps `Apply`'s contract
"palette in, palette out" and keeps the SKY-07 test meaningful — a function that stamped its own name into
the data could never be asserted against "the `A` fields are unchanged".

*Why:* RR §H.12 makes this the explicit lesson from the reference: mixing look presets with quality tiers
means "Storm" silently halves your frame rate.

*Accept:* the preset-apply function of SKY-12 takes a `SkyAtmosphereData&` and a test asserts that after
applying every preset, fields 15–23 are bit-identical to their pre-apply values.

**SKY-08 — `SunDiskRadius` (radians, radius) is replaced by `SunAngularDiameter` (degrees, diameter). `Atmosphere.glslh` and `BakeProceduralSky.shader` take the angular **radius in radians**, converted once on the C++ side.**

*Why:* a bare `0.02` in a component annotated with no unit is exactly the class of bug `Docs/UNITS.md` was
written for; degrees is what an artist reads (the real sun is 0.53°, RR §H.7 `#96` uses 0.032°, and our
default of 2.29° is ~4× the real sun — a fact worth being able to *see* in the UI).

*Accept:* the field carries `Units("deg")`; the migration test asserts `degrees(0.02 rad) * 2 = 2.2918°`
within `1e-4`; the shader's parameter is still radians and `EvaluateSky`'s comment says so.

**SKY-09 — Time-of-day driving is a pure function `Graphic::SunDirectionFromTimeOfDay( float hours, float latitudeDeg, float northOffsetDeg ) -> glm::vec3` returning the **travel** direction (sun → scene), applied to the atmosphere sun's `TransformComponent::Translation` by a dedicated system, and only while `DriveSunFromTimeOfDay` is true.**

Rules: the magnitude of `Translation` is preserved (the editor's own rationale,
`ComponentEditorRegistrations.cpp:400-404`: some scenes author it as a sun *position*). When
`DayLengthSeconds > 0` the system advances `TimeOfDay` by `dt * 24 / DayLengthSeconds`, wrapping at 24.
When it is 0 the sun is frozen at the authored `TimeOfDay`.

*Why:* this is the one part of "day–night" (RR §G.1) that is verifiable here — it is arithmetic. Writing the
light's transform keeps the engine's stated single source of truth for the sun
(`SceneSettings.hpp:209-211`) intact instead of introducing a second one.

*Accept:* unit tests: `hours = 6` and `18` give elevation ≈ 0 (|`dir.y`| < 1e-3) at `latitude = 0`;
`hours = 12, latitude = 0, northOffset = 0` gives the sun directly overhead (travel `dir.y` ≈ −1);
`hours = 0` gives `dir.y` ≈ +1 (sun below the horizon); `northOffset = 90` rotates the noon azimuth by 90°
about world Y; the function is `constexpr`-free, allocation-free and touches no globals.

**SKY-10 — The time-of-day driver lives in its own header-only system `ECS::TimeOfDayECSSystem` with `CanRunParallel() → false`, registered **before** the collectors.**

*Why:* it writes `TransformComponent`, which the light collector and the shadow path read; a `true` here is
a data race (RE §4.8, `SKILL.md:234-236`).

*Accept:* the override returns `false` and a comment says why; registration order in
`EditorLayer.cpp:615-633` and `RuntimeLayer.cpp:81-94` places it ahead of `SkyAtmosphereECSSystem`.

**SKY-11 — The automatic IBL rebake decision is a pure function `Graphic::ShouldRebakeSkyEnvironment( const glm::vec3& bakedSunDir, const glm::vec3& currentSunDir, float thresholdDeg, bool autoRebake, bool hasEnvironment, bool explicitRequest ) -> bool`, and `SkyboxRenderer` uses it in place of the current "first enable or button only" rule (`SkyboxRenderer.hpp:41-44`).**

*Why:* with day/night driving the sun moves every frame and the IBL would stay frozen at dawn forever;
`m_BakedSunDir` was already added for this and is dead (**verified here**). Baking is `WaitDeviceIdle`
(`SkyboxRenderer.cpp:87`), so it must be throttled by an explicit, testable threshold, not by a frame count.

*Accept:* unit tests: `explicitRequest` always true; `!hasEnvironment && enabled` true; a 4.9° move at a 5°
threshold false and a 5.1° move true; `autoRebake == false` suppresses everything except `explicitRequest`;
antipodal directions do not produce NaN from `acos`.

**SKY-36 — Selecting `EnvironmentResolution::High` makes the bake log, ONCE per `SkyboxRenderer`, the actual bytes it allocated and the fact that the cost is paid per live `SceneRenderer`.**

Required form (numbers computed, not quoted from this document):
`[SkyAtmosphere] Environment bake at High (2048x1024): panorama {X} MiB + radiance/irradiance/prefiltered cubes {Y} MiB = {Z} MiB — paid PER LIVE SceneRenderer ({N} live now).`

The size derives from `Image::CalculateImageSize` / `GetBytesPerPixel` (`Graphic/Image.cpp:91-110`) —
`RGBA32F` is 16 B/px, so the 2048×1024 panorama alone is 32 MiB before the cube and its mips. The live
renderer count comes from the slot lease already maintained for the frame-state rule
(`Docs/RENDERER_FRAME_STATE.md:78-82`, `SceneRenderer.cpp:213-247`).

*Why:* the architect's instruction, and it is well-founded: the editor can hold several live renderers at
once (viewport, mesh preview, thumbnail renderer, extra scene views — RE §3.7.3, and the multi-scene
feature makes N unbounded in principle), so this is a cost that multiplies silently. A number in the log at
the moment of choosing beats a number found in a profiler a month later. Logging **once per renderer**, not
per bake, keeps a day/night cycle from flooding the log.

*Accept:* the message is emitted from the bake path guarded by a `bool m_HighResCostLogged`; a unit test
over the pure size helper asserts 32 MiB for 2048×1024 RGBA32F and 8 MiB for 1024×512; the log line is
reproduced verbatim in the developer report.

**SKY-12 — Sky presets are ONE hardcoded table in ONE translation unit: `constexpr SkyPresetEntry kSkyPresets[]` where `struct SkyPresetEntry { SkyPreset Id; const char* Name; void (*Apply)( SkyAtmosphereData& ); }`. Adding a preset is exactly one array entry plus one enumerator — no edit anywhere else. The table covers `ClearNoon`, `GoldenHour`, `OvercastGrey`, `Night`, `StudioNeutral`; `Custom` has no entry and no `Apply`.**

*Architect's ruling, taken once for both halves of the programme:* hardcoded table, one TU, one entry per
preset. The alternative — a preset asset format — is an explicit non-goal (§1) with its cost stated there.
Analyst B applies the same shape to cloud presets; neither half invents a second mechanism.

The design must satisfy the litmus test that `SKILL.md:202-218` actually asks — *"can the next variant be
added without touching five places?"* — even though it cannot satisfy *"without recompiling"*. The
enumerator-plus-row shape is what makes that true; a `switch` over presets scattered across the widget, the
serializer and the renderer is what would make it false.

*Why:* precedent is `ParticleEditorPanel.cpp:27-31`, `:124-127`. `StudioNeutral` is required because
`PreviewViewport.cpp:151-160` currently hand-authors that palette in C++ and
`AssetThumbnailRenderer.cpp:73-84` hand-authors another one **twice** (SKY-16) — a preset lets those call
sites reference one set of numbers instead of three copies (DC §2.1, one source of truth).

*Accept:* a unit test applies every preset to a default-constructed `SkyAtmosphereData` and asserts (a) the
`A` and `Q` fields are unchanged (SKY-07), (b) every preset produces a distinct `ZenithColor`, (c) applying
the same preset twice is idempotent, (d) `kSkyPresets` has one entry per enumerator except `Custom`, checked
by a count assertion so a new enumerator without a row fails the test.

**SKY-37 — `ActivePreset` is display-only state, and it reverts to `Custom` on the first edit of any preset-driven field.**

Mechanism: a pure `uint64_t PresetFingerprint( const SkyAtmosphereData& )` hashing exactly the `P` fields
(2–14). The widget takes the fingerprint before drawing the reflected block and after; if
`PropertyEditorBuilder::Draw` reported a change (it returns `bool` — **verified here**,
`PropertyEditorBuilder.hpp:35`, `:40`) **and** the fingerprint differs, `ActivePreset = SkyPreset::Custom`.
Applying a preset sets `ActivePreset` to that preset (the caller does it, SKY-07).

*Why:* a preset name that keeps claiming "Clear Noon" after the artist has dragged the zenith to purple is a
lie the scene file then carries forever. Making the revert *fingerprint-driven* rather than
"any-widget-touched" means editing a quality knob or the time of day does not falsely clear it.

*Accept:* unit tests: the fingerprint is stable across a no-op round trip; it changes for a change in each
of the 13 `P` fields (a loop over them, so a field added to the `P` set without being hashed fails);
it does **not** change for edits to fields 15–23. A widget-level trace in the developer report shows the
before/after call sites.

**SKY-13 — The CPU→GPU payload for the sky is a single struct mirrored by the shader block, guarded by `static_assert` on `sizeof` and `offsetof` for every member.**

*Why:* `MaterialProceduralSky.hpp:43-56` is a hand-maintained mirror `memcpy`'d raw (`:70`) — a field added
on one side silently corrupts everything after it (RE §1.2). RE §4.6 names this as worth a test.

The block loses the four cloud-carrying members (`SkyParams.y/z/w`, `CloudParams`, `WindDir`;
`ProceduralSky.shader:21-32`, `MaterialProceduralSky.hpp:46`, `:60`, `:68`) — the wind injection at
`SceneRenderer.cpp:913-914` moves to B's path **[CONTRACT B]** — leaving **9 `vec4` = 144 bytes**, which is
the number SKY-39 rests on. The payload struct is the C++ mirror of SKY-38's `SkyPacked`, so there is one
layout serving three consumers: the sky graphics pass, the IBL bake compute, and the cloud compute pass.
The exact layout is written in the header **before** the implementation (DC §2.1, interface first) and the
shader comment quotes the same byte offsets.

⚠️ The IBL bake currently receives these parameters as a **128-byte push-constant block**
(`ComputeImages.cpp:56-76`, `BakeProceduralSky.shader:18-28`). At 9 `vec4` it no longer fits, so the bake
moves to the same SSBO — which is a simplification, not extra work: one buffer, one layout, one unpack, and
the bake stops maintaining a second hand-packed mirror.

*Accept:* the test target asserts `sizeof` of the payload struct and the `offsetof` of every member against
the byte offsets documented in the header, so inserting a member fails the test rather than corrupting the
frame; a `static_assert` ties it to `SKY_PACKED_VEC4_COUNT`; the previously separate push block in
`ComputeImages.cpp` is **deleted**, not left in place.

---

## 4. `SkyboxComponent` after the change

**SKY-14 — `SkyboxComponent` retains exactly two reflected fields and nothing else.**

| Field | Verdict |
|---|---|
| `SkyboxHandle` (`Components.hpp:1451-1452`) | **stays** — `Asset<SkyboxAsset>`, `Hidden`, Category `Skybox` |
| `Intensity` (`:1454-1455`) | **stays** — `Range(0,10)` |
| `Procedural` (`:1458-1459`) | **deleted** → `SkyAtmosphereData::Enabled` |
| `SunIntensity`, `SunDiskRadius` (`:1460-1463`) | **deleted** → fields 8, 10 |
| `ZenithColor` … `StarIntensity` (`:1467-1489`, 11 fields) | **deleted** → fields 2–7, 9, 11–14 |
| `EnableClouds` … `CloudWindSpeed` (`:1492-1503`, 6 fields) | **deleted** → Analyst B's component **[CONTRACT B]** |
| `RequestBake` (`:1505-1506`) | **deleted** → `SkyAtmosphereComponent::RequestBake` |

*Why:* DC §4.1 — the old path is deleted by the same change that replaces it; a `Hidden`/deprecated field
kept "for one release" is the two-path outcome the contract forbids.

*Accept:* `git grep -n "Procedural\|ZenithColor\|SunDiskRadius\|EnableClouds" Desert Editor/Source` returns
**zero** hits against `SkyboxComponent`; `Reflection.gen.cpp`'s `SkyboxComponent` block lists two fields.

**SKY-15 — A scene that used the procedural mode gets a `SkyAtmosphereComponent` on the same entity with `Enabled = true` (SKY-23). The Sky pass mode is decided by: if an enabled `SkyAtmosphereComponent` exists → atmosphere; else if a `SkyboxComponent` with a resolvable `SkyboxHandle` exists → HDR cubemap; else → the Sky pass draws nothing and the previous frame's environment is retained.**

*Why:* the old `Procedural` bool did double duty as "which pass" and "is the sky on"; splitting the
components splits that decision, and it must be stated or the two pipelines
(`SkyboxRenderer.cpp:26-56`, chosen at `:128-142`) will both think they own the frame.

*Accept:* a pure function `Graphic::ResolveSkyMode( bool hasEnabledAtmosphere, bool hasHdrSkybox ) -> SkyMode`
in a header, unit-tested over all four input combinations; `SkyboxRenderer::Render()` calls it.

**SKY-16 — One packing helper, `Graphic::SkySettings MakeSkySettings( const ECS::SkyAtmosphereData& )`, is the only place that converts component fields into the transport struct, and all three `SetProceduralSky` call sites use it.**

The three sites (RE §1.2): `ProceduralSkyCommand::Execute` (`ProceduralSkyCommand.hpp:32`),
`PreviewViewport.cpp:177-178`, `AssetThumbnailRenderer.cpp:97`. Both editor sites hand-build a
`SkySettings` mirroring the component they just authored — `PreviewViewport.cpp:167-176` field-by-field
from its own `SkyboxComponent`, and `AssetThumbnailRenderer.cpp:88-96` as a **second copy of the same eight
literals** it wrote at `:73-84` (**verified here**) — which is exactly the duplication the helper removes.
After this change both build a `SkyAtmosphereData` (from the `Studio Neutral` / thumbnail preset) and pass
it through the helper.

*Why:* RE's risk #4 — those two editor bypass paths are the ones most likely to be forgotten, and they are
the reason a "simple" signature change breaks asset thumbnails silently.

*Accept:* `git grep -n "SkySettings sky;" Editor/Source Desert` returns hits only inside `MakeSkySettings`;
`SceneRenderer::SetProceduralSky`'s new signature has no `CloudSettings` parameter and compiles at all
three sites.

---

## 5. Directional light requirements

**SKY-17 — `DirectionalLightData` (`Components.hpp:382-392`) gains two reflected fields.**

| Field | Type | Default | Annotations |
|---|---|---|---|
| `AtmosphereSunLight` | `bool` | `true` | `DisplayName("Atmosphere Sun Light"), Category("Atmosphere"), Tooltip("This light drives the sky, the sky's IBL bake and the cloud lighting.")` |
| `AtmosphereSunLightIndex` | `int` | `0` | `DisplayName("Atmosphere Sun Light Index"), Category("Atmosphere"), Range(0,0), EditCondition("AtmosphereSunLight"), Tooltip("The engine renders exactly one directional light; index 1 is reserved for a future second sun.")` |

Default `true` is deliberate: a missing field keeps its C++ default on load
(`ReflectionSerializer.cpp:138-140`), so every existing scene's single sun becomes the atmosphere sun with
**no migration for this part**.

*Why:* the architect's fixed input #3; and RE §7.4 confirms there is no notion of "the sun" today at all —
identity is by uniqueness, selection is "first found", and the sky and the lighting can pick *different*
lights because one uses an `entt` group and the other a view (`Scene.cpp:343-344` vs `SkyboxECSSystem.hpp:34`).

*Accept:* `Range(0,0)` renders as `ImGui::SliderInt` — **verified here**, `PropertyEditorBuilder.cpp:510-515`
(`HasRange ? SliderInt : DragInt`), so RE §7.6's "unverified" note is resolved: it works. After a `Desert`
build the two fields appear in `Reflection.gen.cpp` and in the Details panel with no widget code
(`ComponentEditorRegistrations.cpp:355-357`).

**SKY-35 — The sky's `SunColor`/`SunIntensity` and the light's `Color`/`Intensity` are separated BY MEANING, not left as two numbers for one thing. Each of the four fields carries a tooltip that names the other and states the split.**

The split, which is the definition both tooltips and both header comments must use:

| Quantity | Owner | Means | Answers |
|---|---|---|---|
| `SkyAtmosphereData::SunColor` × `SunIntensity` | the sky | **radiance of the sky and of the solar disk** — what the camera sees when it looks up | "how bright is the sun *in the picture*" |
| `DirectionalLightData::Color` × `Intensity` | the light | **illuminance arriving at scene surfaces** — what the BRDF integrates | "how bright is the sun *on the ground*" |

Required tooltip text, in substance: on the sky fields — *"Brightness and tint of the sky and the solar
disk as seen by the camera. Scene surfaces are lit by the directional light's own Colour and Intensity, not
by this."* On the light fields — *"Illumination arriving at scene surfaces. The sun you SEE in the sky is
the Sky Atmosphere component's Sun Colour / Sun Intensity."*

**Why this is not duplicated state — read this before "fixing" it.** DC §2.1 forbids one value living in
two places, and a reviewer meeting `SunIntensity = 22` next to `Intensity = 1` will reasonably suspect
exactly that. It is not. These are two different physical quantities with different units and different
consumers: the first is a **radiance** written into the sky's uniform block and the IBL bake
(`MaterialProceduralSky.hpp:57`, `ComputeImages.cpp:69`), the second is an **illuminance-like multiplier**
written into `DirectionLightsUB` and integrated by every PBR surface
(`ShaderProtocols/DirectionLight.hpp:7-18`, `MaterialPBRBase.cpp:139`). No code path reads one where it
means the other, and there is exactly one source of truth for each. What *would* be duplication — and is
forbidden — is deriving either from the other, mirroring one into the other, or writing both from one
widget. The single quantity they genuinely share is **direction**, and that has one owner already (SKY-20).

*Why:* the architect accepted "do not unify in v1" only on condition that the ambiguity is removed rather
than deferred; an undocumented split is how a future maintainer "simplifies" the sky into darkness.

*Accept:* all four fields carry the tooltips (checked in `Reflection.gen.cpp`'s `PropertyMetadata.Tooltip`
strings — they are emitted verbatim); a comment block above `SkyAtmosphereData`'s sun fields states the
split and points at `DirectionalLightData`, and vice versa; `git grep -n "SunIntensity" Desert Editor/Source`
shows no site reading it into a lighting path and no site deriving one field from the other.

**SKY-18 — Atmosphere-sun resolution is a pure function in `ECS/System/SystemRules.hpp` (namespace `Desert::ECS::Rules`):**

```
struct SunCandidate { Common::UUID Id; bool Marked; int Index; bool DirectionValid; };
std::optional<size_t> SelectAtmosphereSun( std::span<const SunCandidate>, int wantedIndex );
```

Rules, in order:
1. Candidates with `DirectionValid == false` (‖Translation‖ ≤ `1e-4`) are ignored entirely.
2. Among the rest, prefer those with `Marked == true` **and** `Index == wantedIndex` (0 in v1).
3. **Several marked at the same index:** pick the lowest `UUID` — deterministic and reproducible in a test —
   and `LOG_WARN` once per scene load naming every colliding entity. Mirrors `Scene.cpp:356-373`'s
   "truncate loudly and name the offender".
4. **Marked but at a different index:** treated as unmarked for v1 and `LOG_WARN` once, naming the entity
   and the index, because the field is authorable but index ≠ 0 is not rendered (SKY-19).
5. **No light marked:** fall back to the lowest-UUID valid candidate and `LOG_INFO` once, naming it. The sky
   must never go missing because nobody ticked a box.
6. **No valid candidate at all:** return `nullopt`; the sky then uses the documented fallback direction
   `normalize(0.3, 0.9, 0.3)` **toward** the sun (today's value, `SkyboxECSSystem.hpp:32`) and `AtmosphereEnv.Valid`
   is `false`.

*Why:* RE §7.2 lists **three different** no-light fallbacks and **two different** epsilons in the engine
today; a single tested rule replaces all of the sky-side ones.

*Accept:* unit test in `Desert/Tests/Engine/SystemRules/` covering all six rules, including the tie-break
determinism (same input in a different order gives the same UUID).

**SKY-19 — `AtmosphereSunLightIndex` is constrained to 0 in v1 by its `Range(0,0)`, and the one-directional-light truncation in `Scene.cpp:356-373` is left exactly as it is.**

*Why:* RE §7.3 — a second sun requires widening `ShaderProtocols::DirectionLight`, `DirectionLightsUB`,
every lit shader, the CSM path and the RSM. Shipping the field with a hard range and a tooltip is honest;
shipping a slider that does nothing at index 1 is a dead setting (DC §1.3).

*Accept:* the Range is `(0,0)`; no change to `Scene.cpp`; the tooltip states the limitation.

**SKY-20 — The single negation stays where it is. `Translation` is the direction the light TRAVELS (sun → scene); the atmosphere's toward-sun vector is `-normalize(Translation)`, computed in exactly one place, and `AtmosphereEnv.SunDirection` is that vector.**

*Why:* RE §7.2 — one negation exists today (`SkyboxECSSystem.hpp:40`) and the convention is documented in
five headers. Two negations is how a sky ends up lit from below.

*Accept:* `git grep -n -- "-glm::normalize" Desert/Desert/Source Editor/Source` — every remaining hit is
either the atmosphere-sun helper, the editor's own toward-sun display code
(`ComponentEditorRegistrations.cpp:365-374`, `ComponentWidgets/SkyboxComponent.cpp:194`), or the fixed
gizmo of SKY-21.

**SKY-21 — The inverted sign in `LightGizmoRenderer.cpp:191-196` is FIXED: `towardSun = -glm::normalize(t)`, `lightDir = -towardSun`. The repository scenes that author the sun below the horizon are re-authored (three survive; the fourth is deleted by SKY-28). User scenes are NOT auto-flipped; instead the engine warns once at load when the resolved atmosphere sun is below the horizon.**

The inverted authoring, **verified here** by parsing the files:

| File | Entity | `Translation` today | Elevation today | Action |
|---|---|---|---|---|
| `Editor/Resources/Assets/Scenes/Desert_Sandbox.desce` | `Sun` | `[0.3509, 0.9023, 0.2506]` | −64.4° (below) | negate → `[-0.3509, -0.9023, -0.2506]` |
| `Editor/Resources/Assets/Scenes/Starter.desce` | `Sun` | `[0.3509, 0.9023, 0.2506]` | −64.4° | negate |
| `Editor/Resources/Assets/Scenes/CornellDemo.desce` | `CB_Sun` | `[-0.5071, 0.8452, -0.1690]` | −57.7° | negate |
| `Editor/Resources/Assets/Scenes/Autosave/Desert_Sandbox_autosave.desce` | `Sun` | `[35.088, 90.226, 25.063]` | −64.4° | **deleted** (SKY-28) |
| `Editor/Source/EditorLayer.cpp:2024-2026` (`CB_Sun` builder) | — | `normalize(-0.6, 1.0, -0.2)` | −57.7° | negate |

Correct sites, for contrast: `EditorLayer.cpp:436` and `:2272` author `{-0.4, -1.0, -0.5}`;
`PreviewViewport.cpp:140` and `AssetThumbnailRenderer.cpp:65` are also correct.

*Why:* RE §7.2 and RE's risk #6. The gizmo comment at `:172-174` claims "same convention as
SkyboxECSSystem" while the code does the opposite — so the *documentation* is already on the side of the
fix. RE says "two shipped sandbox scenes"; there are in fact **four data sites plus one code site**
(verified here). We do not auto-flip user scenes because a sun below the horizon is a legal authored state
(night), so a blanket negation would break every deliberately-authored night scene.

*Accept:* (a) the gizmo's two lines match `ComponentEditorRegistrations.cpp:365-374`'s convention;
(b) a `python3` check over the three remaining scene files, plus reading the three `EditorLayer.cpp` sites,
asserts the resolved elevation is now positive everywhere;
(c) a `LOG_WARN` is emitted exactly once per scene load when `AtmosphereEnv.SunDirection.y < 0`, naming
the entity tag and the elevation in degrees.

**SKY-22 [CONTRACT B] — The cloud pass receives the sun through `AtmosphereEnv` (SKY-03) and by no other route. It must not read `DirectionLightComponent`, must not re-derive the direction from a transform, and must not negate anything.**

*Why:* two independent derivations of "the sun" is precisely the defect RE §7.2 documents in the current
engine, reproduced at a larger scale.

*Accept:* `git grep -n "DirectionLightComponent" <B's files>` is empty; B's push-constant block is filled
from `GetAtmosphere()`.

---

## 6. Migration requirements

Scene format: **JSON, extension `.desce`**, written by `rfl::json::write( SceneSerialized )`
(`SceneSerializer.cpp:136`), read at `:141`. Component payloads spread flat at the entity's top level via
`rfl::ExtraFields<rfl::Generic> Components` (`Assets/Prefab/PrefabData.hpp:163`), keyed by the
`ComponentRegistry` key — so the old sky lives under `"Skybox"` (RE §6.3, confirmed by parsing the files).

**SKY-23 — A new top-level integer `SceneVersion` is added to `SceneSerialized`, with `static constexpr int kSceneVersion = 1;`. Absent ⇒ 0. It is written on every save and is INDEPENDENT of `UnitVersion`.**

*Why:* `UnitVersion`'s contract is explicit — *"Bump this only if the world unit changes again"*
(`SceneSerializer.cpp:18-21`); reusing it would break that contract and couple two unrelated migrations.
DC §4.3 requires a version number and an explicit N→N+1 function.

*Accept:* every `.desce` in the repository contains `"SceneVersion":1` after the conversion of SKY-27;
`UnitVersion`'s value and its migration are untouched.

**SKY-24 — The migration is a pure function over the parsed JSON tree, not over the live scene:**

```
struct SkyMigrationReport { int Entities; int FieldsCarried; int FieldsDefaulted; int FieldsRejected; };
SkyMigrationReport MigrateSkyV0ToV1( std::vector<Assets::EntityData>& entities );   // pure
```

It is called from `SceneSerializer::DeserializeFromJson` immediately after
`rfl::json::read<SceneSerialized>` succeeds and **before** any entity is created, gated on
`sceneData->SceneVersion.value_or(0) < kSceneVersion`.

*Why:* this is the single most important correction to the obvious plan. `MigrateMetresToUnits`
(`SceneSerializer.cpp:32-90`) operates on the **live ECS scene** — a shape that **cannot** work here,
because once `SkyboxComponent`'s sky fields are deleted (SKY-14) the old values never reach the live scene
at all: the load loop iterates the *registry*, not the file (`EntitySerializer.cpp:91-96`). The data only
exists in the `rfl::Generic` tree. DC §4.4 independently requires the function to be pure with "old
property tree in, new property tree out".

*Accept:* the function's signature touches no GPU, no filesystem, no globals; it links in a test target
that does not link the renderer (the `ShadowCascades` precedent, `Desert/Tests/Engine/ShadowCascades/premake5.lua:11-12`).

**SKY-25 — Old→new mapping table. The function reads the `"Skybox"` payload of each entity and inserts a `"SkyAtmosphere"` payload on the same entity.**

| Old `"Skybox"` field | New `"SkyAtmosphere"` field | Transform |
|---|---|---|
| `Procedural` | `Enabled` | copy |
| `SunIntensity` | `SunIntensity` | copy |
| `SunDiskRadius` (rad, radius) | `SunAngularDiameter` (deg, diameter) | `degrees(v) * 2` |
| `ZenithColor` | `ZenithColor` | copy (3-element array) |
| `HorizonColor` | `HorizonColor` | copy |
| `GroundColor` | `GroundColor` | copy |
| `NightColor` | `NightColor` | copy |
| `SkyBrightness` | `SkyBrightness` | copy |
| `HorizonFalloff` | `HorizonFalloff` | copy |
| `SunColor` | `SunColor` | copy |
| `SunGlow` | `SunGlow` | copy |
| `SunsetColor` | `SunsetColor` | copy |
| `SunsetIntensity` | `SunsetIntensity` | copy |
| `StarIntensity` | `StarIntensity` | copy |
| `SkyboxHandle`, `Intensity` | *(remain under `"Skybox"`)* | untouched |
| `EnableClouds`, `CloudCoverage`, `CloudDensity`, `CloudTiling`, `CloudBrightness`, `CloudWindSpeed` | *(Analyst B's component)* | **not read by this function** **[CONTRACT B]** |
| *(none)* | `DriveSunFromTimeOfDay`, `TimeOfDay`, `DayLengthSeconds`, `Latitude`, `NorthOffset`, `AutoRebakeEnvironment`, `RebakeSunAngleThreshold`, `EnvironmentResolution`, `ActivePreset` | C++ defaults (`ActivePreset` = `Custom`: a migrated palette was hand-authored, so claiming a preset would be a lie — SKY-37) |

The component is created **whenever the entity has a `"Skybox"` payload**, regardless of `Procedural` —
`Enabled` carries the old flag. Creating it only for procedural scenes would silently discard an
HDR-mode scene's authored palette.

**SKY-26 — Behaviour for missing, malformed and already-migrated data.**

| Situation | Behaviour |
|---|---|
| Entity has no `"Skybox"` payload | untouched; nothing added; not counted |
| Entity already has a `"SkyAtmosphere"` payload | skipped entirely — the function is **idempotent** |
| A mapped field is absent | new field keeps its C++ default; `FieldsDefaulted++`; no warning (this matches `ReflectionSerializer.cpp:138-140`) |
| A mapped field has the wrong JSON type (string where number, object where array) | default kept; `FieldsRejected++`; `LOG_WARN` naming entity tag, field, and the offending value |
| A colour array has ≠ 3 elements, or any element is non-finite | default kept; `FieldsRejected++`; `LOG_WARN` |
| `SunDiskRadius` ≤ 0 or non-finite | `SunAngularDiameter` = `2.2918`; `FieldsRejected++`; `LOG_WARN` |
| The whole file fails to parse | unchanged from today: logged and the load aborts (`SceneSerializer.cpp:143-147`) |

*Why:* DC §1.4 forbids silent fallbacks — a rejected value must name itself and its number.

**SKY-27 — Logging. One `LOG_INFO` per migrated scene, plus the per-field `LOG_WARN`s above. Nothing is migrated silently (DC §4.7).**

Required form (values, not adjectives):
`[SceneMigration] '{scene}': sky schema v0 -> v1 — {Entities} entity(ies), {FieldsCarried} carried, {FieldsDefaulted} defaulted, {FieldsRejected} rejected`

*Accept:* the message is emitted from the loader (not from the pure function, which returns the report);
a test asserts the report's four counters for the fixture of T1.

**SKY-28 — Repository scenes converted in the same change.** All six currently-tracked `.desce` files
(**verified here** via `git ls-files`) — four converted, two deleted:

| File | Has `"Skybox"` | Action |
|---|---|---|
| `Editor/Resources/Assets/Scenes/Desert_Sandbox.desce` | yes, `Procedural: true` | migrate + stamp `SceneVersion:1` + negate `Sun` (SKY-21) |
| `Editor/Resources/Assets/Scenes/Starter.desce` | yes, `Procedural: true` | same |
| `Editor/Resources/Assets/Scenes/CornellDemo.desce` | no | stamp `SceneVersion:1` + negate `CB_Sun` |
| `Editor/Resources/Assets/Scenes/MainMenu.desce` | no (and no directional light) | stamp `SceneVersion:1` |
| `Editor/Resources/Assets/Scenes/Autosave/Desert_Sandbox_autosave.desce` | yes | **`git rm`** |
| `Editor/Resources/Assets/Scenes/Autosave/Scene_2_autosave.desce` | no | **`git rm`** |

**The two autosave scenes are deleted, and `Editor/Resources/Assets/Scenes/Autosave/` is added to
`.gitignore`** (architect's decision). They are machine-generated artefacts: tracking them means every
future schema change drags two extra files behind it, and a stale autosave on an old schema is precisely
the "one file in the repository that keeps the old path alive" DC §4.5 forbids. Deleting them is not data
loss — `Desert_Sandbox_autosave.desce` is a copy of `Desert_Sandbox.desce` (**verified here**: same
entities, `UnitVersion:1`, transforms already ×100).

*Accept:* `git ls-files Editor/Resources/Assets/Scenes` lists **four** files; `.gitignore` contains the
Autosave directory and `git status` is clean after the editor writes a new autosave; a `python3` assertion
over the four remaining files asserts `SceneVersion == 1`, that no entity carries any of the 17 removed
`"Skybox"` fields, that every entity that had `Procedural` now has a `"SkyAtmosphere"` payload with
`Enabled` equal to the old value, and that every directional light's elevation is ≥ 0.

**SKY-29 — Migration test cases.** In `Desert/Tests/Engine/SceneSkyMigration/` (GoogleTest, standalone
`ConsoleApp`, auto-discovered by `Desert/Tests/premake5.lua:5-9`):

| # | Case | Assertion |
|---|---|---|
| T1 | The real `Desert_Sandbox` `"Skybox"` payload, verbatim | all 12 same-named values carried exactly; `Enabled == true`; `SunAngularDiameter == 2.2918 ± 1e-4`; `ActivePreset == Custom`; report = `{1, 14, 9, 0}` |
| T2 | Payload with only `Procedural` | report = `{1, 1, 22, 0}` — `FieldsCarried` counts mapped fields found, `FieldsDefaulted` counts new fields left at their C++ default, and the two always sum to 23 |
| T3 | `"SkyBrightness": "bright"`, `"ZenithColor": "blue"` | defaults kept; `FieldsRejected == 2`; no throw |
| T4 | Run the migration twice | second run returns `{0,0,0,0}` and the tree is byte-identical |
| T5 | Entity with no `"Skybox"` | untouched, report all zeros |
| T6 | `"ZenithColor": [0.1, 0.2]` (2 elements) | default kept, `FieldsRejected == 1` |
| T7 | `"SunDiskRadius": 0.0` and `NaN` | `SunAngularDiameter == 2.2918`, `FieldsRejected == 1` each |
| T8 | Payload containing the six `Cloud*` fields | they are neither read nor removed by this function **[CONTRACT B]** |
| T9 | Full round trip: migrate → `DeserializeEntity` → `SerializeEntity` → JSON | the output has a `"SkyAtmosphere"` block and the `"Skybox"` block contains only `SkyboxHandle` and `Intensity` |
| T10 | Deliberately break the `degrees(v)*2` conversion to `degrees(v)` | T1 fails (DC §2.3: prove the test can fail) |

**SKY-30 — Ordering. The sky migration runs on the JSON before entity creation; `MigrateMetresToUnits` continues to run on the live scene after (`SceneSerializer.cpp:254-257`). Neither may depend on the other.**

*Why:* no sky field is a length, so there is no interaction — but that must be *stated*, because an old
metres-era scene will run both in one load and a future reader will ask.

*Accept:* a comment at both call sites states the independence; T1's fixture carries no `UnitVersion` and
still produces identical sky values.

---

## 7. Editor / UX requirements

**SKY-31 — Annotations per field.** The Details panel is annotation-driven
(`PropertyEditorBuilder.cpp:531-533` for floats, `:510-515` for ints, `:552`/`:559` for colours; categories
become sections at `:308` with an `Advanced` fold at `:319-334`). Required `PROPERTY(...)` per field:

| Field | Annotations |
|---|---|
| `Enabled` | `DisplayName("Enabled"), Category("Atmosphere"), Summary` |
| `SkyBrightness` | `DisplayName("Sky Brightness"), Category("Atmosphere"), Range(0,4), Units("x")` |
| `HorizonFalloff` | `DisplayName("Horizon Falloff"), Category("Atmosphere"), Range(0.1,2)` |
| `ZenithColor` / `HorizonColor` / `GroundColor` / `NightColor` | `DisplayName(…), Category("Sky Color"), Color` |
| `SunIntensity` | `DisplayName("Sun Intensity"), Category("Sun"), Range(1,50), Units("x"), Summary, Tooltip(…)` — tooltip text mandated by **SKY-35** |
| `SunColor` | `DisplayName("Sun Color"), Category("Sun"), Color, Tooltip(…)` — tooltip text mandated by **SKY-35** |
| `SunAngularDiameter` | `DisplayName("Sun Angular Diameter"), Category("Sun"), Range(0.1,10), Units("deg"), Tooltip("Apparent size of the solar disk. The real sun is 0.53 deg.")` |
| `SunGlow` | `DisplayName("Sun Glow"), Category("Sun"), Range(0,5)` |
| `SunsetColor` | `DisplayName("Sunset Color"), Category("Sun"), Color` |
| `SunsetIntensity` | `DisplayName("Sunset Intensity"), Category("Sun"), Range(0,3)` |
| `StarIntensity` | `DisplayName("Star Intensity"), Category("Night Sky"), Range(0,5)` |
| `DriveSunFromTimeOfDay` | `DisplayName("Drive Sun From Time Of Day"), Category("Time Of Day")` |
| `TimeOfDay` | `DisplayName("Time Of Day"), Category("Time Of Day"), Range(0,24), Units("h"), EditCondition("DriveSunFromTimeOfDay")` |
| `DayLengthSeconds` | `DisplayName("Day Length"), Category("Time Of Day"), Range(0,86400), Units("s"), EditCondition("DriveSunFromTimeOfDay"), Tooltip("Real seconds per in-game day. 0 freezes the sun at Time Of Day.")` |
| `Latitude` | `DisplayName("Latitude"), Category("Time Of Day"), Range(-90,90), Units("deg"), EditCondition("DriveSunFromTimeOfDay")` |
| `NorthOffset` | `DisplayName("North Offset"), Category("Time Of Day"), Range(0,360), Units("deg"), EditCondition("DriveSunFromTimeOfDay")` |
| `AutoRebakeEnvironment` | `DisplayName("Auto Rebake"), Category("Environment Lighting"), Advanced` |
| `RebakeSunAngleThreshold` | `DisplayName("Rebake Sun Angle"), Category("Environment Lighting"), Range(0.25,45), Units("deg"), Advanced, EditCondition("AutoRebakeEnvironment")` |
| `EnvironmentResolution` | `DisplayName("Environment Resolution"), Category("Environment Lighting"), Advanced, Tooltip("Size of the baked sky cubemap. High costs 32 MiB for the panorama alone, per live scene renderer.")` |
| `ActivePreset` | `DisplayName("Preset"), Category("Atmosphere"), ReadOnly, Tooltip("Which preset the palette came from. Reverts to Custom as soon as any sky colour or sun value is edited.")` |

*Why:* `EditCondition` is the engine's idiom for dependent rows (`Components.hpp:238`); `Advanced` is what
keeps three quality knobs out of an artist's face (`ReflectionMacros.hpp:27`). No field carries `Length` —
none of them is a world distance (`Docs/UNITS.md:28-39`).

*Accept:* after a `Desert` build, `Reflection.gen.cpp`'s `SkyAtmosphereData` block contains the exact
category strings above and the `EditCondition` targets name existing bool fields in the same block.

**SKY-32 — Registration surface.** The component is registered with
`DESERT_REGISTER_CUSTOM_COMPONENT( ECS::SkyAtmosphereComponent, "Sky Atmosphere", /*CanRemove=*/true, (lambda) )`
in a new `Editor/.../ComponentWidgets/SkyAtmosphereComponent.cpp`. The custom lambda adds, around the
reflected block: the preset row (SKY-12), the sky ramp preview moved from
`ComponentWidgets/SkyboxComponent.cpp:33-75` (`DrawSkyRamp`, skipped while a search filter is active,
`:202`), and the "Bake Sky IBL" button (`:209-217`) which sets `RequestBake`.

Also required, each a one-line edit:
* `ComponentWidgetRegistry.cpp:48-69` — add a branch `if ( has( "Sky Atmosphere" ) ) return "Lighting";`
  **placed before** the existing `has("Skybox") → "Rendering"` branch, so `Skybox` keeps its category.
  Without this the component lands in `"Other"` (`:68`).
* `ScenePropertiesPanel.cpp:31-68` — icon `ICON_MDI_WEATHER_SUNSET` (or the nearest available) and primary
  name, checked **before** `SkyboxComponent`.
* `SceneHierarchyPanel.cpp:205-220` — the same chain.
* `Scripting/ReflectionBindings.cpp:55-64` — the Lua entry (free with the `Data` shape).

`SkyboxComponent`'s existing widget keeps `CanRemove = false` and loses the Source-mode radio pair
(`ComponentWidgets/SkyboxComponent.cpp:110-118`), the `if (skybox.Procedural)` gate (`:179`) and the sun
elevation lookup (`:181-199`), which move to the new widget.

*Why:* RE §2.6 — the Add-Component menu iterates the registry, so registering the editor entry is what puts
the component in the menu; categories, icons and Lua are the four hand-maintained lists.

*Accept:* `DrawAddComponentMenu` shows "Sky Atmosphere" under Lighting (checked by reading the registration
code, since the GUI cannot run); the widget file compiles; `premake5 gmake2` re-run because a new `.cpp`
was added (RE §5.3).

**SKY-33 — Zero and duplicate sky components.**

* **None in the scene:** no error. `ResolveSkyMode` (SKY-15) returns `HdrCubemap` or `None`;
  `AtmosphereEnv.Valid == false`; the cloud pass must handle `Valid == false` by drawing nothing
  **[CONTRACT B]**. One `LOG_INFO` per scene load, not per frame.
* **Two or more:** the collector selects the one on the **lowest-UUID** entity and emits one `LOG_WARN`
  naming every sky entity found — the same "truncate loudly and name the offender" pattern as
  `Scene.cpp:356-373`. It must **not** be the current silent `break` after the first visited entity
  (`SkyboxECSSystem.hpp:90`), whose order is not deterministic.
* **Add Component when one already exists on this entity:** already prevented by the registry
  (`ComponentWidgetRegistry.cpp:95-96`, `:107`). Adding a second on *another* entity is allowed and hits
  the rule above.

*Accept:* a pure function `SelectPrimarySky( std::span<const Common::UUID> )` returning the lowest UUID,
unit-tested including the empty case and order-independence.

**SKY-34 — Systems and their registration sites.** Two new header-only systems in
`Desert/Desert/Source/Engine/ECS/System/`:

| System | `CanRunParallel` | Registered at |
|---|---|---|
| `TimeOfDayECSSystem` | `false` (writes transforms, SKY-10) | `EditorLayer.cpp:615-633`, `RuntimeLayer.cpp:81-94` |
| `SkyAtmosphereECSSystem` | `true` (read-only collector) | every site that currently registers `SkyboxECSSystem` |

The five current `SkyboxECSSystem` sites, **verified here**: `EditorLayer.cpp:619`,
`RuntimeLayer.cpp:83`, `PreviewViewport.cpp:146`, `AssetThumbnailRenderer.cpp:60`,
`PhotogrammetryPanel.cpp:626`. All five need the atmosphere collector too — the preview viewport and the
thumbnail renderer *rely* on the procedural sky as their backdrop, and the photogrammetry panel sets
`RequestBake` (`PhotogrammetryPanel.cpp:641`), which now lives on the new component.

*Why:* RE §2.5 — `EditorLayer` and `RuntimeLayer` must mirror each other exactly, and a missing registration
in the thumbnail path is invisible until every asset icon turns black.

*Accept:* `git grep -c "SkyAtmosphereECSSystem>" Editor/Source Runtime/Source` equals
`git grep -c "SkyboxECSSystem>"` over the same paths, and both equal 5.

---

## 8. Acceptance criteria — all checkable without launching the editor

Each row names the exact verification. The GUI cannot run here (DC §2.3, RE §5.1), so nothing below
depends on looking at a picture.

| # | Criterion | How it is verified | Covers |
|---|---|---|---|
| AC-01 | `make -j8 Desert config=debug` and `make -j8 Editor config=debug` build clean, no new warnings | run both; diff the warning count against `dev` | DC §2.4.1-2 |
| AC-02 | `make -j8 Desert config=release`, `make -j8 Editor config=release` build clean | run both | DC §2.4.1 |
| AC-03 | `premake5 gmake2 --with-tests` was re-run for the new `.cpp` files and the new test directory | the generated `*.make` name the new objects | RE §5.3 |
| AC-04 | `Generated/Reflection.gen.cpp` contains a `SkyAtmosphereData` block with **23** fields and the categories of SKY-31; `SkyboxComponent`'s block has 2 fields; `DirectionalLightData`'s has 4 | grep the committed generated file | SKY-04, -14, -17, -31 |
| AC-05 | No reference to a removed field survives | `git grep -nE "Procedural\b\|ZenithColor\|SunDiskRadius\|EnableClouds\|CloudWindSpeed" Desert Editor/Source Runtime/Source` returns only new-component hits | SKY-14 |
| AC-06 | `DShaderTool Editor/Resources/Shaders` parses every shader | run the CI command (`.github/workflows/ci.yml:121-122`) | SKY-01, -02 |
| AC-07 | Migration tests T1–T10 pass on Debug and Release | `./scripts/MacOS/RunTests.sh "$PWD" Debug` and `Release` | SKY-29 |
| AC-08 | T10 demonstrates the test can fail | temporarily break the conversion, show the failure, restore | DC §2.3 |
| AC-09 | `SelectAtmosphereSun` tests (six rules incl. tie-break determinism) pass | test target | SKY-18 |
| AC-10 | `SunDirectionFromTimeOfDay` tests (5 cases) pass | test target | SKY-09 |
| AC-11 | `ShouldRebakeSkyEnvironment` tests (6 cases) pass | test target | SKY-11 |
| AC-12 | `ResolveSkyMode` (4 cases) and `SelectPrimarySky` (3 cases) tests pass | test target | SKY-15, -33 |
| AC-13 | Preset tests: `A`/`Q` fields untouched, presets distinct, idempotent, one table row per enumerator | test target | SKY-07, -12 |
| AC-14 | GPU payload `static_assert`s hold and a field insertion breaks the build | add a member temporarily, show the failure, remove | SKY-13 |
| AC-15 | Every reflected field of `SkyAtmosphereData` is consumed by a named consumer | the reflection-driven coverage test of SKY-06 | SKY-06, DC §1.3 |
| AC-16 | The **four** remaining `.desce` files carry `"SceneVersion":1`, no removed `"Skybox"` field, and a `"SkyAtmosphere"` payload where one is due; the two autosaves are gone and the directory is ignored | a `python3` assertion script over the four files + `git ls-files` + `git status` clean after an autosave write | SKY-28 |
| AC-17 | Every directional light in the remaining scenes and in `EditorLayer.cpp` resolves to elevation ≥ 0 | the same script + reading `EditorLayer.cpp:436`, `:2024-2026`, `:2272` | SKY-21 |
| AC-18 | Loading a v0 scene logs the migration line with real counters | run the headless path that parses a scene, or assert the report values in the test and the format string by inspection | SKY-27 |
| AC-19 | A rejected field logs a `LOG_WARN` naming entity, field and value | T3/T6/T7 assert the report counters; the message text is reviewed | SKY-26 |
| AC-20 | `SkyAtmosphereECSSystem` registration count equals `SkyboxECSSystem`'s (5) | `git grep -c` | SKY-34 |
| AC-21 | Changed lines are clean under clang-format **18** | `/opt/homebrew/opt/llvm@18/bin/git-clang-format --binary /opt/homebrew/opt/llvm@18/bin/clang-format --diff <base>` after `git add` of new files | RE §4.7 |
| AC-22 | No `TODO`/`FIXME`/`XXX`/`HACK` in the new code | `git grep -nE "TODO\|FIXME\|XXX\|HACK"` over the diff | DC §1.1 |
| AC-23 | No new third-party dependency | `git diff --stat ThirdParty/` empty | DC §1.5 |
| AC-24 | `AtmosphereEnv` is populated per `(frame × renderer slot)` — no persistent shared buffer holds sky state | code review against `Docs/RENDERER_FRAME_STATE.md:62-86`; the sky's UB goes through a `Material`, which is per-slot already | RE §3.7 |
| AC-25 | Developer report lists: what was done, decisions and why, every deviation from this document and its reason | the report file | DC §2.4.6 |
| AC-26 | All four sun fields carry the SKY-35 tooltips, and no code path derives one from the other | grep `PropertyMetadata.Tooltip` in the generated file; `git grep -n "SunIntensity"` reviewed site by site | SKY-35 |
| AC-27 | `PresetFingerprint` changes for each of the 13 `P` fields and for none of 15–23; `ActivePreset` reverts to `Custom` on a `P` edit | test target (loop over fields) + the widget trace in the report | SKY-37 |
| AC-28 | `EnvironmentResolution::High` logs the byte cost once per renderer, with the live-renderer count | the size helper's unit test (32 MiB @ 2048×1024, 8 MiB @ 1024×512) + the verbatim log line in the report | SKY-36 |
| AC-29 | B's files contain no `SkyConfig`/`SkySettings`/`DirectionLightComponent` reference; `ParamsBuffer` appears only as a `SetStorageBuffer` argument | `git grep -n` over B's file list, run at integration | SKY-02, -03, -22, -40 |
| AC-30 | Both sky shaders build their `SkyConfig` **only** via `UnpackSkyConfig`; the two hand-written assignment blocks are deleted | grep `ProceduralSky.shader` and `BakeProceduralSky.shader` for `cfg.` assignments — expect none; `DShaderTool` parses | SKY-38 |
| AC-31 | The sky SSBO is created via the public `Create(...)` with `persistent = false`, sized `SKY_PACKED_VEC4_COUNT * 16`, at the binding the shader declares; the old `SkyUB` and the bake's push block are gone | read the two call sites + `git grep -n "SkyUB"` returns nothing | SKY-39, -13 |
| AC-32 | `Atmosphere.glslh` declares no sampler and no derivative/fragment-only construct | `grep -nE "dFdx\|dFdy\|fwidth\|discard\|gl_FragCoord\|gl_FrontFacing\|texture\(\|sampler"` returns empty — the guard for R-1 | §10 R-1 |

---

## 9. Decision record — closed by the architect

The six questions raised in v1 are decided. Nothing here is open; this section exists so that the *reason*
survives the decision. Each row names the requirement the decision became.

| # | Question (v1) | Decision | Became |
|---|---|---|---|
| 1 | Unify the sun's colour/intensity with the light's? | **No numeric unification in v1** — but the two must be separated by MEANING, with tooltips on all four fields and a written argument for why it is not duplicated state. Full unification is v2 and carries the re-authoring of all four sky-bearing scenes. | **SKY-35**, non-goal in §1 |
| 2 | Should `AtmosphereEnv` carry a full `SkyConfig` for B? | **No.** The cloud pass includes `Common/Atmosphere.glslh` and calls `EvaluateSky`. *We share the computation, not the representation* — so a change to the sky palette cannot break the cloud shader, and SKY-01's model choice stays genuinely reversible. | **SKY-02**, **SKY-03** |
| 3 | `Latitude`/`NorthOffset`, or a flat elevation/azimuth pair? | **Keep latitude/north.** Four floats and one tested pure function is an acceptable price for the model an artist recognises from UE. Approved unchanged. | **SKY-09** (unchanged) |
| 4 | Presets hardcoded or data-driven? | **One hardcoded table, in one translation unit, for BOTH halves.** Adding a preset is one row plus one enumerator. The applied preset's name is stored for display only and reverts to `Custom` on the first edit of a preset-driven field. A preset asset format is an explicit non-goal with its cost written down. | **SKY-12**, **SKY-37**, non-goal in §1 |
| 5 | Convert or delete the two autosave scenes? | **Delete, and `.gitignore` the directory.** Machine-generated artefacts that would otherwise be dragged along by every future schema change. | **SKY-28** |
| 6 | Confirm the resolution ladder? | **Confirmed:** three steps, `Medium` default, enum (not int) so the dispatch can never receive a non-power-of-two. Added: `High` must announce its actual byte cost and that the cost is per live `SceneRenderer`. | **SKY-36** |

**Also ruled, outside the six:** the gradient sky model (SKY-01) is accepted for v1. If Analyst B
establishes that scattering is needed for credible cloud *ambient* lighting, the question returns as a
**separate requirement with its own acceptance** — it does not reopen or rewrite this half, and nothing is
to be built in anticipation of it.

### Effect of these decisions on the v1 acceptance criteria

Checked deliberately, because a decision that quietly invalidates a criterion is worse than an open
question. **No criterion was broken.** Four were tightened and four added:

* SKY-07's test changes from "fields 20–22 and 15–19 unchanged" to "fields **15–23** unchanged" — strictly
  stronger, and it stays valid only because `Apply` was forbidden from writing `ActivePreset` itself.
* The field count moves 22 → 23, so AC-04 and the T1/T2 counters were restated (`{1,14,9,0}`,
  `{1,1,22,0}`); they were arithmetic on the field count, not on any behaviour.
* AC-16/AC-17 now cover four scene files instead of six, plus a `git ls-files`/`git status` check that the
  autosaves are gone and stay gone — a *stronger* check than converting them would have been.
* AC-13 gains the "one table row per enumerator" assertion, which is what makes SKY-12's "adding a preset
  is one row" claim enforceable rather than aspirational.
* New: AC-26 (SKY-35 tooltips and no cross-derivation), AC-27 (fingerprint / `Custom` revert),
  AC-28 (`High` memory announcement), AC-29 (B's files free of our representation types).

One decision *created* a risk worth naming: `ActivePreset` is the first piece of **editor-affordance state
stored in a scene file**. It is inert — no renderer reads it — but it is now migrated, undone, duplicated
and prefab-copied like everything else. SKY-25 therefore defaults it to `Custom` on migration rather than
guessing, and SKY-37 makes the revert fingerprint-driven so it cannot drift into a lie.

---

## 10. Known risks, with owners

| # | Risk | Owner | State |
|---|---|---|---|
| R-1 | `Atmosphere.glslh` compiling in a **compute** stage | A | **Largely retired — see below** |
| R-2 | The graphics/compute binding-number asymmetry silently aliases a descriptor | A | Mitigated by SKY-39's explicit rule + an in-code comment; the failure mode is a loud validation error, not a wrong picture |
| R-3 | Moving the IBL bake off push constants onto the SSBO regresses the bake | A | Covered by SKY-13's `static_assert`s and by AC-06; the bake's output is also the only sky artefact we can inspect without a GUI (it is an image the engine writes) |
| R-4 | A future palette field added to the include but not to the C++ mirror | A | `SKY_PACKED_VEC4_COUNT` `static_assert` (SKY-38) turns it into a build error |

### R-1 — reported as an unknown by Analyst B; checked, and it is mostly already answered

B's report that `Atmosphere.glslh` "has only ever been compiled as a fragment shader" is **incorrect on the
facts, in our favour**. `Editor/Resources/Shaders/Programs/Compute/BakeProceduralSky.shader` is a
`Compute { }` program that `#include`s `<Common/Atmosphere.glslh>` and calls `EvaluateSky` — **verified
here**, and it is not a dormant file: it is the IBL bake, driven every time the sky is baked
(`ComputeImages.cpp:39`, `:78-84`). So the include already compiles and runs in a compute stage today, on
every platform CI builds.

Independently, the file contains **no** stage-dependent construct: a grep for `dFdx`, `dFdy`, `fwidth`,
`discard`, `gl_FragCoord`, `gl_FrontFacing`, `texture(`, `textureLod` and `sampler` over
`Common/Atmosphere.glslh` returns **nothing** (**verified here**) — it declares no samplers at all and is
pure arithmetic over its parameters, which is why B's reading was right even though the premise was wrong.

**The residual risk is therefore much narrower than "will it compile", and it is still worth carrying:**

1. A future edit to the include that introduces a sampler or a derivative would break the bake *and* the
   cloud pass, and the sky's own fragment pass would keep working — so the author would not notice locally.
   **Mitigation:** the two compute consumers are covered by the CI shader lint
   (`.github/workflows/ci.yml:121-122`), which parses every `.shader`; keep it that way and never move the
   bake out of CI's path.
2. B's cloud shader will include `Atmosphere.glslh` **alongside** its own includes; a symbol collision
   (e.g. another include defining `PI`, `Remap` or `hash13`) is a real and ordinary failure that neither of
   us can see until the first build. Our side's names are already namespaced (`ATM_PI`, `atm_hash13`).
   **Mitigation:** any new helper added to the include takes the `atm_` prefix; owner A.

This is stated so the first build produces a known, owned item rather than a surprise — but on the evidence
it should compile, and if it does not, the cause will not be "compute cannot run this code".
