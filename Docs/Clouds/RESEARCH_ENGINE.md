# Clouds rewrite — the Desert Engine integration surface

**Purpose.** We are replacing the current flat 2D cloud layer with a first-class **volumetric** cloud
subsystem: its own ECS component, a full settings surface, presets. This document maps the engine as it
actually is, so nobody has to rediscover it. Everything here is cited `file:line` and was read in the
source, not assumed. Where a claim could not be checked it is marked **unverified** rather than guessed.

Verified against the working tree on branch `dev` at commit `b02730b`, 2026-08-10.

Paths are repo-relative to `/Users/daniilsavcenko/Desktop/Programming/C++/DesertEngine/`.

---

## Table of contents

1. [The cloud/sky path as it exists today](#1-the-cloudsky-path-as-it-exists-today)
2. [Adding a new ECS component, end to end](#2-adding-a-new-ecs-component-end-to-end)
3. [The render architecture we must plug into](#3-the-render-architecture-we-must-plug-into)
4. [Engine-wide conventions](#4-engine-wide-conventions)
5. [Hard constraints](#5-hard-constraints)
6. [Sky-settings migration](#6-sky-settings-migration)
7. [The directional light and the "atmosphere sun"](#7-the-directional-light-and-the-atmosphere-sun)

---

## 1. The cloud/sky path as it exists today

### 1.1 The whole chain, end to end

Clouds today are **six floats and a bool** carried from an ECS component to a fragment shader that paints
a flat noise layer over the procedural sky. There is no volume, no raymarch, no texture, no depth test,
no light integration, and nothing is baked into the IBL.

```
ECS::SkyboxComponent (Components.hpp:1445-1507)
  │   EnableClouds, CloudCoverage, CloudDensity, CloudTiling, CloudBrightness, CloudWindSpeed
  │   + 11 sky fields + Procedural/SunIntensity/SunDiskRadius/Intensity/SkyboxHandle + RequestBake
  ▼
ECS::SkyboxECSSystem::Update            Desert/.../ECS/System/SkyboxECSSystem.hpp:27-92
  │   sunDir = -normalize(dirLight Transform.Translation)   (:40)
  │   packs Graphic::CloudSettings (:55-61) + Graphic::SkySettings (:63-74)
  ▼
Graphic::Render::ProceduralSkyCommand    Desert/.../Graphic/Render/Commands/ProceduralSkyCommand.hpp:13-34
  │   Execute() -> renderer.SetProceduralSky(...)            (:30-33)
  ▼
SceneRenderer::SetProceduralSky          Desert/.../Graphic/SceneRenderer.cpp:907-918
  │   injects the SHARED scene wind direction into the cloud config (:913-914)
  ▼
System::SkyboxRenderer::SetProceduralSky Desert/.../Graphic/Systems/Scene/Skybox/SkyboxRenderer.hpp:30-45
  │   stores m_Clouds/m_Sky/m_SunDir; raises m_EnvDirty on first-enable or Bake (:43-44)
  ▼
System::SkyboxRenderer::Render           Desert/.../Graphic/Systems/Scene/Skybox/SkyboxRenderer.cpp:123-143
  │   MaterialProceduralSky::Update(...) then SubmitFullscreenQuad (:130-133)
  ▼
MaterialProceduralSky::Update            Desert/.../Graphic/Materials/Skybox/MaterialProceduralSky.hpp:25-71
  │   writes CameraUB + an 11-vec4 "SkyUB" block                (:43-70)
  ▼
Shader "ProceduralSky"                   Editor/Resources/Shaders/Programs/ProceduralSky/ProceduralSky.shader
      Fragment: EvaluateSky(...) then RenderClouds(...)        (:54-64)
      Common/Atmosphere.glslh  — the sky gradient model
      Common/Clouds.glslh      — the flat cloud layer
```

Registered as a render-graph pass at `SkyboxRenderer.cpp:113-121`:
`builder.AddPass( "SkyboxPass", RenderPhase::Sky, ... )`.

### 1.2 File-by-file

**`Desert/Desert/Source/Engine/Graphic/CloudSettings.hpp`** (21 lines, whole file).
`struct CloudSettings { bool Enabled; float Coverage, Density, Tiling, Brightness, WindSpeed; glm::vec2 WindDir; }`
(`:9-20`). The header comment says it plainly: "*painted in the sky shader, NOT volumetric*" (`:7`).
`WindDir` is **not** authored on the component — it is injected by `SceneRenderer::SetProceduralSky` from
the scene-global wind (`:17-19`, and `SceneRenderer.cpp:913-914`).

**`Desert/Desert/Source/Engine/Graphic/SkySettings.hpp`** (24 lines). Six `glm::vec3` colours plus five
scalars (`:11-22`). Linear colours. This is the artistic palette shared by the screen sky pass and the
IBL bake.

**`Desert/Desert/Source/Engine/Graphic/Materials/Skybox/MaterialProceduralSky.hpp`** (73 lines).
A `Material` subclass bound to shader `"ProceduralSky"` (`:21-23`). `Update()` (`:25-71`) writes two
uniform blocks by reflected name: `ShaderProtocols::Camera::Name` (`:35-36`) and `"SkyUB"` (`:69-70`).
Two things matter for the rewrite:
* **Time is self-generated** from a function-local `static` `steady_clock` start (`:39-41`) — not a
  per-frame `Timestep`, and not the scene wind's `Time`. A volumetric system that wants deterministic
  animation, pausing, or scrubbing must not copy this.
* The `SkyUBData` struct (`:43-56`) is a hand-maintained mirror of the shader's `SkyUB` block. It is
  `memcpy`'d raw (`:70`), so a field added on one side and not the other silently corrupts everything
  after it.

**`Desert/Desert/Source/Engine/Graphic/Render/Commands/ProceduralSkyCommand.hpp`** (35 lines). A plain
`RenderCommand` POD carrying `Enabled, SunDir, SunIntensity, SunDiskRadius, BakeNow, Clouds, Sky`
(`:15-21`) and calling one setter (`:30-33`). This is the pattern a `VolumetricCloudCommand` would copy.

**`Desert/Desert/Source/Engine/Graphic/Systems/Scene/Skybox/SkyboxRenderer.{hpp,cpp}`** (93 + 145 lines).
The Sky-phase render system. Notable facts:
* It owns **two** pipelines against the same target and the same phase — the HDR cubemap path
  (`m_Pipeline`, `SkyboxRenderer.cpp:26-38`) and the procedural path (`m_ProceduralPipeline`, `:41-56`) —
  and `Render()` picks between them (`:128-142`). Both are `DepthTestEnabled = false`,
  `DepthWriteEnabled = false`, `CullMode::None` (`:33-36`, `:47-49`).
* `EnsureProceduralEnvironment()` (`:80-111`) is the IBL bake. It calls
  `Renderer::WaitDeviceIdle()` (`:87`) and must run **outside** the render graph — the header says so
  explicitly (`SkyboxRenderer.hpp:47-49`), and `SceneRenderer::OnUpdate` calls it before
  `ExecuteRenderGraph` (`SceneRenderer.cpp:363-368`).
* `GetEnvironment()` (`SkyboxRenderer.hpp:51-62`) is what feeds ambient/reflections to PBR. **Clouds do
  not participate**: `EnvironmentManager::CreateProcedural` takes only `sunDir/intensity/diskRadius/sky`
  (`Graphic/Environment/SceneEnvironment.hpp:33-34`), no `CloudSettings`. So today an overcast sky lights
  the scene exactly like a clear one.

**`Desert/Desert/Source/Engine/ECS/System/SkyboxECSSystem.hpp`** (94 lines, header-only).
`CanRunParallel() -> true` (`:22-25`). Handles exactly **one** skybox entity — the loop `break`s after the
first (`:90`). Consumes and clears the transient `RequestBake` (`:52-53`).

**`ECS::SkyboxComponent`** — `Desert/Desert/Source/Engine/ECS/Components.hpp:1445-1507`. Full field
inventory in [§6.1](#61-every-sky-field-and-who-reads-it).

**`SceneRenderer::SetProceduralSky`** — declared `Graphic/SceneRenderer.hpp:127-128`, defined
`Graphic/SceneRenderer.cpp:907-918`. **Three** call sites:
* `ProceduralSkyCommand::Execute` — the ECS path (`ProceduralSkyCommand.hpp:32`).
* `Editor/Source/Editor/Widgets/PreviewViewport.cpp:177-178` — the Details mesh preview, which bypasses
  the ECS and calls the setter directly "so the sky is enabled from frame 0".
* `Editor/Source/Editor/Widgets/AssetThumbnailRenderer.cpp:97` — the asset thumbnail renderer, same
  reason (`:63-65`).

Both editor call sites pass `Graphic::CloudSettings{}` (clouds off) and a hand-built `SkySettings`.
**Any signature change to `SetProceduralSky` breaks all three.**

**`Desert/Desert/Source/Engine/Graphic/WindEnv.hpp`** (17 lines). The evaluated per-frame form of the
scene-global wind: `Direction` (normalized XZ), `Strength`, `Turbulence`, `Time` (`:14-15`). Built once
per frame in `SceneRenderer::BeginScene` from `SceneSettings.Wind*` (`SceneRenderer.cpp:285-292`) and read
via `SceneRenderer::GetWind()` (`SceneRenderer.hpp:143-146`). Its own comment names clouds as a future
consumer (`WindEnv.hpp:8-10`). **This is the right place for a volumetric system to get wind** — do not
re-derive it.

`SceneSettings` wind fields: `Desert/Desert/Source/Engine/Core/SceneSettings.hpp:198-203` —
`WindDirection` (degrees, `Range(0,360)`), `WindStrength`, `WindTurbulence`. The comment at `:194-197`
is explicit that wind is deliberately scene-global and **not** owned by the Skybox.

### 1.3 The shaders

Three files, all under `Editor/Resources/Shaders/`:

**`Programs/ProceduralSky/ProceduralSky.shader`** (95 lines). DSL format: `Shader "ProceduralSky" { Fragment { ... } Vertex { ... } }`.
* Vertex (`:72-94`) builds a **direction-only** world-space view ray by unprojecting to view space and
  rotating with `mat3(inverse(cameraUB.View))` (`:90-92`). The comment (`:85-89`) explains why: the
  far-plane-worldPos-minus-cameraPos form loses precision as the camera moves and makes the sun/stars
  boil. **A volumetric raymarch needs the camera position anyway** (it is already passed separately as
  `u_CameraPos`, `:24`) but should keep the direction reconstruction as-is.
* Fragment (`:35-69`): builds a `SkyConfig` from `SkyUB` (`:41-52`), calls `EvaluateSky` (`:54`), then
  conditionally `RenderClouds` (`:58-64`). Output is **linear HDR, alpha 1.0** (`:68`) — the tonemap pass
  applies exposure/gamma downstream.
* ⚠️ **The `u_CloudParams` comment at `:23` is wrong**: it says "x = layer base altitude; y = thickness"
  but the C++ writes `x = tiling, y = brightness` (`MaterialProceduralSky.hpp:60`) and the call site at
  `:62` uses it that way. The correct comment is at `:56-57`. Do not trust `:23`.

**`Common/Atmosphere.glslh`** (118 lines). The sky model, shared by the screen pass and the IBL bake
(`:1-5`). `struct SkyConfig` (`:13-26`), `DefaultSkyConfig()` (`:28-43`), `EvaluateSky(dir, sunDir,
sunIntensity, sunDiskRadius, cfg)` (`:53-116`). It is **not** Rayleigh+Mie despite what several C++
comments claim (`MaterialProceduralSky.hpp:15-16`, `Components.hpp:1457`, `ProceduralSky.shader:5`) — it
is an artistic gradient model driven entirely by `sunDir.y`: day/night blend and sunset window (`:62-64`),
horizon→zenith `smoothstep` gradient (`:71`), sunset horizon band (`:74-80`), soft Gaussian sun disk plus
halo (`:87-93`), direction-space stars (`:98-109`), ground haze below the horizon (`:112-113`).

**`Common/Clouds.glslh`** (118 lines). **This is what dies.** The whole model:
* Bails out below `dir.y <= 0.03` (`:60-61`) — there is no horizon-wrapping cloud dome.
* Flat-ceiling projection `uv = (dir.xz / dir.y) * 0.15 * tiling` (`:65`), wind drift added as
  `uv += normalize(windDir) * time * windSpeed * 0.010` (`:66-67`).
* 2D simplex noise (`cl_snoise`, `:17-39`) and a 6-octave fBm (`cl_fbm2`, `:41-53`).
* Domain warp (`:71-72`), coverage threshold `df = shape - (1 - cov)` then
  `opacity = smoothstep(0, 0.30, df) * density` (`:77-81`).
* Fake lighting: a normal from the noise gradient (`:90-93`), a two-tap "self-shadow" toward the sun
  (`:98-100`), day/sunset/night tinting (`:102-113`), then `mix(skyColor, cloudCol, opacity)` (`:115`).

There is **no** existing volumetric machinery to build on: no 3D noise, no raymarch loop, no
Beer–Lambert transmittance, no phase function, no light march. All of that is new.

**`Programs/Compute/BakeProceduralSky.shader`** (65 lines) — the IBL bake compute. Writes an equirect
RGBA32F panorama via `imageStore` (`:16`, `:62`), 128-byte push-constant block (`:18-28`),
`LocalSize(32,32,1)` (`:30`). It calls `EvaluateSky` only — **clouds are absent from the bake**.

### 1.4 The editor side

**`Editor/Source/Editor/Panels/SceneProperties/ComponentWidgets/SkyboxComponent.cpp`** (220 lines).
There are **no hand-written cloud sliders**. The file says so at `:78-81`: most of the panel is
auto-generated from `REFLECT()`/`PROPERTY()` metadata by `PropertyEditorBuilder::Draw( &skybox,
"SkyboxComponent", ... )` (`:205-206`). The cloud sliders exist purely because of the
`PROPERTY( ..., Category("Clouds"), Range(...) )` annotations at `Components.hpp:1491-1503`.

What *is* hand-drawn:
* Source-mode radio pair, HDR vs Procedural (`:110-118`).
* HDR asset picker + drag-drop + search popup (`:121-174`), which is why `SkyboxHandle` is `Hidden`.
* One hand-written property row, the canonical idiom (`:169-173`):
  `ResetPropertyRows()` → `BeginPropertyRow("Intensity")` → widget → `EndPropertyRow()`.
* `DrawSkyRamp` (`:33-75`), an `ImDrawList` colour-ramp strip with a sun marker, skipped while a search
  filter is active (`:202`). **This is the template for a cloud preview strip.**
* The `"Bake Sky IBL"` button (`:209-217`), which just sets `skybox.RequestBake = true`.

Registration: `DESERT_REGISTER_CUSTOM_COMPONENT( ECS::SkyboxComponent, "Skybox", false, ( lambda ) )`
at `:82-84`. The `false` is `CanRemove` — Skybox cannot be removed from the Details header.

⚠️ `ComponentWidgets/SkyboxComponent.hpp:7-21` declares a `SkyboxComponentWidget : ComponentWidget<...>`
class that is **never defined and never instantiated** — dead legacy scaffolding. Do not copy it.

How a reflected float becomes a slider: `Editor/.../PropertyEditor/PropertyEditorBuilder.cpp:531-533` —
`HasRange ? SliderFloat(min,max) : DragFloat(0.01f)`. Colours: `ColorEdit3/4` at `:552`/`:559`.
`Category` becomes a section via `Utils::ImGuiUtilities::SectionHeader` (`:308`), with an `Advanced` fold
at `:319-334`.

**Scene Settings panel** — `Editor/Source/Editor/Panels/SceneSettings/SceneSettingsPanel.cpp` (238
lines). Hand-written, **not** reflection-driven, despite `SceneSettings` being reflected. Wind section at
`:226-232`, raw `ImGui::SliderFloat` calls plus the note *"Shared: one direction moves grass AND clouds."*
**There are no sky or cloud fields in `SceneSettings` at all** — see [§6.2](#62-scenesettings-has-no-sky-fields-left).

**Preset precedent.** There is no generic/serialized preset system anywhere. Every preset in the engine is
a `constexpr` table of C++ functions. The closest model is the Particle Editor:
`Editor/Source/Editor/Panels/Particles/ParticleEditorPanel.cpp` — `struct Preset { const char* Name; void
(*Apply)( Data& ); };` (`:27-31`), five apply functions (`:33-122`), `constexpr Preset kPresets[]`
(`:124-127`), drawn as a button row (`:223-231`). A preset *dropdown* exists at
`Editor/.../Photogrammetry/PhotogrammetryPanel.cpp:129-160` (`PresetCombo`).
⚠️ Note the tension with the engine's own headline anti-pattern, "hardcoding content into a mechanism"
(`.claude/skills/desert-engine-dev/SKILL.md:202-218`) — a hardcoded cloud-preset table has precedent but
a reviewer may ask for data-driven presets. Decide this deliberately, not by default.

### 1.5 What dies, what survives, what changes

| File | Verdict | Why |
|---|---|---|
| `Editor/Resources/Shaders/Common/Clouds.glslh` | **DIES** | The entire flat-layer model is replaced. |
| `Desert/.../Graphic/CloudSettings.hpp` | **DIES / is replaced** | 6 scalars describing a flat layer; a volumetric system needs altitude, thickness, extinction, scattering, step counts, phase g, weather-map controls, detail-noise controls. Keep the name, replace the contents, or retire it for a `VolumetricCloudSettings`. |
| The 6 `Cloud*` fields of `ECS::SkyboxComponent` (`Components.hpp:1491-1503`) | **MOVE OUT** | They become the new component. See [§6](#6-sky-settings-migration) for the migration problem. |
| `ProceduralSky.shader` cloud call (`:56-64`) | **DIES** | The `RenderClouds` invocation and the `u_SkyParams.y/z/w` + `u_CloudParams` + `u_WindDir` fields of `SkyUB`. |
| `MaterialProceduralSky.hpp` `SkyUBData` cloud fields (`:46`, `:60`, `:68`) | **CHANGES** | The struct must shrink in lockstep with the shader block. |
| `ProceduralSkyCommand.hpp` `Clouds` field (`:20`, `:24`, `:26`, `:32`) | **CHANGES or DIES** | If clouds get their own component they get their own command; the sky command loses `CloudSettings`. |
| `SceneRenderer::SetProceduralSky` (`SceneRenderer.hpp:127-128`, `.cpp:907-918`) | **CHANGES** | Signature loses `CloudSettings`; the wind injection at `:913-914` moves to the new path. **Three call sites** must move together (§1.2). |
| `SkyboxRenderer.{hpp,cpp}` — `m_Clouds`, the `CloudSettings` parameter | **CHANGES** | Cloud state leaves; the Sky pass keeps sun + palette + IBL bake. |
| `Common/Atmosphere.glslh` | **SURVIVES UNTOUCHED** | The sky model is orthogonal. Volumetric clouds composite *over* whatever `EvaluateSky` returns. |
| `Graphic/SkySettings.hpp` | **SURVIVES** (may move) | Still needed by the sky pass and by `EnvironmentManager::CreateProcedural` (`SceneEnvironment.hpp:33-34`). If sky settings move to a new component, this transport struct stays; only its *source* changes. |
| `Graphic/WindEnv.hpp` + `SceneSettings.Wind*` | **SURVIVES UNTOUCHED** | Read it via `SceneRenderer::GetWind()`. Do not add a second wind. |
| `SkyboxRenderer::EnsureProceduralEnvironment` + `EnvironmentManager::CreateProcedural` + `BakeProceduralSky.shader` | **SURVIVES; likely EXTENDS** | If overcast skies should darken scene lighting, the bake needs a cloud term. That is a new requirement, not a port. |
| `Assets/Skybox/SkyboxAsset.*`, `Runtime/Services/Skybox/SkyboxService.*`, `Materials/Skybox/MaterialSkybox.*`, `Programs/Skybox/Skybox.shader` | **UNTOUCHED** | The HDR-cubemap path never had clouds. |
| `Editor/.../ComponentWidgets/SkyboxComponent.cpp` | **CHANGES** | The `Clouds` category disappears from its reflected block; a new widget file appears for the cloud component. |
| `Editor/.../SceneSettings/SceneSettingsPanel.cpp` | **UNTOUCHED** | Wind stays; no sky/cloud fields live there. |

---

## 2. Adding a new ECS component, end to end

Traced through `SkyboxComponent` and cross-checked against `ParticleEmitterComponent` (the `Data`-block
shape) and `FolderComponent` (marker).

### 2.1 The two structural shapes — choose first, it drives everything

| Shape | Example | `REFLECT()` sits on | Serializer helper | Editor helper | Lua |
|---|---|---|---|---|---|
| **`Data` sub-block** (preferred) | `ParticleEmitterData` + `ParticleEmitterComponent` — `Components.hpp:488`, `:561-563` | the `*Data` struct | `MakeReflected<C,D>` | `DESERT_REGISTER_REFLECTED_COMPONENT` (one line) | bindable |
| **Fully reflected in place** | `SkyboxComponent` — `Components.hpp:1445-1507` | the component | `MakeReflectedSelf<C>` | needs a custom entry | **not bindable** |
| **Marker** (no data) | `FolderComponent` — `Components.hpp:1512-1514` | none | `MakeMarker<C>` | n/a | n/a |

The Lua constraint is real: `Desert/.../Scripting/ReflectionBindings.cpp:36-52` (`MakeEntry`) hard-assumes
a `.Data` sub-member (`&r.get<TComp>(e).Data`), and the binding list is
`kReflectedComponents[]` at `:55-64`. `SkyboxComponent` is absent from that list precisely because it is
flat. **Recommendation for the cloud component: use the `Data`-block shape**, so Lua and the one-line
editor registration both come free.

### 2.2 Reflection macros

`Desert/Desert/Source/Engine/Reflection/ReflectionMacros.hpp:33-34`:

```cpp
#define REFLECT()
#define PROPERTY( ... )
```

Both expand to **nothing** at compile time — `DesertHeaderTool` parses them out of raw source. Attribute
vocabulary is documented at `ReflectionMacros.hpp:19-30`:
`DisplayName("…")`, `Category("…")`, `Tooltip("…")`, `Header("…")`, `Range(min,max)`, `Color`,
`Asset<TypeName>`, `Thumbnail`, `ReadOnly`, `Hidden`, `Length`, `Units("…")`, `Advanced`, `Summary`,
`Temperature`, `Preview`, `EditCondition("Foo")` / `("!Foo")`.

* A field **without** a `PROPERTY(...)` above it is invisible to reflection — not serialized, not shown.
  That is how `SkyboxComponent::RequestBake` (`Components.hpp:1505-1506`) stays transient. Preserve that
  trick for any one-shot cloud request flag.
* `EditCondition("EnableVolumetricClouds")` is the idiomatic way to grey out dependent rows — precedent
  at `Components.hpp:238` (`EditCondition("EnableGrass")`).
* `Length` marks a world distance in centimetres (`Docs/UNITS.md:28-39`). **Cloud layer altitude and
  thickness must carry `Length`.**
* **Not supported by the reflection path**: vector-of-struct fields (`Docs/README.md:20-22`). Those force
  a hand-written serializer.
  > **CORRECTION (architect, during requirements review).** An earlier revision of this line also listed
  > `std::string` and variants as unsupported. That was wrong, and the cited source never said it —
  > `Docs/README.md:20-22` names vector-of-struct and nothing else. `std::string` is supported end to end:
  > `FieldType::String` exists (`Engine/Reflection/ReflectionTypes.hpp:15-30`), the codegen emits it for 17
  > fields today (`UIInputField::Text`, `AudioComponent::Clip`, …, `Engine/Generated/Reflection.gen.cpp`),
  > the serializer round-trips it (`Engine/Reflection/ReflectionSerializer.cpp:97`, `:169`) and the editor
  > draws it as an `InputText` (`Editor/.../PropertyEditorBuilder.cpp:205`, `:565`). The claim reached a
  > requirements document and decided a design choice there before it was caught. Widening a cited
  > limitation beyond what the citation says is the specific failure — cite what the source states, and
  > verify anything that decides a design.

### 2.3 Codegen

Tool: `Tools/DesertHeaderTool/main.cpp` (899 lines, standalone). Usage contract at `:7-12`
(`DesertHeaderTool <source-root> <output-file> [scan-subdir]`); scans `.hpp`/`.h` recursively, skipping
any filename containing `.gen.` (`:863`); **content-compares before writing** (`:882-889`) so an unchanged
run does not trigger a rebuild.

Invocation: a **prebuild command on the `Desert` project**, `Desert/Desert/premake5.lua:8-17`. Baked into
`Desert.make:56` (Debug) / `:70` (Release). There is no script and no manual step — **it runs on every
`Desert` build**.

Output: `Desert/Desert/Source/Engine/Generated/Reflection.gen.cpp` (516 lines), banner at `:1-3`
("AUTO-GENERATED … DO NOT EDIT", plus `// clang-format off`). It reflects exactly two headers today:
`Engine/Core/SceneSettings.hpp` and `Engine/ECS/Components.hpp` (`:13-14`).

**The generated file IS committed to git** — `git ls-files Desert/Desert/Source/Engine/Generated/` returns
`Reflection.gen.cpp`. A new component produces a diff here that must be committed.

What it registers, per type: name string, `sizeof`, and per field `Name`, `FieldType`, `offsetof`,
`sizeof`, C++ type-name string, and the full `PropertyMetadata` — plus `EnumValues` for enums. The
`SkyboxComponent` block is `Reflection.gen.cpp:439-466`, ending `.WithDefault<T>().Register();`
(`:464-465`). Static-init linkage is anchored by `ForceLinkGeneratedReflection()` (`:513-516`, declared
`Reflection/ReflectionRegistry.hpp:40`, called `Graphic/Renderer.cpp:40`) because `Desert` is a static lib.

### 2.4 Serialization

`Desert/Desert/Source/Engine/Core/Serialize/ComponentRegistry.cpp` (1074 lines). Three factories, all in
the anonymous namespace:
* `MakeReflected<TComponent, TData>( key, typeName, member )` — `:51-81`
* `MakeMarker<TComponent>( key )` — `:85-99`
* `MakeReflectedSelf<TComponent>( key, typeName )` — `:366-398` (adds an `AssetResolver` so `AssetHandle`
  fields round-trip as paths)

The Skybox entry, verbatim (`ComponentRegistry.cpp:1010-1016`):

```cpp
        // ---- Skybox (now FULLY REFLECTED via RA3) ----
        // No more hand-written SkyboxComponentSer / field mapping: the whole component reflects, and its
        // SkyboxHandle round-trips as a path through the AssetResolver. (RequestBake has no PROPERTY → it's
        // excluded automatically.) Compat note: old scenes stored the HDR under key "SkyboxPath"; the
        // reflected field is "SkyboxHandle", so an old HDR selection needs re-pick — procedural sky +
        // clouds carry over (those field names are unchanged).
        Register( MakeReflectedSelf<ECS::SkyboxComponent>( "Skybox", "SkyboxComponent" ) );
```

The `Data`-block one-liner, for comparison (`ComponentRegistry.cpp:965-966`):

```cpp
        Register( MakeReflected<ECS::ParticleEmitterComponent, ECS::ParticleEmitterData>(
             "ParticleEmitter", "ParticleEmitterData", &ECS::ParticleEmitterComponent::Data ) );
```

Adding a `PROPERTY` field to an already-registered component extends serialization with **zero** code
change (`ComponentRegistry.cpp:49-50`). Registering a *new* component is one manual line.

Scene save/load is fully generic: `EntitySerializer.cpp:58-63` (save) and `:87-96` (load) both iterate
`ComponentRegistry::Get().All()`. `TagComponent`/`UUIDComponent`/`TransformComponent` are special-cased
entity meta (`:50-56`, `:70-85`) and are not in the registry.

⚠️ **The load loop iterates the registry, not the file** (`EntitySerializer.cpp:91-96`). Consequence: a
component key present in an old file but absent from the registry is never even read. This is the crux of
the migration problem — see [§6.4](#64-what-a-migration-must-do).

### 2.5 The ECS system

Base class `Desert/Desert/Source/Engine/ECS/System/System.hpp`: `class System` (`:11`), non-copyable
(`:16-19`), pure-virtual `Update( entt::registry&, Graphic::Render::RenderCommandBuffer&, const
Common::Timestep& )` (`:23-24`), `CanRunParallel()` defaulting to `false` (`:31-34`) with the contract at
`:26-30`, and `SetCameraSnapshot` (`:39-41`).

All 14 ECS systems live header-only in `Desert/Desert/Source/Engine/ECS/System/`.

Registration is **manual and order-significant** — `Scene::AddSystem<T>()` at `Core/Scene.hpp:176-180`;
scheduling at `Core/Scene.cpp:401-435`, where a maximal consecutive run of `CanRunParallel()` systems is
dispatched as one JobSystem group (`:415-435`) and the command buffers replay in registration order.

**Five call sites** must be considered:

| # | Site | Needed for clouds? |
|---|---|---|
| 1 | `Editor/Source/EditorLayer.cpp:619` (`BuildSceneSystems`, `:615-633`) | **yes** |
| 2 | `Runtime/Source/RuntimeLayer.cpp:83` (`:81-94`, "Same system set + order as the editor's Play mode") | **yes — must mirror #1 exactly** |
| 3 | `Editor/Source/Editor/Widgets/PreviewViewport.cpp:146` | only if the mesh preview should show clouds |
| 4 | `Editor/Source/Editor/Widgets/AssetThumbnailRenderer.cpp:60` | almost certainly **no** (cost per thumbnail) |
| 5 | `Editor/Source/Editor/Panels/Photogrammetry/PhotogrammetryPanel.cpp:626` | no |

Pure decision logic that should be unit-testable goes in
`Desert/Desert/Source/Engine/ECS/System/SystemRules.hpp` (namespace `Desert::ECS::Rules`), tested by
`Desert/Tests/Engine/SystemRules/system_rules_test.cpp`.

### 2.6 Editor registration

Registry: `Editor/Source/Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp` —
`ComponentEditContext` (`:36-64`, including `FieldFilter` at `:58`), `ComponentEditorEntry` (`:67-83`),
`ComponentWidgetRegistry` (`:87-101`), `MakeReflectedComponentEntry` (`:117-147`),
`MakeCustomComponentEntry` (`:150-164`).

The two macros, `ComponentWidgetRegistry.hpp:172-189`:

```cpp
#define DESERT_REGISTER_REFLECTED_COMPONENT( ComponentT, Member, DataTypeName, DisplayName )
#define DESERT_REGISTER_CUSTOM_COMPONENT( ComponentT, DisplayName, CanRemove, DrawLambda )
```

Both expand to an anonymous-namespace `const int _desert_component_reg_<COUNTER> = …Register(…)`, i.e.
static-init self-registration. Line `:182` warns: **wrap the custom lambda in parentheses** so its commas
do not split the macro arguments.

The one-liner home is `Editor/Source/Editor/Panels/SceneProperties/ComponentEditorRegistrations.cpp:46-90`,
whose file header (`:1-3`) says "*To expose a new reflected component in the editor, copy one line
below*". Custom entries are built as `static ComponentEditorEntry Make*Entry()` (e.g. `MakeEmitterEntry`
at `:480-513`) and registered in an anonymous namespace at `:1124-1158`.

**"Add Component" menu**: `DrawAddComponentMenu` — declared `ComponentWidgetRegistry.hpp:113`, defined
`.cpp:72-128`. **There is no hand-maintained list of addable components** — it iterates
`ComponentWidgetRegistry::Get().Entries()` and skips what the entity already has (`:95-96`, `:107`,
`:120-121`). Registering the editor entry is what puts a component in the menu. Adds are undoable via
`Commands::MutateEntityUndoable` (`:85-87`).
Categories, however, **are** a hand-maintained keyword table — `kAddCategories` at `.cpp:41-46` and
`CategoryOf()` at `:48-69` (substring match on the display name; `"Skybox" → "Rendering"` at `:56`).
Unmatched names fall to `"Other"` (`:68`).

### 2.7 Round-trip: what is automatic

| Concern | Mechanism | Automatic? |
|---|---|---|
| Scene save/load | `EntitySerializer.cpp:58-63` / `:87-96` | ✅ via `ComponentRegistry` |
| Undo/redo of add, remove, and field edits | `Commands::MutateEntityUndoable`, `SceneCommands.cpp:869-888`; snapshot via `CaptureSubtree` (`:64-83`), replay via `RestoreSnapshot` (`:86-124`) | ✅ (it serializes) |
| Duplicate / copy | `DuplicateEntity` `SceneCommands.cpp:598-621`, `DuplicateEntities` `:623-645` — both are `RestoreSnapshot( CaptureSubtree(…), preserveIds=false )` | ✅ — **but an unregistered component silently does not survive duplication** |
| Delete + undo | `SceneCommands.cpp:296`, `:558-596` | ✅ |
| Prefabs | `PrefabAsset.cpp:67` / `:113`, `Runtime/Factory/PrefabFactory.cpp:55` | ✅ — same `EntitySerializer` path (`SceneCommands.hpp:26`) |
| Component-removed cascades | per-entry and manual (e.g. Terrain drags its `MaterialComponent`, `ComponentEditorRegistrations.cpp:525-531`) | ❌ manual if you own another component |
| Default/starter scene | `EditorLayer::BuildStarterScene`, `EditorLayer.cpp:1895` and `:433-439`, `:2023`, `:2268` | ❌ manual |
| Outliner icon / primary name | `ScenePropertiesPanel.cpp:31-68`, `SceneHierarchyPanel.cpp:205-220` — `if`-chains | ❌ manual, optional |
| Lua exposure | `Scripting/ReflectionBindings.cpp:55-64` (`kReflectedComponents[]`) | ❌ manual, and requires the `.Data` shape |

**There is exactly one serialization path.** Undo, duplicate, delete, prefabs and scene files all funnel
through `EntitySerializer` + `ComponentRegistry` — no second `CopyComponentIfExists`-style clone exists
anywhere (grep found none). This is the single most valuable fact in this section.

### 2.8 The checklist

**Required**

1. **`Desert/Desert/Source/Engine/ECS/Components.hpp`** — add `struct VolumetricCloudData { REFLECT() … };`
   plus `struct VolumetricCloudComponent { VolumetricCloudData Data; bool RequestX = false; };`
   Mark distances `Length`; use `EditCondition` for dependent rows; leave transient flags without
   `PROPERTY`.
2. **Build `Desert` once** — `DesertHeaderTool` regenerates
   `Desert/Desert/Source/Engine/Generated/Reflection.gen.cpp`. Never hand-edit it. **Commit the diff.**
3. **`Desert/Desert/Source/Engine/Core/Serialize/ComponentRegistry.cpp`**, in `RegisterBuiltins()` (the
   reflected block ends `:1019`) — one line:
   `Register( MakeReflected<ECS::VolumetricCloudComponent, ECS::VolumetricCloudData>( "VolumetricCloud", "VolumetricCloudData", &ECS::VolumetricCloudComponent::Data ) );`
   ⚠️ Skipping this means no save/load **and** no duplicate **and** no undo.
   3b. Only if a **new asset type** is referenced: extend `MakeAssetResolver` (`ComponentRegistry.cpp:189+`)
   with `if ( type == "…Asset" )` branches in both `ToPath` and `FromPath`.
4. **`Editor/.../SceneProperties/ComponentEditorRegistrations.cpp:46-90`** — one line:
   `DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::VolumetricCloudComponent, Data, "VolumetricCloudData", "Volumetric Clouds" )`
   This gives Details UI, collapsed-header summary, multi-select, search filter, remove button **and** the
   Add-Component menu entry. For bespoke UI (preset row, preview strip) write a
   `ComponentWidgets/VolumetricCloudComponent.cpp` using `DESERT_REGISTER_CUSTOM_COMPONENT` — model on
   `ComponentWidgets/SkyboxComponent.cpp:82`, **not** on its dead `.hpp`.
5. **`Editor/.../PropertyEditor/ComponentWidgetRegistry.cpp:48-69`** — add the display-name keyword to
   `CategoryOf()` or the component lands in "Other".

**Required for runtime behaviour**

6. **`Desert/Desert/Source/Engine/ECS/System/VolumetricCloudECSSystem.hpp`** — header-only,
   `: public System`. `CanRunParallel() -> true` **only** if read-only and no cross-system writes.
7. Register it, in order, in `EditorLayer.cpp:615-633` and `RuntimeLayer.cpp:81-94` (these two must match);
   optionally `PreviewViewport.cpp:146`.

**Optional**

8. `ScenePropertiesPanel.cpp:31-68` and `SceneHierarchyPanel.cpp:205-220` — icon / primary-name chains.
9. `EditorLayer.cpp:1890-1900` — starter-scene seeding.
10. `Scripting/ReflectionBindings.cpp:55-64` — Lua binding (requires the `.Data` shape).
11. `Desert/Tests/Engine/<Name>/` — a unit test (see [§4.6](#46-tests)).

**Build**

12. Re-run `premake5 gmake2` for any **new `.cpp`** (see [§5](#5-hard-constraints)). New `.hpp`-only work
    needs no regeneration.

**Files you never touch**: `EntitySerializer.cpp`, `SceneSerializer.cpp`, `ComponentEditor.cpp`,
`SceneCommands.cpp`, `PrefabFactory.cpp`, and every `premake5.lua`.

---

## 3. The render architecture we must plug into

### 3.1 The render graph

**Phases** — `Desert/Desert/Source/Engine/Graphic/RenderPhase.hpp:12-36`, spaced by 100 so user phases fit
between:

```
None 0 · DepthPrePass 100 · Sky 200 · Geometry 300 · Outline 400 · Decals 500
Lighting 600 · Transparency 700 · PostProcess 800 · Overlay 900 · UI 1000 · Debug 1100
k_UserBase 10000
```

User phases register at runtime via `RenderPhaseRegistry::Register( name )`
(`RenderPhaseRegistry.hpp:29`, `.cpp:30-36`), which allocates from `k_UserBase` upward. Built-ins are
pre-registered in the registry constructor (`RenderPhaseRegistry.cpp:5-28`); declaration order seeds the
topological-sort tie-breaker (`RenderPhase.hpp:30-35`, `RenderGraphBuilder.cpp:133-143`).

**Builder** — `Graphic/RenderGraphBuilder.{hpp,cpp}`. `PassConfig` (`hpp:27-43`) is
`{ Name, Phase, ExecuteFunc, PipelineSpec, TargetFramebuffer, Dependencies, optional ClearColor,
CachedRenderPass }`. Passes are added with
`AddPass( name, phase, executeFunc, pipelineSpec, targetFramebuffer, dependencies, clearColor )`
(`hpp:49-54`, `cpp:32-62`). `Build()` (`cpp:77-87`) validates for cycles then `TopologicalSort()`
(`cpp:109-181`) runs Kahn's algorithm **over phases, not passes**, and caches one `RenderPass` object per
pass (`cpp:168-176`). Intra-phase ordering is therefore **not deterministic by declaration** — this is why
`MeshRenderer::UpdateCascades()` is hoisted out of the graph (`SceneRenderer.cpp:370-375`).

**Rebuild** — `SceneRenderer::RebuildRenderGraph()` (`SceneRenderer.cpp:1033-1052`): clears the builder,
calls `RegisterPasses` on **every** render system (`:1037-1040`), then declares the five phase edges:

```cpp
DepthPrePass → Geometry ; Sky → Geometry ; Geometry → Outline ;
Geometry → Lighting ; Lighting → PostProcess          (SceneRenderer.cpp:1042-1046)
```

It is called from `Init()` (`:201`), from `RegisterRenderSystem`/`UnregisterRenderSystem` (`:1024`,
`:1030`) and from `RegisterExternalPass`/`UnregisterExternalPass` (`:1012`, `:1018`).

**Adding a render system** — `RegisterSystem<T>( name, this, targetFramebuffer, m_RenderGraphBuilder )`
inside `SceneRenderer::Init()` (`SceneRenderer.hpp:331-336`; call sites `SceneRenderer.cpp:104-199`), then
`Initialize()`. Base class: `Graphic/Systems/RenderSystem.hpp:16-41` — `Initialize()`, `Shutdown()`,
`GetSystemFramebuffer()`, protected `m_SceneRenderer`, `m_TargetFramebuffer` (a **weak_ptr**),
`m_RenderGraphBuilder`. Interface: `Graphic/IRenderSystem.hpp:7-13` — a single
`RegisterPasses( RenderGraphBuilder& )`.
Failure policy in `Init()` is per system: core systems `DESERT_VERIFY(false)` on failure
(`:109-110`, `:113-114`), optional ones `LOG_WARN` and carry on (`:157-158`, `:168-169`, `:176-177`).
**A volumetric cloud system must be optional (`LOG_WARN`)**, not fatal.

⚠️ `SceneRenderer::RegisterRenderPass` (`SceneRenderer.hpp:192-193`), `GetFramebufferForPhase` (`:208`)
and `GetTexture` (`:209`) are **declared but never defined and never called** — calling them will not
link. Use `RegisterRenderSystem` or `RegisterExternalPass` instead.

**External passes** — `Graphic/ExternalRenderPass.hpp`. `ExternalPassSpecification` (`:36-48`) is
`{ Name, Phase (default Debug), Dependencies (default: after Geometry), PipelineSpecification, Execute }`;
`ExternalPassContext` (`:23-29`) hands the pass `Camera`, `Target`, **`Depth`** (the scene depth
attachment) and `ScenePlaying`. Adapted into the graph by `ExternalPassSystem`
(`SceneRenderer.cpp:964-998`). This is the editor's injection route; the engine's own clouds should be a
proper render system instead, but the context struct shows the shape of "a pass that needs depth".

### 3.2 The actual per-frame order — and where a volumetric pass can legally go

`SceneRenderer::OnUpdate` (`SceneRenderer.cpp:356-623`) is the truth. The graph is only one step in it:

| # | Step | Line | Notes |
|---|---|---|---|
| 1 | `EnsureProceduralEnvironment()` (sky IBL bake) | `:363-368` | outside any render pass; may `WaitDeviceIdle` |
| 2 | `UpdateCascades()` | `:370-375` | CSM matrices, once per frame |
| 3 | `ClearMainFramebuffer()` | `:377-380` | |
| 4 | `CullGrassInFrame()` — **compute** | `:382-387` | outside any render pass |
| 5 | `SimulateInFrame()` — **compute** (particles) | `:389-394` | outside any render pass |
| 6 | **`ExecuteRenderGraph()`** | `:396-399` | Sky → Geometry → Outline → … , **skipping Debug/Transparency/UI** (`:1077-1079`) |
| 7 | Deferred block (only if `RenderPath == Deferred`) | `:403-528` | `RenderGBufferManual` `:408`; **`CopyDepthImage` G-buffer → target `:415-420`**; SSAO `:439-446`; RSM/GI `:451-472`; **`DeferredLightingRenderer::Execute` (the composite) `:493-496`**; generic + skinned forward-over-composite `:500`,`:504`; scene-colour copy `:509-514`; SSR `:519-525`; glass `:527` |
| 8 | **`ExecuteTransparency()`** | `:534-537` | LOAD overlay, after the composite — see below |
| 9 | Overdraw debug | `:541-545` | |
| 10 | `ExecuteDebugOverlay()` | `:550-553` | LOAD overlay |
| 11 | Backdrop blur (UI glass) | `:558-564` | |
| 12 | `ExecuteUI()` | `:568-571` | LOAD overlay |
| 13 | Jump-Flood outline | `:575-584` | |
| 14 | Auto-exposure | `:586-594` | |
| 15 | Bloom | `:597-601` | |
| 16 | Tonemap | `:603-606` | applies exposure/gamma; adds bloom |
| 17 | FXAA **or** SMAA | `:608-617` | |
| 18 | `CompositeRenderPass()` | `:619-622` | ⚠️ **body is commented out** — `:802-810` |

**Why Transparency runs after the graph** — `ExecuteTransparency()` (`SceneRenderer.cpp:1140-1175`), with
the reasoning stated twice (header `SceneRenderer.hpp:241-245`, implementation `:1146-1151`): in the
Deferred path the lighting composite runs **after** the graph, so a Transparency pass recorded *inside*
the graph gets painted over wherever geometry exists — visible against the sky, gone against the ground.
That was the particle "top-down" bug. All three deferred overlays (`Debug`, `Transparency`, `UI`) are
skipped in the main loop (`:1077-1079`) and replayed later with `BeginRenderPass(pass, /*clearFrame=*/false)`
— a **LOAD** begin, so nothing wipes the colour or the depth the later overlays test against.

**Where a volumetric cloud pass can legally be inserted.** The constraint is that volumetric clouds need
**scene depth** (to stop the march at opaque geometry) and must be composited **before tonemap** (so they
are exposed and bloomed like the rest of the HDR scene).

* ❌ **`RenderPhase::Sky` (200), inside the graph — where the current clouds live.** At that point the
  target depth is empty; Geometry has not run. Fine for a flat layer painted at infinity, useless for a
  volume that must be occluded by terrain and buildings.
* ❌ **Any in-graph phase between Geometry and PostProcess, in the Deferred path.** The deferred composite
  runs *after* the whole graph (`:403-528`) and would paint over it. Exactly the particle bug.
* ✅ **`RenderPhase::Transparency` (700), which is deferred to `ExecuteTransparency()` at step 8.** This
  is the only in-graph phase that lands at the right moment in *both* paths:
  * In **Deferred**, the G-buffer depth has already been copied into the target depth at step 7
    (`:415-420`), the lighting composite has run, and SSR/glass are done.
  * In **Forward**, the graph itself wrote geometry into the target with depth.
  * It is still **before** bloom (15) and tonemap (16), so clouds get exposure and glow for free.
  * `ExecuteTransparency` uses a LOAD begin, so the cloud pass composites over the finished scene.
  * ⚠️ The existing particle pipelines set **depth test off** on purpose (`SceneRenderer.cpp:1150-1151`).
    A cloud pass must set its own pipeline state; the phase does not force anything.
* ✅ **A dedicated step between 7 and 8**, i.e. a new `SceneRenderer::ExecuteVolumetricClouds()` called
  explicitly in `OnUpdate`. Warranted if the pass needs a compute dispatch (which must be outside any
  render pass) plus a composite draw, or its own half-resolution target and reprojection history. The
  precedent for "a system that needs an explicit call outside the graph" is everywhere in `OnUpdate`
  (steps 1, 2, 4, 5, 7, 11, 13-17).

**Recommendation:** register a `VolumetricCloudRenderer : RenderSystem` that (a) exposes a
`SimulateInFrame`-style method for its compute work, called near step 5, and (b) registers its composite
draw in `RenderPhase::Transparency` so `ExecuteTransparency` places it correctly with no new plumbing.
If a dedicated stage is needed later, promoting it to its own `Execute*` call is a one-line change in
`OnUpdate`.

### 3.3 The depth problem — read this before designing the pass

**Nothing in the engine samples a depth attachment as a texture today.** Verified: `GetDepthAttachmentImage()`
(`Graphic/Framebuffer.hpp:108`) has exactly three call sites — `SceneRenderer.cpp:418-419` (the depth
*copy*) and `:986-988` (the external-pass context) — and no shader under `Editor/Resources/Shaders/`
declares a depth sampler.

The depth image is *nearly* ready to be sampled, and the gap is precise:
* It **is** created with `Properties = Sample` (`API/Vulkan/VulkanFramebuffer.cpp:223-234`), so it has
  `VK_IMAGE_USAGE_SAMPLED_BIT` and a sampler, with aspect `DEPTH` (`VulkanImage.cpp:240-241`).
* But its **tracked layout is `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`**
  (`VulkanImage.cpp:17-22`), and `ComputePipeline::SetInput` binds that tracked layout **verbatim**
  (`API/Vulkan/VulkanPipelineCompute.cpp:91-100`). Binding it straight into a compute or fragment shader
  therefore presents a depth-attachment layout to a `COMBINED_IMAGE_SAMPLER` — a validation error.
* And the fix is not one line: `VulkanImage2D::TransitionLayout`'s subresource range **hardcodes
  `VK_IMAGE_ASPECT_COLOR_BIT`** (`VulkanImage.cpp:443-456`, aspect at `:448`), as does the
  `ComputeImageBeginWrite`/`EndWrite` pair. There is **no depth-aware transition helper**.

**This is the single biggest hidden gap for a depth-aware cloud pass.** It is a contained fix (a
depth-aspect transition helper), but it must be budgeted, not discovered.

Instead, the deferred path carries **world position in `GBufferC` (RGBA32F)** and reads *that*.
`SceneRenderer::Init` (`:74-75`) with the rationale at `:64-67`: "*so the lighting pass gets point/spot-light
distances directly (bulletproof vs depth reconstruction, which is error-prone under the GL-on-Vulkan depth
conventions)*". SSAO consumes it: `Editor/Resources/Shaders/Programs/Deferred/SSAO.shader:30-31` —
`Uniform(1) sampler2D u_GBufferPos; Uniform(2) sampler2D u_GBufferNormal;`.

Consequences for volumetric clouds:

* **In the Deferred path** the ray-stop distance is available for free: sample `GBufferC` (world position)
  from `SceneRenderer::GetGBuffer()` (`SceneRenderer.hpp:167-170`, public) and take
  `length(worldPos - cameraPos)`. This is the established, working pattern.
* **In the Forward path there is no world-position buffer and no sampled depth.** Clouds either cannot be
  occluded by geometry in Forward, or new infrastructure is needed (a depth-sampling path or a forward
  world-position/depth target). **This must be an explicit product decision, not a discovery in week 3.**
  `SceneSettings.RenderingPath` defaults to Forward (`Core/SceneSettings.hpp:42-46`) and
  `SceneRenderer::m_RenderPath` is initialised to Forward (`SceneRenderer.hpp:297`), though shipped scenes
  save `"RenderingPath":1` (Deferred).
* Reading the target's own depth attachment while that framebuffer is bound is a read/write hazard. The
  engine's answer to exactly this problem elsewhere is a **copy**: `CopyRenderer` snapshots scene colour
  for glass/SSR (`SceneRenderer.cpp:509-514`) and `Renderer::CopyDepthImage( Image2D* src, Image2D* dst )`
  exists (`Graphic/Renderer.hpp:79`). Either copy, or sample the G-buffer, or draw into a separate target.

### 3.4 Compute passes

Public API — `Desert/Desert/Source/Engine/Graphic/Renderer.hpp`:

```
DispatchComputeInFrame( pipeline, gx, gy, gz )     :65-66
DispatchComputeCull   ( pipeline, gx, gy, gz )     :70-71
ComputeImageBeginWrite( Image2D* ) / ComputeImageEndWrite( Image2D* )   :74-75
CopyDepthImage( Image2D* src, Image2D* dst )       :79
```

Both dispatch entry points record bind + a fresh ring descriptor set + dispatch, then issue a **global
`VkMemoryBarrier`** — they differ only in the destination stage mask
(`Graphic/API/Vulkan/VulkanRenderer.cpp:522-540` vs `:542-560`):

* `DispatchComputeCull`: `COMPUTE_SHADER → VERTEX_SHADER | DRAW_INDIRECT`, access
  `SHADER_WRITE → SHADER_READ | INDIRECT_COMMAND_READ` (`:533-539`). Use when the vertex stage or an
  indirect draw reads the result.
* `DispatchComputeInFrame`: `COMPUTE_SHADER → …` with `SHADER_WRITE → SHADER_READ | SHADER_WRITE`
  (`:555-557`). Use for compute→compute and compute→fragment-sample chains.

⚠️ Both are **coarse global memory barriers**, not per-resource image barriers, and neither performs an
image **layout transition**. That is why compute-writes-to-image goes through the separate
`ComputeImageBeginWrite`/`EndWrite` pair (`API/Vulkan/VulkanRenderer.cpp:563-584`, `:586-598`), which
transitions to `GENERAL` and back. There is **no automatic barrier or resource-state tracking for compute
anywhere** — every barrier in the engine is hardcoded at one of these call sites.

**Queues.** A dedicated compute queue family *is* selected when the device offers a compute-only family
(`API/Vulkan/VulkanDevice.cpp:458-470`, fallbacks `:489-493`, `:511-512` with an explicit MoltenVK note at
`:508-510`); the queue is created (`:209-215`) and retrieved (`:393-394`). **But there is no async
compute**: the compute queue is used only by the blocking immediate `Dispatch()` path, while every
in-frame dispatch is recorded into the frame's **graphics** command buffer
(`VulkanRenderer.cpp:527-531`, `:547-551`). No semaphores, no queue-ownership transfers, no overlap.
In-frame dispatches must be issued **outside** an open render pass.

**Descriptor sets for compute are all set 0** (`VulkanPipelineCompute.cpp:84`, `:98`, `:110`). The
in-frame path consumes one set from a **ring** (`EnsureInFrameRing`, `:155-210`; cursor at `:220-221`) so
several dispatches in one command buffer do not alias each other's descriptors. `descriptorCount` is
always 1 (`VulkanShader.cpp:204`) — no descriptor arrays, no bindless.

**Pipeline API** — `Graphic/Pipeline.hpp:157-179`, a UE-style fluent binder:

```cpp
SetInput( binding, Image* )                       // sampled input          :165
SetOutput( binding, Image*, mip = 0 )             // writable storage image :167
SetStorageBuffer( binding, StorageBuffer* )       // read-write SSBO        :169
SetPushConstants( const void*, size )             //                        :171
Dispatch( gx, gy, gz )                            // IMMEDIATE submit       :173
```

⚠️ `Dispatch()` is documented as "*Record + submit one **immediate** compute dispatch*" (`:172`) — it is
**not** the in-frame path, and it **blocks**: it takes a compute-pool command buffer, transitions all
bound outputs to `GENERAL` and back, then flushes and waits on a fence with `UINT64_MAX`
(`API/Vulkan/VulkanPipelineCompute.cpp:129-153`; `CommandBufferAllocator.cpp:10-38`, `:149-153`).
Use it only for one-shot work at a frame boundary (the IBL bake does). For per-frame work use
`Renderer::DispatchCompute*InFrame`.

⚠️ `ComputePipeline::Create()` does **not** build the pipeline — `Invalidate()` must be called manually
afterwards (`VulkanPipelineCompute.cpp:226-272`: descriptor layouts from reflection `:232-238`, one
`VK_SHADER_STAGE_COMPUTE_BIT` push-constant range `:242-253`, `vkCreateComputePipelines` against the
disk-persisted pipeline cache `:259-267`, then `InitializeDefaults()` `:271`). Every call site uses the
same two-line idiom — `BloomRenderer.cpp:97-98`, `ParticleRenderer.cpp:67-68`, `ComputeImages.cpp:78-79`.

**Worked example — the immediate path (IBL bake):**
`Graphic/ComputeImages.cpp:35-87` (`BakeProceduralPanorama`). Fetch shader by name (`:39`, via
`ShaderService::GetByName`), create an `Image2D` with `Properties = Storage | Sample` (`:43-52`), fill a
128-byte push struct that mirrors the shader block (`:56-76`), `ComputePipeline::Create({ .Shader, .DebugName })`
+ `Invalidate()` (`:78-79`), then `SetOutput/SetPushConstants/Dispatch` (`:81-84`). Work-group size
`kWorkGroupSize = 32` (`:14`) matched to `LocalSize(32,32,1)` in the shader.

**Worked example — the per-frame path (GPU particles):**
`Graphic/Systems/Scene/Particles/ParticleRenderer.{hpp,cpp}`.
* Persistent per-emitter GPU state cached by entity id — `EmitterGpu { StorageBuffer Particles; StorageBuffer Counter; … }`
  (`hpp:62-69`); created with `StorageBuffer::Create( "ParticleState", cap*stride, 1, /*persistent=*/true )`
  (`cpp:119-121`) and zero-initialised (`:124-126`).
* Push-constant struct `SimPush` (`hpp:49-60`), 8×`vec4`/`uvec4` = 128 bytes, mirroring the shader block.
* CPU snapshot in `PrepareFrame(scene)` called from `BeginScene` (`SceneRenderer.cpp:313`).
* `SimulateInFrame()` (`cpp:203-221`): per emitter `SetStorageBuffer(0/1)`, `SetPushConstants`, then
  `renderer.DispatchComputeCull( pipeline, groups, 1, 1 )` — the comment at `:217-218` explains the barrier
  choice. Called from `OnUpdate` at `SceneRenderer.cpp:389-394`, outside any render pass.
* `RegisterPasses` adds the billboard draw in `RenderPhase::Transparency` (`cpp:229-230`).
* ⚠️ `cpp:116-118` records a real trap: the *explicit* binding argument to `SetStorageBuffer` is what
  compute uses, **not** the buffer's own binding — mismatching it aliased the camera UB at binding 0 and
  tripped `VUID-VkWriteDescriptorSet-descriptorType-00319`.

**The template to copy** for a per-frame compute pass that writes a storage image is
`Graphic/Systems/Scene/PostProcessing/BloomRenderer.cpp`: create a `Storage | Sample` `Image2D`
(`:71-82`), `ComputePipeline::Create` + `Invalidate` (`:97-98`), then
`ComputeImageBeginWrite` → N × (`SetInput`/`SetOutput`/`SetPushConstants` + `DispatchComputeInFrame`) →
`ComputeImageEndWrite` (`:134-136`, dispatches `:150-154`, `:166-170`), driven from an
`Execute()` called imperatively in `SceneRenderer::OnUpdate` (`SceneRenderer.cpp:596-601`).
Note Bloom keeps the whole mip chain in `GENERAL` for the duration (`:134-136`) to sidestep the per-mip
transition caveat in `VulkanImage2D::TransitionLayout` (`VulkanImage.cpp:438-439`).

Descriptor pool sizing for compute: `Graphic/API/Vulkan/VulkanPipelineCompute.cpp:166-193` — one pool per
pipeline, sized from reflection counts × `kInFrameRingSize`.

### 3.5 Images and textures — **3D textures do not exist**

**Definitive answer: the engine has no 3D image support of any kind. Storage images (2D and cube) are
fully supported.**

`Desert/Desert/Source/Engine/Graphic/Image.hpp` declares exactly three classes: `Image` (`:15`),
`Image2D` (`:46`), `ImageCube` (`:70`). There is no `Image3D`, no `Texture3D`.

`Desert/Desert/Source/Engine/Core/Formats/ImageFormat.hpp` — the entire specification surface:
* `enum class ImageFormat` (`:17-25`): **`RGBA8F`, `RGBA32F`, `BGRA8F`, `DEPTH24STENCIL8`, `DEPTH32F`.**
  That is all five. **No `R8`, no `R16F`, no `RGBA16F`, no `R8G8B8A8_UNORM` variants.**
* `enum ImageProperties` (`:27-31`): `Storage = 0x1`, `Sample = 0x2`.
* `struct Image2DSpecification` (`:74-91`): `Tag, Width, Height, Format, Mips, Samples, Data, Usage,
  Properties, GenerateMips`. **No `Depth` field.**
* `struct ImageCubeSpecification` (`:93-102`): same minus MSAA/mips-generation.

Search results confirming the absence:
* `VK_IMAGE_TYPE_3D` / `VK_IMAGE_VIEW_TYPE_3D` appear **only inside commented-out lines** of the vendored
  `Graphic/API/Vulkan/VulkanUtils/lightweightvk/VulkanClasses.cpp` (`:606`, `:4283-4284`, `:4579`, `:6071`)
  — dead third-party code, not compiled behaviour.
* `Desert/Desert/Source/Engine/ShaderResources/` has `UniformImage2D.hpp` and `UniformImageCube.hpp` —
  no 3D equivalent.

Two further limits worth knowing before sizing a volume:
* `Image::GetBytesPerPixel` handles **only** `RGBA8F` and `RGBA32F` and **returns 0 for everything else**
  (`Graphic/Image.cpp:91-104`); `CalculateImageSize(w, h, fmt)` is `w*h*bpp` with **no depth parameter**
  (`:106-110`).
* `Utils::CreateSampler` (`VulkanImage.cpp:24-60`) hardcodes
  `addressModeU/V/W = VK_SAMPLER_ADDRESS_MODE_REPEAT` (`:49-51`) — convenient for tiling noise, but there
  is no way to request `CLAMP_TO_EDGE` without adding a parameter. Note also that filter and anisotropy
  are driven by the **global Scene Settings texture filter** via `RenderConfig` (`:27-42`), so a noise
  volume would inherit whatever the user picked, and a filter change recreates all samplers
  (`SceneRenderer.cpp:330-337`).
* Cubes are stored as a **4×3 cross**: `ImageCubeSpecification::Width/Height` are the cross dimensions and
  the face size is `Width / 4` (`VulkanImage.cpp:488`, `VulkanImage.hpp:117-118`).

Storage images, by contrast, work end to end:
* `Graphic/API/Vulkan/VulkanImage.cpp:227` and `:512`:
  `if ( m_Specification.Properties & Core::Formats::Storage ) info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;`
* Reflected from SPIR-V into `ShaderDescriptorSets[set].StorageImage2DSamplers`
  (`Graphic/API/Vulkan/VulkanShader.cpp:165-174`) and turned into `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`
  bindings (`:210`, `:91` of `CreateDescriptorsLayout`).
* Device support is gated on `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` (`Graphic/API/Vulkan/VulkanDevice.cpp:327`).
* `BakeProceduralSky.shader:16` is a live example: `layout(binding = 0, rgba32f) restrict writeonly uniform image2D`.

**Cubemaps and the compute→image pipeline** (the closest existing analogue to what clouds need) are in
`Graphic/ComputeImages.cpp` + `Graphic/Environment/SceneEnvironment.{hpp,cpp}`:
`BakeProceduralPanorama` (2D storage image) → `ProccessForImageCube` (`:89+`, panorama→cube) →
`CreateDiffuseIrradiance` → `ProccessForImageCubeMips` (GGX prefilter, per-mip `SetOutput(…, mip)`).
`Environment { RadianceMap, IrradianceMap, PreFilteredMap }` (`SceneEnvironment.hpp:12-23`), all
`Runtime::ImageHandle`s owned by the `ImageService` and released via `Unregister`
(`SkyboxRenderer.cpp:100-107`).

#### What is missing for volumetric clouds, precisely

| Need | Status | What has to be built |
|---|---|---|
| 3D noise volume (Perlin-Worley shape + detail) | **absent** | `Core/Formats/Image3DSpecification` (add `Depth`), `Graphic::Image3D` + `VulkanImage3D` (`VK_IMAGE_TYPE_3D`, `VK_IMAGE_VIEW_TYPE_3D`), a `MipMap3DGenerator` or an explicit no-mips path, `ImageService`/`ResourceRegistry` registration, `ComputePipeline::SetOutput` accepting it |
| `sampler3D` in a shader | **absent AND actively mis-bound** | `VulkanShader.cpp:125-131` classifies sampled images by a **name heuristic** — `"Env"` or `"Cube"` in the resource name → `ImageCubeSamplers`, else → `Image2DSamplers`. A `sampler3D` would be silently registered as a 2D combined image sampler. Needs a real type check on `spirv_cross`' `type.image.dim`. |
| `image3D` storage output (compute-generated noise) | **absent** | `VulkanShader.cpp:165-174` funnels every `storage_images` resource into `StorageImage2DSamplers` unconditionally. Same fix needed. |
| Single-channel or half-float formats | **absent** | Only `RGBA8F` / `RGBA32F` are usable. A 128³ RGBA32F volume is 32 MB; RGBA8F is 8 MB. An `R8`/`R16F` path would cut that 4×. |
| Depth-aware compositing | **partly present** | Sample `GBufferC` world position in Deferred; nothing exists for Forward (§3.3). |

**Viable workaround if 3D textures are out of scope for v1:** generate the noise **analytically in the
shader** (as `Clouds.glslh` already does in 2D), or pack a 3D volume into a **2D tile atlas** (a
`Z`-sliced sheet) — which needs no new engine plumbing at all. This is the lowest-risk path to a first
working volumetric pass, and the 3D-texture work can follow as an optimisation.

### 3.6 The shader system

**Where shaders live**: `Editor/Resources/Shaders/` — the root is the constant
`SHADERDIR_PATH = "Resources/Shaders/"` (`Desert/Common/Source/Common/Core/Constants.hpp:18`).
`Programs/<Dir>/<Name>.shader` for programs; `Common/*.glslh` for shared includes.

**Discovery is automatic — there is no manifest.** `AssetPreloader::PreloadShaders()`
(`Desert/Desert/Source/Engine/Assets/AssetPreloader.cpp:177-189`) walks `SHADERDIR_PATH` with a
`recursive_directory_iterator` (`:59`) for `SUPPORTED_SHADERS_EXTENSIONS = { ".shader" }` (`:21`) and
registers every hit with `ShaderService::Register` (`:186`).
`ShaderService::Register` (`Runtime/Services/Shader/ShaderService.cpp:6-26`) maps `shader->GetName()` →
handle (`:15`) and additionally registers every named DSL pass as `"<Shader>/<Pass>"` (`:17-23`).
`GetByName` (`:28-42`) resolves both.

⚠️ **`Shader::GetName()` is derived from the FILE STEM, not from the `Shader "..."` string inside the
file** (`API/Vulkan/VulkanShader.cpp:31-35`). `GetByName("ProceduralSky")` resolves the *file*
`ProceduralSky.shader`. Every existing file keeps the two equal — do the same, or the lookup name and the
declared name will silently diverge.

⚠️ Ordering: `PreloadShaders` must run before `PreloadSkyboxes` (`AssetPreloader.cpp:172-174`).

**So: drop `VolumetricClouds.shader` anywhere under `Editor/Resources/Shaders/` and
`ShaderService::GetByName("VolumetricClouds")` works. No registry edit, no premake edit.**

**The DSL** — `Shader "Name" { Domain … Properties … State … Vertex{} Fragment{} Compute{} Pass "X"{} }`
(`.claude/skills/desert-engine-dev/SKILL.md:161-164`). Declaration syntax, from live files:
* `In(N) type name;` / `Out(N) type name;` — `ProceduralSky.shader:16-17`
* `Uniform(N) BlockName { … };` — `ProceduralSky.shader:19-33`
* `Uniform(N) sampler2D name;` — `Deferred/SSAO.shader:30-31`
* `PushConstant Name { … };` — `Compute/BakeProceduralSky.shader:18-28`
* `LocalSize(x, y, z);` — `Compute/BakeProceduralSky.shader:30`
* raw `layout(binding = N, rgba32f) restrict writeonly uniform image2D …;` is also accepted —
  `Compute/BakeProceduralSky.shader:16`
* `#include <Common/Foo.glslh>` — `ProceduralSky.shader:13-14`

**The sugar table** — `Core/ShaderCompiler/DShader/DShaderParser.cpp:806-846`. Full spec in the header
comment at `DShaderParser.hpp:3-51`.

| DSL | GLSL |
|---|---|
| `In(n) T x;` / `Out(n) T x;` | `layout(location = n) in/out T x;` |
| `Uniform(n) …` / `Uniform(s, n) …` | `layout(binding = n) uniform …` / `layout(set = s, binding = n) …` |
| `Buffer(n) { … }` | `layout(std430, binding = n) buffer { … }` |
| `ReadBuffer(n)` / `WriteBuffer(n)` | as above `+ readonly` / `+ writeonly` |
| `LocalSize(x, y, z);` | `layout(local_size_x=…, local_size_y=…, local_size_z=…) in;` |
| `PushConstant Name { … };` | `layout(push_constant) uniform Name { … };` |

Paren-less forms auto-allocate the lowest free slot **per stage** across three independent spaces
(in-locations / out-locations / bindings) — `DShaderParser.cpp:818-826`, impl `:855-905`. ⚠️ Anything
shared across stages or bound by a **fixed number from C++** must keep an explicit `(n)` (`:822-826`).
⚠️ **Storage-image format qualifiers are deliberately NOT sugared** (`:828-831`) — write them raw:
`layout(binding = N, rgba16f) uniform image3D u_CloudVolume;`. Same for tessellation layout.

**Compilation & caching**: `.shader` → SPIR-V **at runtime via shaderc**
(`Core/ShaderCompiler/ShaderCompiler.cpp:134-184`), `shaderc_env_version_vulkan_1_1`, with
`SetWarningsAsErrors()` (`:161-162`) and debug info in Debug (`:164-166`). Supported stages: Vertex,
TessControl, TessEvaluation, Fragment, Compute (`:20-33`) — **no geometry, no mesh/task, no raytracing**.

The **content-addressed cache** lives in `Cooked/ShaderCache/<key:016x>.spv` (`:97-101`;
`COOKED_PATH` at `Common/Core/Constants.hpp:37`). ✅ **Resolved:** the key is FNV-1a over
`"vulkan1.1|v1"` (+ `"|debuginfo"` in Debug) + stage + the assembled source **+ recursively the content
of every `#include`d file** (`:141-152`, hashing helper `:55-95`). So editing a `.glslh` correctly
invalidates every shader that includes it — the `SKILL.md:165-167` warning is already satisfied for
includes. (It would *not* cover an external input like a generated define; keep that in mind.)

**CI lints every shader**: `.github/workflows/ci.yml:121-122` runs
`./build/Bin/Release/DShaderTool Editor/Resources/Shaders` — a broken `.shader` fails the pipeline, not
the editor at runtime. Tools live in `Tools/DShaderTool/`; the parser has its own test target
(`Desert/Tests/Engine/DShaderParser/`).

**Reflection** — `Graphic/API/Vulkan/VulkanShader.cpp`, `Reflect()` at ~`:100-195`:
* Uniform buffers with full field layout (name/offset/size/array) — `:120-137`
* Sampled images, **classified by name heuristic** (`"Env"`/`"Cube"` → cube, else 2D) — `:139-151`
* Storage buffers — `:153-163`
* Storage images → always `StorageImage2DSamplers` — `:165-174`
* Push constants, **one block**, with the largest declared size across stages winning — `:176-195`
  (the comment at `:189-193` explains why: glslang strips unused members per stage)
* `CreateDescriptorsLayout()` (`:198-216`) builds one `VkDescriptorSetLayout` per reflected set;
  `spv::DecorationDescriptorSet` is honoured, so multiple sets already work — shaders simply do not
  declare `set = N` today, so everything lands in set 0 (`Docs/RENDERER_FRAME_STATE.md:32-35`).

Reflection value types: `Desert/Desert/Source/Engine/ShaderResources/ShaderReflectionTypes.hpp` —
`UniformBuffer` (`:22-30`), `StorageBuffer` (`:32-40`), `Image2DSampler` (`:42-49`),
`ImageCubeSampler` (`:51-58`), `PushConstantRange` (`:60-67`). **No 3D variants** (see §3.5).

**Buffer creation** — `ShaderResourcesManager( debugName, shader )` builds every UniformBuffer/
StorageBuffer/sampler the shader declares and exposes them by name: `GetUniformBuffer`,
`GetStorageBuffer`, `GetUniformImage2D`, `GetUniformImageCube`
(`ShaderResources/ShaderResourcesManager.hpp:44-47`). `UniformBuffer::Create` is **private to it**
(`Docs/RENDERER_FRAME_STATE.md:40-42`): *"There is currently no way to own a shader-declared buffer
outside a material."* Materials write by reflected name —
`Get<UniformBufferProperty>( "SkyUB" )->SetRawData(…)` (`MaterialProceduralSky.hpp:69-70`).

⚠️ **There is no `GetStorageImage` on `ShaderResourcesManager`.** Storage images are bindable only via
`ComputePipeline::SetOutput` or the material-backend fallback path.
⚠️ **The reflection-driven SSBO path hardcodes a 36-byte size** — documented at
`ShaderResources/StorageBuffer.hpp:19-22`. Anything real must call
`ShaderResources::StorageBuffer::Create( name, size, binding, persistent )` directly (`:29-30`), as the
particle system does (`ParticleRenderer.cpp:119-121`).

**Push constants** are the cheap per-pass channel: 128 bytes is the guaranteed minimum and both existing
examples sit exactly at it (`ComputeImages.cpp:56-57`, `ParticleRenderer.hpp:49-60`). Prefer push
constants for per-dispatch cloud parameters; use a UB only when the payload exceeds 128 bytes.

### 3.7 The per-frame / per-renderer-slot rule — `Docs/RENDERER_FRAME_STATE.md`

**The rule any new material or uniform must obey.**

The problem (`RENDERER_FRAME_STATE.md:3-27`): a `Material`'s parent is **one object per shader** — every
mesh drawn with `StaticMeshPBR` shares it. `UniformBufferProperty::SetRawData` writes at offset 0 of a
single mapped allocation and marks it dirty; the Vulkan descriptor set **references** that buffer, it does
not copy. So the value the GPU reads is *whatever was written last before the queue executed the frame* —
not what was set when the draw was recorded. Two `SceneRenderer`s in one frame (a scene view plus a
preview or a thumbnail) overwrite each other's camera, lights, shadow cascades and IBL.

**The mitigation that has landed — "solution B", an owner dimension** (`:62-76`):
* **B1** `EngineContext::GetActiveRendererSlot()`, claimed per `SceneRenderer` and published in
  `BeginScene` (`SceneRenderer.cpp:253`).
* **B2** descriptor sets allocated and bound per `(frame × slot)`.
* **B3** uniform buffers hold a copy per `(frame × slot)`; `SetData`/`MapMemory`/descriptor-info all
  resolve the recording slot in `VulkanUniformBuffer::CopyIndex`.
* **B4** storage buffers likewise — **except persistent ones**, which are deliberately shared because GPU
  simulation state must survive across frames *and* views (`:73-76`, `:84-86`).

**Slots are leased, not consumed** (`:78-82`): claimed in the ctor, released in the dtor
(`SceneRenderer.cpp:213-237`, `:240-247`). Overflow warns and folds onto slot 0 (`:227-230`).

**What this means for the cloud subsystem, concretely:**

1. Any new **uniform buffer** you create through a `Material` is automatically per-`(frame × slot)`. Good.
2. Any new **storage buffer** you mark `persistent = true` is **shared across renderers and frames**. If
   the cloud system keeps temporal-reprojection history or a persistent weather buffer in a persistent
   SSBO, the Details mesh preview and the asset thumbnail renderer will write into the same buffer as the
   main viewport. Either make it non-persistent (per-slot), or key it by renderer slot yourself, or
   guarantee only one renderer ever runs the cloud pass.
3. Any **image** you allocate per `SceneRenderer` (a half-res cloud target, a history buffer) is paid for
   **once per live renderer** — and the editor creates several. The engine's own answer to this is lazy
   allocation on first use: see `EnsureGIResources`/`EnsureSSRResources` (`SceneRenderer.cpp:644-746`) and
   the rationale at `:96-100` and `:314-318`. **Copy that pattern**: allocate cloud targets on first
   actual use, latch the failure, never retry per frame.
4. Do not add a second `SceneRenderer`. The document's own conclusion (`:108-110`) is that the Details
   live preview stays disabled until the frame-set split (solution A) lands.

---

## 4. Engine-wide conventions

There is **no `CLAUDE.md`** in this repo. The conventions document is
`.claude/skills/desert-engine-dev/SKILL.md` (247 lines), plus `Docs/`.

### 4.1 Units — 1 world unit = 1 centimetre

`Docs/UNITS.md:1-5`: *"Desert uses the Unreal convention: one world unit is one centimetre, everywhere.
There is no conversion layer."* Canonical values at `:7-15` — 1 m = 100, 1 km = 100000, gravity −981,
default primitive 100, camera near/far 10/100000.
Helpers: `Desert/Common/Source/Common/Core/Units.hpp` — `Common::Units::Metres()`, `Cm()`,
`FormatLength()`, `UnitsPerMetre = 100.0f` (`:24`). Use `Metres(x)` when the number reads better in metres.
Reflected distances **must** carry `PROPERTY( …, Length )` (`Docs/UNITS.md:28-39`) so the Details panel
drags them in 1 cm steps.
👉 For clouds: layer bottom ≈ `Common::Units::Metres( 1500.0f )`, thickness ≈ `Metres( 3000.0f )`, and the
raymarch far distance is in the hundreds of thousands of units. Writing `1.5f` for "1.5 km" is exactly the
bug that `Desert/Tests/Engine/ShadowCascades/shadow_cascades_test.cpp:1-8` was written to prevent.

### 4.2 Naming

`SKILL.md:112-113`: *"Namespaces mirror the tree: `Desert::ECS`, `Desert::Graphic::Render`,
`Desert::Core::Formats`, `Desert::Editor`, `Common`. Types & methods PascalCase, members `m_`-prefixed."*
Observed and consistent: `m_` members, `s_` file-statics, `k`-prefixed constants
(`kAddCategories`, `kRSMResolution`, `kWorkGroupSize`), file-local helpers in an anonymous namespace,
closing `} // namespace Xxx` comments, public reflected component fields bare PascalCase.
`.clang-tidy` does **not** configure `readability-identifier-naming` — naming is convention plus review.

### 4.3 Error handling

`Desert/Common/Source/Common/Core/ResultStr.hpp`:
`template<typename T> class ResultStr` (`:27`), `using BoolResultStr = ResultStr<bool>` (`:24`).
Factories `MakeError<T>(msg)` (`:130-134`), `MakeFormattedError<T>(fmt, …)` (`:136-140`),
`MakeSuccess(v)` (`:142-147`). Query with `IsSuccess()` (`:60`) / `explicit operator bool()` (`:94`).

`Desert/Common/Source/Common/Core/Core.hpp`:
* `#define NO_DISCARD [[nodiscard]]` — `:38`
* `#define BOOLSUCCESS Common::MakeSuccess( true );` — `:39` ⚠️ **the macro includes its own semicolon**
* `DESERT_VERIFY( cond, … )` — `:54-63`: logs, debug-breaks, then `std::abort()` **in all configs**
* `DESERT_VERIFY_WARN( cond, … )` — `:65-69`, warn only, ⚠️ **not** wrapped in `do/while`

Policy (`SKILL.md:117-120`): *"Errors: `Common::ResultStr<T>`, **not exceptions**, for anything that can
fail on data. Exceptions are for truly exceptional/programmer errors only."*
`RenderSystem::Initialize()` returns `Common::BoolResultStr` (`Systems/RenderSystem.hpp:27`) — a cloud
system must too, and `SceneRenderer::Init` must treat its failure as `LOG_WARN`, not `DESERT_VERIFY`.

### 4.4 Logging

`Desert/Common/Source/Common/Core/Logger.hpp` — namespace `Common::Logger`, spdlog + `engine_log.txt`
(`:10-19`). `LogDebug/LogInfo/LogWarn/LogError/LogCritical/LogTrace` at `:21-67`, `fmt`-style `{}`
placeholders. Macros at `:70-75`: `LOG_INFO(…)`, `LOG_WARN(…)`, `LOG_ERROR(…)`, … ⚠️ **each macro carries
its own trailing semicolon**. There is **no** `DESERT_LOG` macro.

### 4.5 Comment style

No document mandates it; the nearest normative statement is `SKILL.md:19-20` — *"read its neighbours —
match the surrounding altitude, naming, and error style."* The house voice, characterised from the code:

1. Comments **explain *why* and argue the design**, not what the line does — `SceneRenderer.cpp:207-210`,
   `Clouds.glslh:69-71`, `ProceduralSky.shader:85-89`.
2. **Tombstone comments** where code was deliberately removed — `SceneSettings.hpp:119-122`, `:205-211`.
3. **CAPITALISED emphasis** on the load-bearing word — `SkyboxComponent.cpp:79`, `SceneRenderer.hpp:242`.
4. **Section banners** — `// ── Title ── … ──` (`SkyboxComponent.cpp:108-109`) or
   `// --- Physics (Jolt) ---…---` (`Components.hpp:1527`).
5. **UE comparisons** as design justification; **commit SHAs cited as evidence** (`SKILL.md:79, 157, 237`);
   **dated verification claims** (`RENDERER_FRAME_STATE.md:28`).
6. Hand-wrapped to the 115-column limit.

Write cloud comments this way. A reviewer will notice if you do not.

### 4.6 Tests

**Framework: GoogleTest.** Each test is a standalone `ConsoleApp` with its own `main`.

Layout: `Desert/Tests/{Common,Editor,Engine}/<Name>/` — each leaf holds exactly
`<name>_test.cpp` + `premake5.lua`. The **directory name is the project name, target name and
`<Name>.make` name**.

A test's `premake5.lua` derives everything (`Desert/Tests/Engine/ShadowCascades/premake5.lua:1-2`):
```lua
local test_name  = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")
```
Aggregation is auto-discovery — `Desert/Tests/premake5.lua:5-9` globs `./**/premake5.lua`, so **a new test
directory needs zero edits elsewhere**.

Tests are **not generated by default locally**: `Desert/premake5.lua:6-11` gates `include "Tests/"` on
`_OPTIONS["with-tests"]` or the `CI` env var. Regenerate with `premake5 gmake2 --with-tests`.
Run with `./scripts/MacOS/RunTests.sh "$PWD" Debug`.

**The model for a testable unit** is `Desert/Tests/Engine/ShadowCascades/` — its `premake5.lua:11-12`
says: *"The unit under test is header-only pure math (`Engine/Graphic/ShadowCascades.hpp`): no renderer,
no GPU, nothing to link."*

👉 **What of the cloud system is unit-testable, given the GUI cannot run here (§5):**
* A header-only `Graphic/CloudMath.hpp` (or `ECS/System/SystemRules.hpp` additions): layer
  entry/exit intersection of a ray with the cloud slab, step-count derivation from distance, Beer–Lambert
  transmittance, the Henyey–Greenstein phase function, coverage→density remapping, wind offset from
  `WindEnv`. All pure math over world units — testable exactly like `ShadowCascades`.
* The **CPU→GPU packing struct** (push constants / UB): assert `sizeof` and `offsetof` against the
  documented layout so a shader/C++ divergence fails a test instead of corrupting the frame. Given
  `MaterialProceduralSky.hpp`'s hand-mirrored `SkyUBData`, this is worth doing.
* Serialization round-trip of the new component through `ComponentRegistry` — precedent:
  `Desert/Tests/Engine/ReflectionSerializer/`.
* Migration of an old `SkyboxComponent` payload into the new component (§6) — the highest-value test in
  the whole project, since it is the only correctness property that cannot be eyeballed.

### 4.7 The clang-format gate

Config: `.clang-format` — Allman braces, `IndentWidth: 4`, `ContinuationIndentWidth: 5`,
`ColumnLimit: 115`, `SpacesInParentheses: true` (hence `( x )` everywhere),
`AlignConsecutiveAssignments/Declarations: true`, `SortIncludes: false`, `PointerAlignment: Left`,
`NamespaceIndentation: All`, `AccessModifierOffset: -4`.

CI job `format` — `.github/workflows/ci.yml:25-38`, gating everything (`macos` `needs: [format, sanitizers]`
at `:85`; `windows` `needs: [format]` at `:154`). The check is **diff-based, changed lines only** —
`scripts/CI/CheckFormat.sh:22` runs `git clang-format --diff "$BASE" -- '*.cpp' '*.hpp'`, deliberately not
a full-tree gate (`ci.yml:23-24`).

**Verify locally with clang-format 18, not whatever is on `PATH`** — `Docs/README.md:17-19`:
```
/opt/homebrew/opt/llvm@18/bin/git-clang-format --binary /opt/homebrew/opt/llvm@18/bin/clang-format --diff <base>
```
⚠️ `git add` **new** files first — untracked files are skipped locally but checked by CI.
⚠️ The CI install step (`ci.yml:35`) is unpinned (`apt-get install clang-format`), so the exact CI version
is **unverified**; the documented 18 is the safe target.
⚠️ Known v18-vs-v22 divergences that make locally-clean code fail CI: parameter-less multi-line lambdas;
multi-variable declarations split across lines; aligned `const`-declaration blocks. For a stubborn block,
wrap it in exactly `// clang-format off` … `// clang-format on` — the directive must be that string
**alone**; trailing text after `off` makes clang-format ignore it.

### 4.8 Other conventions that will bite

* **Never render offscreen from `OnUIRender()`** — defer GPU work to `OnPreUpdate()`
  (`Docs/README.md:26-27`). A live cloud *render* preview in the Details panel hits this directly.
* **Adding a serialized field must not break old assets** — `rfl::json` with `rfl::DefaultIfMissing`;
  always default the field (`SKILL.md:121-123`, `:239-240`).
* **Never hand-edit `Engine/Generated/`** (`SKILL.md:124-126`).
* **No GLSL walls embedded in C++** — boilerplate goes in shared `.glslh` includes (`SKILL.md:127-129`).
* **No ad-hoc `std::thread`/`std::async`** — use `Common::JobSystem::Get()` (`SKILL.md:175-181`).
* **No new singletons** (`SKILL.md:232-233`).
* **`CanRunParallel() → true` on a system that writes shared state is a data race** (`SKILL.md:234-236`).
* **Vulkan lifetime discipline** — pipeline layouts, push-constant ranges and descriptor set layouts must
  outlive the pipelines referencing them; a dangling `VkPushConstantRange` cost real FPS once, commit
  `868b6b2` (`SKILL.md:155-157`, `:237`).
* **Optimizing without a profile is an anti-pattern** — Optick `DESERT_PROFILE_SCOPE` and the in-viewport
  Perf HUD exist; a volumetric pass is expensive enough that it must carry profile scopes from day one
  (`SKILL.md:185-190`, examples throughout `SceneRenderer.cpp`).
* **Hardcoding content into a mechanism is *the* red flag** (`SKILL.md:202-218`). Litmus test: *"Can a
  content author add the next variant without recompiling the engine?"* Relevant to cloud presets.
* **Tests green on Debug and Release** before calling anything done (`SKILL.md:130`).

---

## 5. Hard constraints

State these to every developer before they start.

1. **The GUI editor cannot be launched in this development environment.** There is no working Vulkan —
   `glfwVulkanSupported` fails. Nothing can be visually verified locally. Vulkan validation errors surface
   as `VulkanDebugCallback` warnings and must be pasted in by the user. Consequences:
   * Every claim about how the clouds *look* is unverifiable here. Do not write "verified" about visuals.
   * Correctness must be pushed into unit-testable pure functions wherever possible (§4.6).
   * Shader changes are validated only by `DShaderTool` parsing (§3.6) and by CI — not by rendering.

2. **Builds are `make -j8 <Target> config=debug` from the repo root.**
   Root `Makefile:3-5` defaults `config=debug`; `:11-55` validates `debug`/`release` and errors otherwise;
   available targets are listed in `PROJECTS` at `Makefile:57`. Both configurations must build — CI builds
   Debug (under ASan+UBSan) and Release, on macOS **and** Windows (`ci.yml:43-76`, `:83-117`, `:154+`).
   ⚠️ The currently committed `Makefile:57` was generated **without** `--with-tests`, so test targets are
   absent from it and the root `Result.make` / `ShadowCascades.make` files are stale leftovers. Regenerate
   with `--with-tests` before trying `make -j8 <TestName> config=debug`.

3. **`premake5 gmake2` must be re-run whenever a source file is added or removed.**
   The premake scripts use **globs** — `Desert/Desert/premake5.lua:19-29` (`"Source/Engine/**.cpp"`),
   `Editor/premake5.lua:9-14` (`"Source/**.cpp"`) — but globs are resolved **at generation time** and the
   generated makefiles list every object **explicitly**. Verified: `Desert.make:196`, `:369`, `:839` each
   name `SkyboxRenderer.o` individually. **A new `.cpp` is invisible to `make` until premake re-runs.**
   New headers need no regeneration. `scripts/MacOS/BuildMacOS.sh:36-37` runs `premake5 gmake2` on every
   invocation, so building through that script is safe; a bare `make` is not.
   Add `--with-tests` when adding a test project.

4. **`DesertHeaderTool` runs as a prebuild step on every `Desert` build** and rewrites
   `Desert/Desert/Source/Engine/Generated/Reflection.gen.cpp`, which **is committed**. Expect it in your
   diff; commit it; never hand-edit it.

5. **The engine supports exactly ONE directional light** — see [§7.3](#73-the-hard-limit-one-directional-light).

6. **No new third-party dependencies without explicit request** (`SKILL.md:66-71`). A volumetric cloud
   system is a classic place to reach for a noise library. Write the noise, or generate it — do not add a
   dependency.

---

## 6. Sky-settings migration

Scope addition: **all** procedural-sky settings move out of `SkyboxComponent` (and out of `SceneSettings`)
into the new component, not just clouds.

### 6.1 Every sky field and who reads it

`ECS::SkyboxComponent`, `Desert/Desert/Source/Engine/ECS/Components.hpp:1445-1507`, complete:

| # | Field | Type | Default | Line | Reflected as |
|---|---|---|---|---|---|
| 1 | `SkyboxHandle` | `Assets::AssetHandle` | — | 1451-1452 | `Asset<SkyboxAsset>`, **`Hidden`**, Category `Skybox` |
| 2 | `Intensity` | `float` | `1.0` | 1454-1455 | `Range(0,10)`, Category `Skybox` |
| 3 | `Procedural` | `bool` | `false` | 1458-1459 | Category `Skybox` |
| 4 | `SunIntensity` | `float` | `22.0` | 1460-1461 | `Range(1,50)` |
| 5 | `SunDiskRadius` | `float` | `0.02` | 1462-1463 | `Range(0.002,0.1)` — radians |
| 6 | `ZenithColor` | `glm::vec3` | `{0.08,0.26,0.70}` | 1467-1468 | `Color`, Category `Sky Color` |
| 7 | `HorizonColor` | `glm::vec3` | `{0.50,0.66,0.92}` | 1469-1470 | `Color` |
| 8 | `GroundColor` | `glm::vec3` | `{0.16,0.19,0.24}` | 1471-1472 | `Color` |
| 9 | `NightColor` | `glm::vec3` | `{0.010,0.020,0.050}` | 1473-1474 | `Color` |
| 10 | `SkyBrightness` | `float` | `1.0` | 1475-1476 | `Range(0,4)` |
| 11 | `HorizonFalloff` | `float` | `0.85` | 1477-1478 | `Range(0.1,2)` |
| 12 | `SunColor` | `glm::vec3` | `{1.00,0.96,0.88}` | 1480-1481 | `Color`, Category `Sun & Sky` |
| 13 | `SunGlow` | `float` | `1.0` | 1482-1483 | `Range(0,5)` |
| 14 | `SunsetColor` | `glm::vec3` | `{1.00,0.42,0.18}` | 1484-1485 | `Color` |
| 15 | `SunsetIntensity` | `float` | `1.0` | 1486-1487 | `Range(0,3)` |
| 16 | `StarIntensity` | `float` | `1.0` | 1488-1489 | `Range(0,5)` |
| 17 | `EnableClouds` | `bool` | `false` | 1492-1493 | Category `Clouds` |
| 18 | `CloudCoverage` | `float` | `0.5` | 1494-1495 | `Range(0,1)` |
| 19 | `CloudDensity` | `float` | `1.0` | 1496-1497 | `Range(0,2)` |
| 20 | `CloudTiling` | `float` | `1.5` | 1498-1499 | `Range(0.2,10)` |
| 21 | `CloudBrightness` | `float` | `1.0` | 1500-1501 | `Range(0,3)` |
| 22 | `CloudWindSpeed` | `float` | `8.0` | 1502-1503 | `Range(0,50)` |
| 23 | `RequestBake` | `bool` | `false` | 1505-1506 | **not reflected** — transient |

**Readers, per field group.** This is the "cannot simply be moved" analysis.

| Field group | Reader | Site |
|---|---|---|
| 1, 2 (HDR path) | `SkyboxECSSystem` → `SkyboxCommand` → `SkyboxRenderer::PrepareMaterial` → `MaterialSkybox` | `SkyboxECSSystem.hpp:81-88`; `SkyboxRenderer.cpp:66-78`, `:137-142` |
| 1 | `SkyboxService`/`AssetResolver` round-trip | `ComponentRegistry.cpp:197-201`, `:238-252`; editor `SkyboxComponent.cpp:92-106` |
| 3 (`Procedural`) | selects the pass mode | `SkyboxECSSystem.hpp:77-81`; `SkyboxRenderer.cpp:128`; editor `SkyboxComponent.cpp:111-118`, `:179` |
| 4, 5 | **(a)** screen sky pass, **(b)** IBL bake | `MaterialProceduralSky.hpp:57-58`; `SkyboxRenderer.cpp:89-90` → `EnvironmentManager::CreateProcedural` → `ComputeImages::BakeProceduralPanorama` (`ComputeImages.cpp:69-70`) |
| 6-16 (`SkySettings`) | **(a)** screen sky pass, **(b)** IBL bake, **(c)** editor sky ramp | `MaterialProceduralSky.hpp:62-67`; `ComputeImages.cpp:70-76`; `SkyboxComponent.cpp:33-75` |
| 6-16 | **(d)** the mesh-preview viewport, which builds `SkySettings` by hand | `PreviewViewport.cpp:151-178` |
| 6-16 | **(e)** the asset thumbnail renderer, likewise | `AssetThumbnailRenderer.cpp:68-97` |
| 17-22 (clouds) | **only** the screen sky pass — never the bake | `MaterialProceduralSky.hpp:58-60`; `Clouds.glslh` |
| 23 (`RequestBake`) | consumed+cleared by the ECS system; set from **five** places | consume `SkyboxECSSystem.hpp:52-53`; set `SkyboxComponent.cpp:217`, `EditorLayer.cpp:441`, `PreviewViewport.cpp:164`, `AssetThumbnailRenderer.cpp:85`, `PhotogrammetryPanel.cpp:641` |

**The load-bearing consequence:** fields 4-16 have **three independent consumers** (screen pass, IBL bake,
editor ramp) **and two bypass paths** (`PreviewViewport`, `AssetThumbnailRenderer`) that construct the
settings in C++ without touching the ECS at all. Moving them is not a rename — it is a five-site change,
and the two bypass sites are the ones most likely to be forgotten because they are in the editor, not the
engine.

### 6.2 `SceneSettings` has no sky fields left

Verified by reading `Desert/Desert/Source/Engine/Core/SceneSettings.hpp` end to end (221 lines). There are
**no** sky, cloud, sun or atmosphere properties. They were already moved, with tombstones:
* `:119-122` — *"skybox brightness now lives on the SkyboxComponent (entity) as `SkyboxComponent::Intensity`
  … Procedural sky lives there too. The old `EnvironmentMapIntensity` / `SkyboxLOD` scene-global knobs were
  dead (no render consumer) and removed … stale copies in old scene files are simply ignored on load."*
* `:209-211` — *"**Time of Day was removed**: the sun's single source of truth is the directional-light
  ENTITY."*

The only environment-ish fields that remain are `WindDirection`/`WindStrength`/`WindTurbulence`
(`:198-203`), which are deliberately scene-global (`:194-197`) and **must stay there** — one wind moves
grass, clouds, hair and cloth.

**So "move it out of SceneSettings" is a no-op.** The entire migration is out of `SkyboxComponent`.

### 6.3 How scenes store this today

**Format: JSON, extension `.desce`** — `Desert/Common/Source/Common/Core/Constants.hpp:73`
(`SCENE_EXTENSION = ".desce"`), directory `:31`/`:56`. Written with reflect-cpp:
`rfl::json::write( scene )` at `Core/Serialize/SceneSerializer.cpp:136`; read
`rfl::json::read<SceneSerialized>( json )` at `:141`. There are no `*.scene`/`*.dscene` files.

Files on disk: `Editor/Resources/Assets/Scenes/{Desert_Sandbox,Starter,CornellDemo,MainMenu}.desce` plus
`Scenes/Autosave/*.desce`.

Top-level schema — `SceneSerializer.cpp:23-30`:
```cpp
struct SceneSerialized {
    std::string                     SceneName;
    std::vector<Assets::EntityData> Entities;
    std::optional<rfl::Generic>     Settings;
    std::optional<int>              UnitVersion;
};
```
Entity schema — `Assets/Prefab/PrefabData.hpp:144-164`: `id`, `parent`, `PrefabPath`, `Tag`,
`Translation/Rotation/Scale`, then `rfl::ExtraFields<rfl::Generic> Components` (`:163`, rationale
`:158-162`) — so component payloads spread **flat** at the entity's top level.

Real excerpt, `Editor/Resources/Assets/Scenes/Desert_Sandbox.desce:1`:
```json
{"id":4576059197435775655,"Tag":"Sky","Translation":[0.0,0.0,0.0], …,
 "Skybox":{"SkyboxHandle":"","Intensity":1.0,"Procedural":true,"SunIntensity":22.0,
 "SunDiskRadius":0.0199…,"ZenithColor":[0.0799…,0.2599…,0.6999…], …,
 "EnableClouds":false,"CloudCoverage":0.5,"CloudDensity":1.0,"CloudTiling":1.5,
 "CloudBrightness":1.0,"CloudWindSpeed":8.0}}
```
Key facts: the payload key is **`"Skybox"`**; every field is written **flat, by its C++ member name**,
straight from reflection; `glm::vec3` is a 3-element JSON array; `AssetHandle` is a **path string**.

### 6.4 What a migration must do

**Behaviour on load, all verified:**

| Situation | Behaviour | Citation |
|---|---|---|
| Field present in the struct, missing in the file | **keeps the C++ default**, no error | `Reflection/ReflectionSerializer.cpp:138-140` — `if ( !found.has_value() ) continue;` |
| Field in the file, unknown to the struct | **silently ignored** (only `type.Fields` is iterated) | `ReflectionSerializer.cpp:136` |
| Component key in the file, not in the registry | **never even read** — the load loop iterates the registry | `EntitySerializer.cpp:91-96` |
| Component key missing | the component is simply **not added** | `EntitySerializer.cpp:91-96` guard |
| Whole file fails to parse | logged, **load aborted**, scene left as-is | `SceneSerializer.cpp:143-147` |

**Therefore: if the cloud/sky fields move to a new component key, every existing `.desce` silently loses
its sky.** The values sit in the file under `"Skybox"` and nothing reads them.

**Versioning.** There is exactly **one** version integer, and it is scoped to units:
`SceneSerializer.cpp:18-21` — `static constexpr int kUnitVersion = 1;` with the comment *"Bump this only
if the world unit changes again."* Declared `:29`, written `:101`, read `:256`. There is **no**
general-purpose scene schema version and no per-component version.
⚠️ Reusing `UnitVersion` would violate its stated contract. Add a second integer
(e.g. `SkyVersion` / `SchemaVersion`) or use a presence check.

**The precedent to copy — `MigrateMetresToUnits`** (`SceneSerializer.cpp:32-90`, triggered `:254-257`):
```cpp
if ( sceneData->UnitVersion.value_or( 0 ) < kUnitVersion )
    MigrateMetresToUnits( *m_Scene );
```
Shape of it: **version-gated, post-load, operating on the live ECS scene** (not on the JSON), touching
each affected component in turn (`:47-87`), logging what it did (`:88`), and made once-only by the
re-save stamping the new version (`:101`). Documented in `Docs/UNITS.md:41-48`.

**Two viable strategies for sky:**

* **A — presence-based (recommended).** Because unknown keys survive in `rfl::ExtraFields`, a post-load
  pass can detect "entity has `SkyboxComponent` but no `VolumetricCloudComponent`/`SkyComponent`" and
  populate the new component from the old fields. Requires keeping the old fields on `SkyboxComponent`
  for one release (deprecated, `Hidden`), or reading the raw `rfl::Generic` before the registry loop.
  Idempotent by construction; needs no new version integer.
* **B — version-gated.** Add a second version int to `SceneSerialized`, mirror `MigrateMetresToUnits`
  exactly. Cleaner story, but a new schema field is itself a one-way door.

**The rename trap.** There is **no field-alias / renamed-key mechanism anywhere** in the reflection
serializer. The project's own precedent is to accept the data loss: `ComponentRegistry.cpp:1010-1015` —
*"old scenes stored the HDR under key `SkyboxPath`; the reflected field is `SkyboxHandle`, so an old HDR
selection needs re-pick."* **If you rename sky fields while moving them, they silently reset to
defaults.** Keep the names identical across the move and the reflection serializer will read them
unchanged; rename in a *separate*, deliberate step with an explicit migration.

**Other migration precedents** (all "drop it and ignore stale keys"): `SceneSettings.hpp:119-122`,
`:209-211`; `ComponentRegistry.cpp:116` (single-script legacy format removed); `:869-871`, `:886-889`
(TextComponent font: handle in memory, path on disk, "backward-compatible with pre-handle saves").

**Also keep in sync when the component set changes:**
1. `Core/Serialize/ComponentRegistry.cpp:943-1019` — the serializer list.
2. `Scripting/ReflectionBindings.cpp:55-64` — `kReflectedComponents[]`, the Lua list (needs a `.Data` shape).
3. Editor: `ComponentEditorRegistrations.cpp`, `ComponentWidgetRegistry.cpp:48-69` (`CategoryOf`),
   `ScenePropertiesPanel.cpp:32-70`, `SceneHierarchyPanel.cpp:207,261`.

Undo, copy/paste, duplicate and prefabs need **no** changes — they all route through `EntitySerializer`
(`SceneCommands.hpp:26`; sites `SceneCommands.cpp:73`, `:111`, `:598-645`; `PrefabAsset.cpp:67`, `:113`;
`PrefabFactory.cpp:55`).

**Test it.** A migration is the one part of this rewrite whose correctness is fully checkable without a
GPU: load a fixture `.desce` carrying the old `"Skybox"` payload, run the migration, assert the new
component's fields. Model on `Desert/Tests/Engine/ReflectionSerializer/`.

---

## 7. The directional light and the "atmosphere sun"

Goal: UE-style **Atmosphere Sun Light** (bool) and **Atmosphere Sun Light Index** (which sun slot) on the
directional light, so the sky takes its sun from a designated light instead of "the first one found".

### 7.1 The component today

`Desert/Desert/Source/Engine/ECS/Components.hpp:382-398` — that is the **entire** component:

```cpp
    struct DirectionalLightData          // :382
    {
        REFLECT()                        // :384

        PROPERTY( DisplayName("Color"), Category("Light"), Color, Temperature )   // :386
        glm::vec3 Color = glm::vec3( 1.0f );                                      // :387

        PROPERTY( DisplayName("Intensity"), Category("Light"), Range(0.0f,10.0f), Summary, Units("x"),
                  Tooltip("Linear multiplier on the light colour. NOT a photometric unit …") )   // :389-391
        float Intensity = 1.0f;                                                   // :392
    };

    struct DirectionLightComponent       // :395
    {
        DirectionalLightData Data;       // :397
    };
```

Two fields. No direction, no shadow toggle, no "is sun" flag, no angular diameter.
Serialized under key `"DirectionLight"` (`ComponentRegistry.cpp:945-946`); on disk:
`"DirectionLight":{"Color":[1.0,1.0,1.0],"Intensity":1.0}`.

It uses the **`Data`-block shape**, so adding two fields is a two-line change to `DirectionalLightData`
plus a rebuild — serialization, Details UI and Lua all follow automatically ([§2](#2-adding-a-new-ecs-component-end-to-end)).

### 7.2 How the direction is derived

Confirmed: the direction is `TransformComponent::Translation`, interpreted as **the direction the light
TRAVELS** (sun → scene). It is normalized but its magnitude is deliberately preserved by the editor.

| Consumer | Code | Sign |
|---|---|---|
| **Collection (the single upload point)** — `Core/Scene.cpp:344-354` | `glm::vec4( glm::normalize( transform.Translation ), 0.0f )` | **+** travel |
| **Sky** — `ECS/System/SkyboxECSSystem.hpp:40` | `sunDir = -glm::normalize( t.Translation );` | **−** toward sun |
| **Shadow cascades** — `Graphic/Systems/Scene/Mesh/MeshRenderer.cpp:1267` | `setup.LightDirection = DirectionLights[0].Direction;` | **+** |
| **Deferred lighting** — `Graphic/SceneRenderer.cpp:422-428` | `lightDir = dl[0].Direction;` (fallback `(0,-1,0,0)`) | **+** |
| **Terrain / grass** — `Graphic/Systems/Scene/Terrain/TerrainRenderer.cpp:70-84`, used `:319`, `:401` | `outDir = dl[0].Direction;` | **+** |
| **RSM light-eye** — `MeshRenderer.cpp:1288-1292` | backs off against travel | **+** |
| **Editor light widget** — `ComponentEditorRegistrations.cpp:365-374` | `toSun = -travel / length` | **−** |
| **Editor sky ramp** — `ComponentWidgets/SkyboxComponent.cpp:194` | `toSun = -glm::normalize( travel )` | **−** |
| **Editor viewport gizmo** — `ViewportPanel/LightGizmoRenderer.cpp:191-196` | `towardSun = normalize(t); lightDir = -towardSun;` | ⚠️ **OPPOSITE** |

**Exactly one negation exists in the engine** (`SkyboxECSSystem.hpp:40`); everything downstream of it is
"toward sun", everything on the other path is "travels". The convention is documented in five places:
`ShadowCascades.hpp:39`, `MaterialDeferredLighting.hpp:45-46`, `MaterialProceduralSky.hpp:45`,
`ProceduralSkyCommand.hpp:12`, `Atmosphere.glslh:52`.

⚠️ **Bug worth fixing while in this area.** `LightGizmoRenderer.cpp:191-196` treats `Translation` as
*toward-sun* and negates it — inverted relative to `Scene.cpp:346-353` and every other reader. Its own
comment at `:172-174` claims "same convention as SkyboxECSSystem", which the code contradicts; `:186-189`
rationalises it as "the demo sun *sits* at (0.3, 0.9, 0.3)". The ambiguity is real in shipped data:
`Desert_Sandbox.desce` / `Starter.desce` author the Sun at `Translation = [0.351, 0.902, 0.251]` (positive
Y = sun *below* the horizon under the canonical convention) while `EditorLayer.cpp:436` and `:2272` author
`{-0.4f, -1.0f, -0.5f}` (correct travel direction). **The two shipped sandbox scenes therefore have their
sun the wrong way round.** Any sky rewrite that renders a believable sun will surface this immediately.

Further inconsistencies to be aware of:
* **Different degenerate-vector epsilons**: `Scene.cpp:349` uses `0.001f`; `SkyboxECSSystem.hpp:38` and
  `LightGizmoRenderer.cpp:192` use `1e-4f`.
* **Different "first light" selection**: `Scene.cpp:343-344` uses an entt **group**;
  `SkyboxECSSystem.hpp:34` uses a **view**. Visit order can differ, so with more than one directional
  light the sky and the lighting can pick different suns.
* **Three different fallbacks when there is no light**: sky `normalize(0.3,0.9,0.3)` toward-sun
  (`SkyboxECSSystem.hpp:32`); deferred `(0,-1,0)` travel (`SceneRenderer.cpp:422`); terrain
  `normalize(-0.4,-0.85,-0.35)` travel (`TerrainRenderer.cpp:72`); cascades simply bail
  (`MeshRenderer.cpp:1257-1258`).
* **`DirectionalLightData::Intensity` and `Color` never reach the sky.** Sky sun brightness is
  `SkyboxComponent::SunIntensity` (`Components.hpp:1461`) and sky sun tint is `SkyboxComponent::SunColor`
  (`:1481`) — completely separate numbers from the light's own. A UE-style atmosphere sun would normally
  unify these.

### 7.3 The hard limit: ONE directional light

`Desert/Desert/Source/Engine/Core/Scene.cpp:356-373`:

```cpp
        // The engine supports EXACTLY ONE directional light (DirectionLightsUB is a single
        // struct — a second payload overflows every PBR material's UB and aborts). Truncate
        // loudly instead of crashing; name the extras so the offending entity is findable.
        if ( sceneRendererInfo.DirLights.DirectionLights.size() > 1 )
        {
            …
            LOG_ERROR( "[Scene] {} directional lights collected ({}) — only ONE is supported; "
                       "using the first.", … );
            sceneRendererInfo.DirLights.DirectionLights.resize( 1 );
        }
```

There is **no named constant** — the limit is that literal `1` plus the UB layout:
`Graphic/ShaderProtocols/DirectionLight.hpp:7-18` — *"std140-friendly layout (two vec4s, 32 bytes)
matching `DirectionLightsUB` in `PBR.glsl.frag`"*, `Name = "DirectionLightsUB"`. Count uploaded via
`LightsMetadataUB.DirectionLightsCount` (`Materials/Mesh/PBR/MaterialPBRBase.cpp:139`).
Editor-side defensive checks: `EditorLayer.cpp:424-431` (*"Adding a sun here regardless is what produced
TWO directional lights — and the engine supports one"*), `:2020-2023`.
Every downstream consumer indexes `[0]` unconditionally after an `.empty()` check
(`SceneRenderer.cpp:424-427`, `MeshRenderer.cpp:1267`, `TerrainRenderer.cpp:77-82`, `MeshRenderer.cpp:318`).

**Implication for "Atmosphere Sun Light Index" (UE supports two suns): the engine supports one.** An index
field can be *authored and serialized* immediately, but making index 1 actually light and shade anything
requires widening `ShaderProtocols::DirectionLight`, `DirectionLightsUB`, every lit shader, the CSM path
(which fits cascades to a single `LightDirection`) and the RSM. **That is a separate project.** Recommend:
ship the field with a documented "index 1 is authored but not yet rendered" limitation, or restrict the
range to 0 for v1.

### 7.4 Is there any notion of "the sun" today?

**No.** No flag, no component, no tag type. Searched `Sun`, `Primary`, `Atmosphere`, `IsSun`, `IsMain`
across `Desert/Desert/Source` and `Editor/Source`.

What exists instead:
* **Identity by uniqueness** — "the sun" == "the one directional light". Stated at
  `SkyboxECSSystem.hpp:30` ("Sun = the directional light"), `Scene.cpp:356-358`, and most explicitly
  `SceneSettings.hpp:209-211`: *"the sun's single source of truth is the directional-light ENTITY."*
* **Selection is "first found"**, not "designated" — `SkyboxECSSystem.hpp:34-42`, `Scene.cpp:359-372`.
* **Convention-level markers only**: entities *named* "Sun" (`EditorLayer.cpp:432`, `:2267`;
  `Desert_Sandbox.desce` `"Tag":"Sun"`), a gizmo tooltip "Directional Light (sun)"
  (`LightGizmoRenderer.cpp:246`), an editor section titled "Sun Direction"
  (`ComponentEditorRegistrations.cpp:376-377`).
* The one comparable "primary" idiom in the codebase is **`CameraData::IsMainCamera`**
  (`Components.hpp:68-69`) — a plain reflected `bool` defaulting to `true`, inside a `Data` block, exactly
  the shape an `AtmosphereSunLight` flag wants. Mirror it.
* "Atmosphere" exists only as shader/render vocabulary (`Common/Atmosphere.glslh`,
  `Components.hpp:1457`, `SceneEnvironment.cpp:92`), never as an ECS concept.

### 7.5 The editor widget

There is **no** file in `ComponentWidgets/` for the directional light. It is a `ComponentEditorEntry` built
by `MakeDirectionalLightEntry()` — `ComponentEditorRegistrations.cpp:339-408`:
* `:346-352` — entry metadata: `Name = "Directional Light"`, `CanRemove = true`,
  `ReflectedTypeName = "DirectionalLightData"`, `Has`/`Add`/`Remove`/`DataPtr` lambdas.
* `:355-357` — **Color and Intensity come entirely from reflection**:
  `PropertyEditorBuilder::Draw( &c.Data, "DirectionalLightData", … )`. **So two new `PROPERTY` fields on
  `DirectionalLightData` appear in the panel with no widget code at all.**
* `:362-374` — reads `Translation` as travel dir; substitutes `(-0.4,-1.0,-0.5)` if degenerate; computes
  elevation `degrees(asin(toSun.y))` and azimuth `degrees(atan2(toSun.x, toSun.z))`.
* `:376-381` — collapsible "Sun Direction" section with a custom compass dial `DrawSunDial` (`:320-337`).
* `:383-392` — azimuth and elevation sliders + a "below horizon" warning.
* `:394-405` — writes back `t.Translation = -dir * length`, **preserving the original magnitude**
  (`:400-404`: *"some scenes author it as a 'sun position' and only the direction is read, so rewriting the
  magnitude would be a silent edit"*).

Also: `ComponentWidgets/TransformComponentWidget.cpp:21-28` **replaces** the whole Location/Rotation/Scale
block with `DrawDirectionWidget("Direction", transform.Translation)` for a directional light.

### 7.6 What has to change for the sky to take its sun from a designated light

1. **`Components.hpp:382-392`** — add to `DirectionalLightData`:
   ```cpp
   PROPERTY( DisplayName( "Atmosphere Sun Light" ), Category( "Atmosphere" ),
             Tooltip( "This light drives the sky/atmosphere." ) )
   bool AtmosphereSunLight = true;

   PROPERTY( DisplayName( "Atmosphere Sun Light Index" ), Category( "Atmosphere" ),
             Range( 0.0f, 1.0f ), EditCondition( "AtmosphereSunLight" ) )
   int AtmosphereSunLightIndex = 0;
   ```
   Default `true` so existing scenes keep working ([§6.4](#64-what-a-migration-must-do): a missing field
   keeps its C++ default, so **every old scene's single sun becomes the atmosphere sun automatically** —
   no migration needed for this part). Note the `Range` on an `int` is **unverified** as a slider form;
   check `PropertyEditorBuilder.cpp`'s int branch before relying on it.
2. **Rebuild** — reflection, serialization and the Details UI follow automatically (§7.5).
3. **`ECS/System/SkyboxECSSystem.hpp:30-44`** — replace "first light with a non-degenerate Translation"
   with "the light whose `Data.AtmosphereSunLight` is true and whose `Data.AtmosphereSunLightIndex`
   matches", falling back to the current behaviour when none is marked (so a scene with no marked light
   still gets a sky). Keep the single negation at `:40` — it is the engine's one sign flip.
4. **Decide the fallback contract explicitly.** Today three subsystems have three different no-light
   fallbacks (§7.2). If the sky now requires a *marked* light, "no marked light" is a new state. Either
   log once and fall back, or render a flat default sky — but pick one and document it.
5. **`Core/Scene.cpp:356-373`** — if index 1 is ever to be honoured, this is where the one-light truncation
   lives, and it is the wall (§7.3). For v1, leave it alone and constrain the index to 0.
6. **`ViewportPanel/LightGizmoRenderer.cpp:191-196`** — fix the inverted sign while you are here, and
   ideally have the gizmo mark which light is the atmosphere sun.
7. **Consider unifying** `DirectionalLightData::Color`/`Intensity` with `SkyboxComponent::SunColor`/
   `SunIntensity` (§7.2, last bullet). UE's atmosphere sun uses the light's own colour and intensity. This
   is a behaviour change with a visible result and belongs in the same design conversation as the sky
   settings move — not sneaked in.
8. **Test it** — `ECS::Rules`-style pure function `SelectAtmosphereSun( span<lights>, index ) -> optional<index>`
   in `ECS/System/SystemRules.hpp`, unit-tested in `Desert/Tests/Engine/SystemRules/`. This is the one
   piece of the sun rework that is verifiable without a GPU.

---

## Appendix — the top integration risks, ranked

1. **No 3D textures.** No `Image3D`, no `Image3DSpecification`, no `sampler3D`/`image3D` reflection, and
   the SPIR-V sampler classification is a **name heuristic** that would mis-bind one silently (§3.5).
   Either build the 3D image path or design v1 around analytic noise / a 2D tile atlas.
2. **Depth.** Nothing samples depth as a texture today, and doing so needs a **depth-aspect layout
   transition helper that does not exist** — `TransitionLayout` and `ComputeImageBeginWrite` hardcode
   `VK_IMAGE_ASPECT_COLOR_BIT`, and the depth image's tracked layout is
   `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`, which `SetInput` would bind verbatim (§3.3). In the **Deferred**
   path this is sidesteppable by sampling `GBufferC` world position instead, as SSAO does. In the
   **Forward** path there is no world-position buffer either, so cloud occlusion needs new
   infrastructure. Decide up front which paths are supported.
3. **The migration is silent.** Existing `.desce` scenes lose their sky the moment the fields move, because
   the load loop iterates the registry, not the file (§6.4). There is no field-alias mechanism and the
   project's precedent is to accept data loss on rename.
4. **`SetProceduralSky` has three call sites**, two of them editor-only bypass paths that build settings in
   C++ (`PreviewViewport.cpp:177`, `AssetThumbnailRenderer.cpp:97`) — the ones most likely to be missed
   (§1.2, §6.1).
5. **Per-renderer state.** `Docs/RENDERER_FRAME_STATE.md` — persistent storage buffers are shared across
   renderer slots by design, and per-renderer images multiply by the number of live previews. Any cloud
   history/weather buffer must be per-slot or lazily allocated (§3.7).
6. **The sun sign bug in shipped scenes.** `LightGizmoRenderer.cpp:191-196` is inverted and the two sandbox
   scenes author the sun below the horizon (§7.2). A believable volumetric sky will expose this on day one.
7. **Nothing can be seen locally.** No Vulkan, no editor (§5). Correctness must live in unit-testable pure
   math and in the migration test, or it does not exist.
