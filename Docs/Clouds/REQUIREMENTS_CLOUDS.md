# Requirements — Volumetric Clouds (`VolumetricCloudsComponent`)

**Version 2 — APPROVED.** All nine open questions of v1 have been **decided by the architect** and are now
requirements, not questions; the two consequences B raised in response (R-2, R-4) are closed the same way.
The decisions are folded into the body; §9 is the decision record, mapping each decision to the requirement
it produced. Changes from v1 are summarised in §0. Requirements are approved and go to task breakdown; no
further edits are expected from Analyst B.

**Analyst B.** Scope: the volumetric cloud subsystem only. The sky/atmosphere colour model, the atmosphere
sun light component and the `SkyboxComponent` → new-component scene migration are Analyst A's; this
document specifies only what clouds **consume** from them (§7) and what clouds contribute back.

**Inputs.** `Docs/Clouds/RESEARCH_REFERENCE.md` (cited as *REF §x*), `Docs/Clouds/RESEARCH_ENGINE.md`
(cited as *ENG §x*), `Docs/Clouds/DEV_CONTRACT.md`. Claims re-verified directly in the tree are cited
`file:line`. Anything I could not confirm is marked **unverified**.

**Notation.** `M(x)` ≡ `Common::Units::Metres( x )` = `x * 100` world units
(`Desert/Common/Source/Common/Core/Units.hpp:24`, `:31-34`). All distances below are world units
(centimetres) unless the cell says otherwise.

**Requirement form.** Each requirement is *statement — rationale — acceptance*. A requirement with no
checkable acceptance is not in this document.

---

## 0. What changed in v2

| Area | v1 | v2 (architect's decision) |
|---|---|---|
| Intra-phase pass order | flagged as non-deterministic, workaround offered | **CLD-19** — the graph primitive is fixed: stable tie-break by registration order, unit-tested; **clouds composite before particles**. The bypass is withdrawn. |
| Temporal accumulation | open question, "recommend v1" | **In v1, as its own task with its own acceptance** (CLD-32, CLD-32a, CLD-32b). Camera-only reprojection; `Off` is a full tested path; the bought artefacts are named up front. |
| Depth | two options | **CLD-13 built.** One path for Forward and Deferred. No `GBufferC`-only variant. |
| `RGBA16F` | non-goal N7 | **CLD-18 — added to `ImageFormat` now.** Scatter/history targets become `RGBA16F`; history memory halves. And **CLD-18a** — `GetBytesPerPixel` becomes total and loses its silent `return 0U`, so the next added format is a compile error rather than a wrong allocation size. |
| Clouds in the IBL bake | non-goal | Still non-goal; recorded as a **named v1.1 item** with its testable formulation (§1.4). |
| Planet radius | "a smaller cloud-only radius is a possible fallback" | **Forbidden.** One radius for sky and clouds; divergence escalates to the architect (CLD-24a). |
| Sampler-classification check | open question about its form | **Must exist**; if `Reflect()` needs a device, it moves to `DShaderTool --reflect` and the developer says so in the report (CLD-11). |
| Presets | recommended C++ table | **Confirmed**, engine-wide for both halves; a preset *asset* is an explicit non-goal (N10). `CloudPresetValues` kept. |
| §7 sky contract | C++ palette struct (`SkyRadianceZenith/Horizon`, `GroundAlbedo`) | **Rewritten.** `AtmosphereEnv` is narrow (sun direction, sun radiance, ambient term, valid flag); the sky is **evaluated**, not transported — clouds `#include <Common/Atmosphere.glslh>` and call `EvaluateSky` (CLD-70, CLD-70a). |
| CLD-35 storage-buffer slot dimension | marked unverified | **Verified** — `VulkanStorageBuffer.hpp:36`, `.cpp:123,130`. Mark removed. |

---

## 1. Goals and non-goals

### 1.1 Goals (in the client's terms)

| # | Goal |
|---|---|
| G1 | The flat painted cloud layer is **deleted** and replaced by a real volumetric cloudscape you can fly through — a spherical shell layer covering the whole sky, occluded correctly by terrain and buildings, lit by the scene's sun. |
| G2 | Clouds live in **their own ECS component**, `VolumetricCloudsComponent`, separate from sky/atmosphere. |
| G3 | **"All possible settings" exposed** — every value that changes the look is a named, ranged, tooltipped, serialized, undoable field in the Details panel. 95 fields (§4). Nothing hidden behind a recompile. |
| G4 | **Named presets** — Clear, Fair Weather, Partly Cloudy, Stratus, Overcast, Storm, Cirrus (§5). One click gives a recognisable sky. |
| G5 | **Quality is orthogonal to look** — a separate Low/Medium/High/Ultra/Custom tier owns every sampling budget. Choosing "Storm" never changes the frame cost; choosing "Ultra" never changes the weather (§6). |
| G6 | Everything correctness-critical is a **pure function with a unit test**, because the editor cannot be launched in this environment (`DEV_CONTRACT.md:82-91`, ENG §5.1). |

### 1.2 Non-goals for v1 (deliberate, each with a reason)

| # | Non-goal | Reason |
|---|---|---|
| N1 | Baked NVDF "hero clouds" (the Nubis3 path). | Its look depends on Guerrilla VDBs we may not ship (REF §F.4, §J.1 risk 1). Replaced by an extension point (CLD-33). |
| N2 | OpenVDB at runtime, and any VDB/Houdini import path. | Architect decision 4; `DEV_CONTRACT.md:37-38`. |
| N3 | Clouds in the IBL bake — an overcast sky will not darken scene lighting in v1. | The bake runs outside the render graph and may `WaitDeviceIdle` (ENG §1.2, `SkyboxRenderer.cpp:87`); the cloud volume does not exist at bake time. Decided (D-5); the v1.1 shape is §1.3 V1.1-a. See also CLD-74. |
| N4 | Cloud shadows cast onto scene geometry (a cloud-shadow map). | Requires a second shadow projection and a change to every lit shader. Separate project. |
| N5 | Precipitation, lightning, aurora, godrays/light shafts. | Godrays are a post-process (REF §G.3) and belong to the post chain, not the cloud pass. |
| N6 | A second wind system. | Wind is scene-global by design (`SceneSettings.hpp:194-203`, ENG §1.2). Clouds read `SceneRenderer::GetWind()`. |
| N7 | New `ImageFormat` entries **`R8` and `R16F`**. | `RGBA8F` covers every noise volume we need. (`RGBA16F` **is** in scope — see CLD-18; the v1 non-goal was overturned by the architect.) |
| N8 | Near/far split raymarch (REF §B.2-B.3). | It exists to serve a hero cloud in a box, produces a seam at exactly 500 units (REF §D.4) and its handoff is one of the confirmed defects (REF §J.3 #1). Superseded by CLD-25. |
| N9 | The light voxel grid (REF §B.1). | A 256×256×32 grid is sized for a 2 km box. Over a 150 km cloudscape it is meaningless. Superseded by the cone march (CLD-26). Recorded as a future option in CLD-33. |
| N10 | A **preset asset file** (data-driven presets loaded from disk). | Architect decision, engine-wide for both halves of the programme. A preset asset means a new asset type with its own registry entry, `AssetResolver` branch, editor picker, thumbnail path and migration story — a project in itself, for a payload that is currently seven rows of numbers. Presets are a `constexpr` C++ table (CLD-50): adding one is a single table entry in a single file. |

### 1.3 Named v1.1 items (out of v1, but specified so they are not re-litigated)

| # | Item | The shape it must take |
|---|---|---|
| V1.1-a | **Clouds darken scene lighting** (overcast ≠ clear, ENG §1.2). | Not the volume in the bake. A **scalar sky-occlusion coefficient computed on the CPU** from `Coverage`, `ExtinctionScale` and `LayerThickness`, passed into `EnvironmentManager::CreateProcedural` alongside the existing sun/sky arguments. Being a pure CPU function of three fields, it is unit-testable without a GPU — which is the whole reason for this shape. |
| V1.1-b | Baked-volume density source (the Nubis3 NVDF path, N1). | A second implementation of `Common/CloudDensity.glslh` behind the seam already required by CLD-33. No change to the march loop, the light march, the compositor or the component. |
| V1.1-c | Cloud shadows on scene geometry (N4). | Separate project — a second shadow projection touching every lit shader. |

### 1.4 Explicitly not ported

We reimplement from the published Nubis 1/2/3 formulas. Every defect in REF §J.3 is a thing we must **not**
reproduce, and each has a positive requirement below that contradicts it:

| REF defect | Our requirement |
|---|---|
| J.3 #2 light grid marches *away* from the sun | CLD-26 — the light march is defined in world space toward `SunDirection`, unit-tested. |
| J.3 #3 `HG(cos, 1.0) ≡ 0`, no forward lobe | CLD-27 — dual-lobe HG with `PhaseForwardG` ∈ [0, 0.99]; a test asserts the forward lobe is non-zero and peaks at `cos = 1`. |
| J.3 #4 `Remap(0.5, …)` replaces the height fraction | CLD-27 — the in-scatter probability is a function of the normalized height fraction; test asserts it varies with height. |
| J.3 #5 `mTransmittance` is both loop guard and accumulator | CLD-25 — transmittance is a separate scalar initialised to 1, monotonically non-increasing; the early-out gates on it; unit-tested. |
| J.3 #6 `abs()` in the slab test | CLD-24 — signed ray/shell intersection; test asserts a miss when the ray points away. |
| J.3 #11 mip level computed and discarded | CLD-30 — no mip chain in v1; distance filtering is the explicit `DistanceSoftening` term, not a dead LOD. |
| J.3 #12 `DENSITY_SCALE` defined, never used | CLD-88 — the dead-parameter test forbids a field that does not change the GPU payload. |
| J.3 #13 double tonemapping | CLD-31 — the cloud pass writes **linear HDR** into the scene target; the engine tonemapper owns the curve (ENG §3.2 steps 15-16). |

---

## 2. Rendering architecture

### 2.1 Stage chain

Five stages. Positions are relative to the numbered steps of `SceneRenderer::OnUpdate`
(`SceneRenderer.cpp:356-623`, enumerated in ENG §3.2).

| # | Stage | Kind | When | Inputs | Outputs |
|---|---|---|---|---|---|
| S0 | Noise volume generation | compute, **immediate** (`ComputePipeline::Dispatch`) | once per process, and on seed/tile change | push constants (seed, dims) | `ShapeNoise` 128³ RGBA8F, `DetailNoise` 32³ RGBA8F, `CurlNoise` 128×128 RGBA8F |
| S1 | Weather map generation | compute, **in-frame** | new step 7.5, only when the weather fields are dirty | weather fields (SSBO) | `WeatherMap` 512×512 RGBA8F storage image |
| S2 | Raymarch | compute, **in-frame** | new step 7.5, after S1 | S0 volumes, S1 map, scene depth, params SSBO, push constants | `CloudScatterTransmittance` **RGBA16F** at `ResolutionScale`, `.rgb` = in-scattered radiance (premultiplied), `.a` = transmittance; **and `CloudDepthGuide` RGBA8** — the distance each ray was allowed to run to (T9: S4 cannot read the scene depth, see CLD-32) |
| S3 | Temporal resolve | compute, **in-frame** | new step 7.5, after S2, **only when `TemporalMode = Reprojection`** | S2 output, history image, previous view-projection | the resolved image, which IS the new history — one ping-pong half, not a third target |
| S4 | Composite | **graphics**, `RenderPhase::Transparency` | step 8, `ExecuteTransparency()` | S3's output (or S2's, with `TemporalMode = Off`) and the depth guide — ~~scene depth~~, which this pass cannot sample | scene colour target (LOAD, blended) |

**CLD-20 — S0..S3 are compute dispatches issued from a new `SceneRenderer::ExecuteVolumetricClouds()`
called between the deferred block (step 7) and `ExecuteTransparency()` (step 8).**
*Rationale:* the raymarch needs finished scene depth, which only exists after step 7 in Deferred and after
the graph in Forward; and in-frame compute must be issued **outside an open render pass**
(ENG §3.4). At that point in `OnUpdate` no render pass is open — the deferred block has ended its passes and
`ExecuteTransparency` has not begun one. This is exactly the precedent of steps 1/2/4/5/7/11/13-17.
*Acceptance:* the call appears once in `OnUpdate` between the deferred block and `ExecuteTransparency()`;
`make Desert Editor config=debug` and `config=release` build; no Vulkan validation error about dispatching
inside a render pass is reported by the user when they run it.

**CLD-21 — S4 is registered as a render-graph pass in `RenderPhase::Transparency` (700).**
*Rationale:* architect decision 5. `ExecuteTransparency()` replays Transparency passes with
`BeginRenderPass( pass, /*clearFrame=*/false )` — a LOAD begin over the finished composited scene
(`SceneRenderer.cpp:1140-1175`), which is correct in both Forward and Deferred, and still before bloom (15)
and tonemap (16) so clouds get exposure and glow for free.
*Acceptance:* `RegisterPasses` adds exactly one pass with `Phase == RenderPhase::Transparency`; a unit
test on the render system's registration asserts the phase ID equals 700.

**CLD-21a — within `Transparency`, the cloud composite must execute BEFORE the particle billboards.**
*Statement:* the cloud render system is registered ahead of `ParticleRenderer` in `SceneRenderer::Init`, and
the graph's now-deterministic intra-phase order (CLD-19) preserves that.

> **CORRECTION (architect, after T5 landed).** CLD-19's diagnosis — and the identical claim in
> `RESEARCH_ENGINE.md` §3.1 — was **wrong**. Passes inside a phase already emerged in registration
> order; `m_PhasePasses` is a vector. The real disorder sat on either side of it:
> `SceneRenderer::RebuildRenderGraph` iterated an `unordered_map` of render systems, so "registered
> first" meant "name hashed low" and reshuffled whenever a system was added, and the phase-level
> topological queue drained from `unordered_map` edge lists. Implementing CLD-19 as written would
> have produced a sort that is deterministic while ordering by nothing meaningful, and CLD-21a's
> mechanism — register the cloud system ahead of the particle one — would still not have held.
>
> As landed: order inside a phase is an explicit `OrderInPhase` key first, registration index
> second. **The cloud composite states its own place** (`RenderPassOrder::FarField`) instead of
> relying on where its system is registered. Found by the T5 developer by reading the code the
> requirement cited.

*Rationale:* clouds are the far field and particles are the near field; sparks and smoke drawn by an emitter
in front of the camera must sit **over** the cloudscape, not be erased by it. Ordering the other way is the
same class of mistake as the particle "top-down" bug (`SceneRenderer.cpp:1146-1151`).
*Acceptance:* the sort unit test of CLD-19 includes this exact case — two passes registered in
`Transparency`, clouds first, and the sorted output preserves that order across repeated builds.

**CLD-22 — the S4 pipeline must set its own depth state explicitly (`DepthTestEnabled = false`,
`DepthWriteEnabled = false`), not inherit.** *Rationale:* the existing particle pipelines in that phase set
depth test off on purpose and the phase forces nothing (`SceneRenderer.cpp:1150-1151`). Occlusion is
resolved inside the raymarch (CLD-29), not by the fixed-function depth test — a fullscreen composite quad
has no meaningful depth. *Acceptance:* code review of the `PipelineSpecification`; the fields are literals,
not defaults.

### 2.2 Geometry and the numeric-precision rule

**CLD-23 — the cloud layer is a spherical shell** concentric with the planet centre, defined by
`PlanetRadius` (owned by `SkyAtmosphereComponent`, §7), `LayerBottomAltitude` and `LayerThickness`.
*Rationale:* architect decision 3; a box gives one hero cloud on an empty sky (REF §C.2), a shell gives a
horizon-wrapping cloudscape and is what Nubis1/2 and every shipping title use (REF §C.7).
*Acceptance:* `CloudMath::ShellBounds()` unit test — camera below / inside / above the layer, ray up /
down / horizontal / away from the planet.

**CLD-24 — ray↔shell intersection is a signed analytic ray/sphere test evaluated in a planet-relative,
kilometre-scaled space (world units ÷ 100 000), not in world centimetres.**
*Rationale:* two reasons. (a) REF §J.3 #6: the reference's `abs()` slab test reports hits behind the camera;
we use a signed test. (b) **Precision.** With `PlanetRadius` at Earth scale, 6360 km = 6.36e8 world units,
and the quadratic's `c` term is `r²` ≈ 4.0e17 — far outside `float`'s 24-bit mantissa (ulp ≈ 3.2e10), so the
discriminant loses every significant digit. In km-space `r = 6360`, `r²` ≈ 4.0e7, ulp ≈ 4, which is fine.
*Acceptance:* `CloudMath` test computes the shell entry/exit for a camera at 2 km altitude looking at 5°
above the horizon with `PlanetRadius = M(6360000)` and asserts the entry distance is within 0.1 % of a
`double`-precision reference computed in the same test; the same test run in world-unit space is asserted to
fail that tolerance (documenting *why* the scaling exists).

**CLD-24a — all cloud-space arithmetic is camera-relative, and a cloud-only planet radius is FORBIDDEN.**
*Statement:* the camera position is subtracted before any shell or sampling arithmetic, so world magnitudes
never enter the shader at planet scale. `PlanetRadius` has exactly one value in the scene, owned by
`SkyAtmosphereComponent` (§7), shared by the sky and the clouds. The cloud subsystem must not scale it,
clamp it, or substitute a smaller "rendering radius" of its own.
*Rationale:* architect decision. A cloud shell on a 600 km sphere under a sky built on a 6360 km sphere
produces a horizon that does not agree with itself — clouds meeting the ground line at the wrong place — and
that is a week of searching because both halves look individually correct. If camera-relative maths still
leaves artefacts, the escalation path is to change the radius **once, for both subsystems**, with the
architect; it is not a local fix.
*Acceptance:* grep — no literal planet-radius constant and no scaling of `AtmosphereEnv::PlanetRadius`
anywhere in the cloud code or shader; the CLD-24 precision test is run with the camera at
`(0, PlanetRadius + M(2000), 0)` in absolute coordinates and asserts the camera-relative formulation matches
the `double` reference where the absolute one does not.

### 2.3 The raymarch

**CLD-25 — one raymarch pass at `ResolutionScale`, not a near/far split.**
*Statement:* a single compute dispatch marches `[tEnter, min(tExit, tGeometry, MaxViewDistance)]`, accumulating
premultiplied in-scattered radiance `L` and transmittance `T` (initialised `T = 1`, updated
`T *= exp(-sigma_t * density * dt)`), with an early-out at `T < 0.005`. Step length is
`clamp( MinStepSize + StepGrowthRate * t, MinStepSize, MaxStepSize )`, capped at `MaxSteps` iterations,
and the ray origin is offset by a per-pixel Bayer/blue-noise fraction of one step scaled by `JitterStrength`.
*Rationale:* the reference's split exists for a hero cloud, leaves a seam at 500 units and its
density handoff is broken (REF §D.4, §J.3 #1). A single loop with distance-adaptive steps gives the same
saving without a seam, and one loop is one thing to keep correct. `T` is a genuine transmittance, which is
what makes the early-out work at all (REF §J.3 #5).
*Acceptance:* `CloudMath::ScheduleSteps()` unit test — step distances strictly increasing, first ≥ `tEnter`,
last ≤ `tExit`, count ≤ `MaxSteps`, every step within `[MinStepSize, MaxStepSize]`, and total coverage of
`[tEnter, tExit]` when `MaxSteps` is not binding. `CloudMath::BeerTransmittance()` test — `T(0) = 1`,
monotone non-increasing, `T(∞) = 0`, and `T(a+b) == T(a)*T(b)` within 1e-5.

**CLD-26 — empty-space acceleration is a two-tier coarse/fine march plus the analytic shell bounds; there is
no SDF and no baked occupancy volume in v1.**
*Statement:* outside the shell, zero samples (CLD-24). Inside, the marcher evaluates only the **cheap**
density (weather map × height gradient × base shape, no detail, no lighting) while stepping at
`step * CoarseStepMultiplier`. On a non-zero cheap sample it steps back one coarse step and switches to fine
sampling (detail + light march). After `EmptySamplesBeforeCoarse` consecutive zero fine samples it returns to
coarse.
*Rationale:* the reference's empty-space skip is an SDF baked into the NVDF alpha channel
(REF §C.3), which only exists because the shape is baked. We have no SDF (REF §J.1 risk 5). The
miss-counting heuristic is the Nubis1/2 answer (REF §C.7, tunables #170/#171) and needs no extra memory.
*Acceptance:* `CloudMath::MarchSchedule()` test drives a synthetic density function (a slab occupying 30-40 %
of the ray) and asserts (a) fine sampling covers the whole occupied interval, (b) the total sample count is
at least 2× lower than a uniform fine march over the same interval, (c) no fine sample is skipped at the
slab entry (the step-back is exercised).

**CLD-27 — light sampling is a cone march toward the sun with dual-lobe Henyey–Greenstein, Beer + powder,
and a height-dependent in-scatter probability.**
*Statement:* `LightMarchSamples` samples along `SunDirection`, at exponentially increasing offsets over
`LightMarchDistance`, each offset perturbed inside a cone of half-angle `LightConeSpread`. Optical depth to
the sun feeds `exp(-tau)`; the phase term is
`mix( HG(cos, PhaseBackwardG), HG(cos, PhaseForwardG) * SilverLiningIntensity, PhaseBlend )`; the powder
term is `1 - PowderStrength * exp(-PowderScale * density)`; and the in-scatter probability is a function of
the **normalized height fraction** `h ∈ [0,1]` within the layer.
*Rationale:* every one of the reference's three lighting defects (REF §J.3 #2/#3/#4) is a consequence of not
doing exactly this. `eccentricity = 1.0` erases the forward lobe; the constant `0.5` erases the vertical
gradient; the grid marched the wrong way.
*Acceptance:* `CloudMath` tests — `HG(c, 0)` ≡ `1/(4π)` for all `c` within 1e-6; `HG(c, g)` for `g > 0`
strictly increases in `c` and is maximal at `c = 1` (this is the test that would have caught `g = 1.0`);
`DualLobePhase` is positive for all `c ∈ [-1,1]` and all valid `(g0, g1, blend)`;
`InScatterProbability(h)` is not constant in `h` (this is the test that would have caught `Remap(0.5, …)`);
`ConeSampleOffsets(SunDirection, …)` returns offsets whose dot product with `SunDirection` is positive for
every sample.

**CLD-28 — multiple scattering is approximated by `MultiScatterOctaves` octaves with geometric falloff**
(`MultiScatterExtinctionFalloff`, `…ScatterFalloff`, `…PhaseFalloff` per octave), evaluated from the same
optical depth — no extra light march per octave.
*Rationale:* single scattering makes thick cloud cores black; this is the standard cheap fix and it costs
arithmetic, not samples, so it can sit in the quality tier without changing the sample budget.
*Acceptance:* unit test — octave 0 with all falloffs = 1 reproduces the single-scatter result bit-for-bit;
adding octaves is monotonically brightening for a fixed optical depth.

**CLD-29 — depth-aware compositing: the march terminates at scene geometry, sampled from the scene depth
attachment inside the S2 compute shader.**
*Statement:* S2 binds the target framebuffer's depth attachment as a sampled image, linearises it, and clamps
`tExit` to the geometry distance. Where nothing was drawn (cleared depth) the march runs to
`MaxViewDistance`. S4 does **not** re-test depth (CLD-22).
*Rationale:* architect decision — it must work in **both** rendering paths, with one code path.
`SceneSettings::RenderingPath` defaults to Forward (`SceneSettings.hpp:42-46`) and the Forward path has
neither a G-buffer nor a world-position target (ENG §3.3), so the `GBufferC` trick used by SSAO covers only
Deferred; "clouds are occluded only in Deferred" is exactly the half-result the contract forbids
(`DEV_CONTRACT.md:13-15`). Sampling depth once, in compute, outside any render pass, is one path for both —
and reading it in compute avoids the read/write hazard of sampling the depth of a bound framebuffer. It
requires CLD-13. **There is no `GBufferC` variant and no per-path branch.**
*Acceptance:* `CloudMath::ClampExitToGeometry()` unit test over the depth-linearisation formula (near/far
plane round-trip: linearise(project(d)) == d within 1e-4 across the depth range); build succeeds; the user
reports no `VulkanDebugCallback` layout warning for the depth image.

**CLD-30 — no mip chain and no runtime LOD selection on the noise volumes in v1. Distance aliasing is
handled by the explicit `DistanceSoftening` / `SofteningStartDistance` / `SofteningEndDistance` terms.**
*Rationale:* the reference computes a mip level and throws it away, with `maxLod = 0` on every sampler
(REF §J.3 #11) — a dead knob pretending to be a feature. We either build 3D mip generation (a real engine
prerequisite we are choosing not to take, CLD-11 note) or we say so.
*Acceptance:* the 3D image specification in CLD-10 has `Mips = 1`; no shader samples with an explicit LOD;
`DistanceSoftening` is covered by the dead-parameter test (CLD-88).

**CLD-31 — the cloud pass writes linear HDR; it never tonemaps, never applies gamma, never applies exposure.**
*Rationale:* REF §J.3 #13 — the reference tonemaps twice and composites non-linearly. The engine's tonemap
runs at step 16 (`SceneRenderer.cpp:603-606`) and owns the curve; the existing sky shader already follows
this rule (`ProceduralSky.shader:68`, ENG §1.3).
*Acceptance:* code review — no `pow(…, 1/2.2)`, no Uncharted/ACES/Reinhard term and no exposure multiply in
`VolumetricClouds.shader`; grep-checkable.

### 2.4 Temporal resolve and upsampling

**CLD-32 — S3 implements reprojection-based temporal accumulation with a neighbourhood clamp, and S4
upsamples with a distance-aware bilateral filter.**
*Statement:* S3 reprojects each pixel through the **cloud shell** — intersect the view ray with the shell
mid-surface, transform that point by the previous frame's view-projection, and sample the history at the
resulting UV (the standard trick, sketched but disabled in the reference, REF §D.3). History is rejected when
the reprojected UV leaves the screen or the sample falls outside a `TemporalClampScale`-scaled min/max box of
the 3×3 current neighbourhood. Blend weight is `TemporalBlendFactor`. `TemporalMode = Off` makes S3 a no-op
and S4 still bilaterally upsamples.

> **CORRECTION (T9 developer, as landed).** Three things in this requirement did not survive contact.
>
> 1. **"Rejected" versus "clamped" — the statement and its own acceptance disagreed.** The statement above
>    says history is rejected when it falls outside the box; the acceptance three lines down says an outside
>    sample "is clamped onto the box boundary", and CLD-32b calls the mechanism "the neighbourhood clamp"
>    throughout. As landed: leaving the SCREEN rejects (the pixel resolves to the marched frame, bit for
>    bit); leaving the BOX clamps onto the boundary and is then blended. Rejecting on the box would snap a
>    disoccluded pixel to a single frame of a jittered half-resolution march, which is the flicker the
>    accumulation exists to remove.
> 2. **S4 cannot read the scene depth, so S3 is not where the low-resolution depth comes from.** The
>    composite is a graphics pass inside a render pass that has the depth attachment bound, and sampling a
>    bound attachment is a feedback loop — the same constraint that put CLD-29's depth read in compute. The
>    guide the bilateral filter weights by is therefore written by S2, which already holds the number
>    (`CloudGeometryLimit`), into an RGBA8 image at the raymarch's own resolution. It costs six lines in the
>    march and no extra dispatch; the alternative was a second compute pass re-reading the depth to
>    recompute what S2 had just worked out. It also means the guide exists in BOTH temporal modes, which
>    CLD-32a constraint 2 requires and an S3-owned guide could not have delivered.
> 3. **What the bilateral filter actually fixes.** With no full-resolution depth in the composite, the
>    reference for each pixel is the nearest tap's distance, not the pixel's own. That removes the SMEAR
>    across a silhouette — a foreground texel no longer bleeds cloud into the open sky beside it — but it
>    cannot place the silhouette more finely than the cloud buffer's own grid. Sub-texel edge placement
>    would need a full-resolution depth copy, which is a full-resolution image this programme has not
>    budgeted (CLD-34) and which nothing else in the frame needs.
*Rationale:* at `ResolutionScale = Half` with 64 steps the raw march boils visibly; the reference's
"temporal upscaling" is a marketing name for a spatial split with no history at all (REF §D.2), so there is
nothing to port and this is designed from scratch. The bilateral upsample replaces the reference's plain
bilinear magnify, which is what produces its 500-unit seam (REF §D.2).
*Acceptance:* `CloudMath::ReprojectThroughShell()` unit test — with an identity camera delta, the reprojected
UV equals the source UV within 1e-5 for a grid of directions; with a pure translation the UV shift matches a
`double` reference; the out-of-screen rejection predicate is exercised.
`CloudMath::ClampToNeighbourhood()` test — a history sample inside the box passes through unchanged, one
outside is clamped onto the box boundary, `TemporalClampScale` widens the box monotonically.

**CLD-32a — the temporal stage is a separate task with separate acceptance, under three constraints.**
1. **Camera-only reprojection.** The reprojection is a function of the camera delta and the shell geometry alone. There are **no per-object motion vectors** and no motion-vector buffer: cloud movement is wind, and wind is already integrated into the march through the animated sample position, so a reprojected cloud pixel is correct to first order without one. Adding a motion-vector target would be new engine infrastructure for an effect the wind term already produces.
2. **`TemporalMode = Off` is a first-class, tested path**, not a "if it breaks" branch. It is the Low tier's real configuration (CLD-61): S3 becomes a no-op, S4 still bilaterally upsamples, and the pipeline is complete and correct without history. A build in which `Off` is untested or degraded is not accepted.
3. **The stage ships with its artefact list** (CLD-32b) written before the code, because none of it can be seen in this environment.
*Rationale:* architect decision. Temporal accumulation is the largest single chunk of the programme and the
one with no working reference (the reference's reprojection is commented out, REF §D.3). Splitting it out
means it can be reviewed, and can slip, without holding the raymarch hostage.
*Acceptance:* the temporal work is its own task with its own DoD; the CLD-87 math tests for reprojection and
clamping pass; a test drives the full resolve with `TemporalMode = Off` and asserts the output equals the S2
output bit-for-bit (proving the no-op path is real).

**CLD-32b — the artefacts we are knowingly buying must be named in the developer's report and in a comment
at the top of the temporal shader.** With their trigger and their mitigation knob:

| Artefact | Trigger | Knob that trades it away |
|---|---|---|
| **Disocclusion trails** — a smear behind geometry that uncovers new cloud | translating past a foreground object; flying out of a canyon | lower `TemporalBlendFactor` weight is *worse*; the fix is the neighbourhood clamp — lower `TemporalClampScale` |
| **Inertia / lag on fast rotation** — clouds "catch up" after a whip pan | ~~angular velocity high enough that most reprojected UVs leave the screen~~ **corrected below** — any rotation, on the pixels that STAYED on screen | raise `TemporalBlendFactor` (more current frame), or `TemporalMode = Off` |
| **Shell-parallax error** — reprojection assumes the pixel sits on the shell mid-surface, so very near clouds reproject slightly wrong | camera inside or just below the layer | inherent to camera-only reprojection; bounded because near clouds are also where `NearFadeMinDensity` thins them |
| **Ghosting on wind-driven silhouette change** — an edge that changed because the cloud moved, not because the camera did | high `AnimationSpeed`/`WindInfluence`, e.g. the Storm preset | the neighbourhood clamp; this is the case the clamp exists for |
| **Sun-glint flicker** — a bright forward-scatter pixel enters and leaves the clamp box | looking near the sun with high `SilverLiningIntensity` | raise `TemporalClampScale` (accepting more ghosting) |

> **CORRECTION (T9 developer, table checked against the landed code).** Four of the five rows survive:
> each names a mechanism that exists, and the knob named in each row is the one that drives that mechanism.
> Row 2's TRIGGER is backwards.
>
> A pixel whose reprojected UV leaves the screen has NO history — `CloudReprojectThroughShell` reports
> `Valid = false`, and `CloudTemporalResolve` returns the marched frame bit for bit. Those pixels are the
> ones with no inertia at all. The lag is in the pixels that stayed on screen and were blended: the
> reprojection is exact for a rotation about the eye, but the shell mid-surface stand-in is not exact for
> anything else, and where the reprojection is slightly wrong the accumulation converges to the new answer
> at the rate `TemporalBlendFactor` sets. So the artefact is real, the knob is right, and only the
> explanation of when it appears was wrong: it is not the pixels that left the screen, it is the ones that
> did not.
>
> Verified for the other four, in the code that shipped:
> * **Disocclusion trails** — the reprojected UV of a disoccluded pixel is on-screen and valid, so only the
>   box catches it; a narrower box (lower `TemporalClampScale`) pulls the stale sample further toward the
>   current neighbourhood, and a lower `TemporalBlendFactor` keeps more of the stale sample. Both directions
>   as stated.
> * **Shell parallax** — `CloudReprojectThroughShell` intersects `bottomKm + 0.5 * thicknessKm`, so the
>   error is exactly as described, and `NearFadeMinDensity` is a real field of §4.2 acting on the near end.
> * **Wind ghosting** — the reprojection contains no wind term at all (CLD-32a constraint 1), so the clamp
>   is indeed the ONLY mechanism that can notice a silhouette that moved on its own.
> * **Sun-glint flicker** — `SilverLiningIntensity` multiplies the forward lobe alone
>   (`CloudDualLobePhase`), so it is the field that makes such a pixel bright; widening the box lets the
>   bright history survive the clamp.

*Rationale:* `DEV_CONTRACT.md:82-86` and ENG §5.1 — we cannot look at the result here, so an artefact that is
not named in advance will be reported later as a bug and re-investigated from scratch. Naming them is the
only form of "we knew this" that survives.
*Acceptance:* the table is present in the report and mirrored in the shader header comment; each row names a
field that exists in §4.2 (checkable by grep against the field list).

**CLD-33 — the density-field source is an extension point.**
*Statement:* the shader's density evaluation is reached through exactly two functions with a fixed
signature — `CloudDensityCheap( vec3 worldPos, float heightFraction )` and
`CloudDensityFull( vec3 worldPos, float heightFraction, float distance )` — declared in a header
`Common/CloudDensity.glslh` and implemented in `Common/CloudDensityProcedural.glslh`, included by the
raymarch shader. On the C++ side the resources those functions need are supplied through a
`CloudDensitySource` struct (the set of bound images + the parameter block region), populated by one
function. Adding a baked-volume source later means a second `.glslh` implementation and a second populate
function — it must not require editing the march loop, the light march, the compositor or the component.
*Rationale:* architect decision 3 explicitly requires that a baked volume can be added later without
redesign. This is the seam, and it costs nothing today.
*Acceptance:* code review — `VolumetricClouds.shader`'s march loop contains no direct
`texture( u_ShapeNoise, … )` / `u_WeatherMap` reference; every density read goes through the two functions.
Grep-checkable.

### 2.5 Resources — and what the engine actually has

| Resource | Dimensions | Format | Lifetime / ownership | Exists in engine today? |
|---|---|---|---|---|
| `ShapeNoise` | 128 × 128 × 128 | `RGBA8F` (8 MiB) | one per process, shared, refcounted; regenerate on `ShapeSeed`/tile change | ❌ **needs CLD-10** (`Image.hpp:15,46,70` — only `Image`, `Image2D`, `ImageCube`) |
| `DetailNoise` | 32 × 32 × 32 | `RGBA8F` (128 KiB) | same | ❌ CLD-10 |
| `CurlNoise` | 128 × 128 | `RGBA8F` (64 KiB) | same | ✅ `Image2D`, `Storage \| Sample` |
| `WeatherMap` | 512 × 512 | `RGBA8F` (1 MiB) | one per `SceneRenderer`, lazily allocated | ✅ |
| `CloudScatterTransmittance` | target × `ResolutionScale` | `RGBA16F` | per `SceneRenderer`, lazily allocated | ❌ **needs CLD-18** |
| `CloudDepthGuide` | same | `RGBA8F` | per `SceneRenderer`, lazily allocated, **in both temporal modes** | ✅ (added by T9 — see below) |
| `CloudHistory` ×2 (ping-pong) | same | `RGBA16F` | per `SceneRenderer`, lazily allocated, only when `TemporalMode != Off` | ❌ CLD-18 |
| `CloudParams` SSBO | ≈ 512 B | std430 | **non-persistent** `StorageBuffer::Create( …, persistent = false )` | ✅ `StorageBuffer.hpp:29-30` |
| Scene depth (read) | target | `DEPTH24STENCIL8` (`SceneRenderer.cpp:58`) | borrowed | ⚠️ **needs CLD-13** |

**CLD-34 — every per-`SceneRenderer` cloud image is allocated lazily on first actual use, with the failure
latched (never retried per frame), and its byte cost logged once at `LOG_INFO`.**
*Rationale:* the editor creates several `SceneRenderer`s (the Details mesh preview, the asset thumbnail
renderer) and anything allocated in the constructor is paid for once per preview — the engine's own answer is
`EnsureGIResources`/`EnsureSSRResources` (`SceneRenderer.cpp:644-746`, rationale `:96-100`), which we copy.
At Full resolution with temporal on, three 1920×1080 `RGBA16F` images is **≈ 47.5 MiB per live renderer**
(it would have been ≈ 95 MiB at `RGBA32F` — this is what CLD-18 buys); at the default High tier
(`Half`) it is ≈ 11.9 MiB. Those numbers must be visible in the log, not discovered in a memory graph.
*Acceptance:* code review against the `EnsureSSRResources` shape; a `LOG_INFO` line naming the resolution,
format and total bytes; a unit test asserts the byte arithmetic for `Half`/`Full` × `Off`/`Reprojection`
against the four figures above.

> **CORRECTION (T9 developer, as landed).** Two problems with the figures.
>
> *The table never quoted four figures* — only the two `Reprojection` ones. And they omit the depth guide
> that CLD-32's bilateral upsample needs, because the resource table above did not have it. The guide is
> `RGBA8F` at the raymarch's own resolution, a quarter of a colour target's bytes, and it is needed in both
> temporal modes. The four figures at 1920×1080, as `Graphic::CloudScaledImageBytes` computes them and as
> `Tests/Engine/CloudTemporal` asserts them:
>
> | | `Off` | `Reprojection` |
> |---|---|---|
> | **Full** | 23.73 MiB | 55.37 MiB *(was quoted as 47.5)* |
> | **Half** (default High tier) | 5.93 MiB | 13.84 MiB *(was quoted as 11.9)* |
>
> The weather map is deliberately outside these figures, exactly as it was in the original: it is one fixed
> megabyte that does not move with the view size or with any of these settings.

**CLD-35 — the cloud parameter block is a non-persistent `StorageBuffer`, never a persistent one, and never
a hand-rolled global.**
*Rationale:* the per-renderer-slot frame-state rule (`Docs/RENDERER_FRAME_STATE.md`, ENG §3.7). A
**persistent** storage buffer is deliberately shared across renderer slots and frames, and a non-persistent
one is indexed by **(frame × recording renderer slot)** — verified directly:
`ShaderResources/API/Vulkan/VulkanStorageBuffer.hpp:36` ("*Indexed by (frame x recording renderer slot) — see
CopyIndex*") and `VulkanStorageBuffer.cpp:123,130` (`const uint32_t idx = m_Persistent ? 0u : CopyIndex();`).
If the cloud params lived in a persistent buffer, the Details mesh preview would overwrite the viewport's
clouds — the exact class of bug documented in `Docs/RENDERER_FRAME_STATE.md`.
Note also that `ComputePipeline` has **no** `SetUniformBuffer` (`Pipeline.hpp:157-179`), so an SSBO is the
only per-slot channel available to a compute dispatch; per-dispatch varying data (inverse view-projection,
camera position, target dimensions, frame index, jitter) goes in the 128-byte push constant.
*Acceptance:* `StorageBuffer::Create` call passes `persistent = false` — reviewable in one line; the
push-constant struct's `sizeof` is asserted ≤ 128 in a static_assert **and** in a unit test.

---

## 3. Engine prerequisites

These are deliverables of this programme, not assumptions. Each is real work with its own acceptance.

**CLD-10 — 3D images.** Add `Core::Formats::Image3DSpecification` (`Tag, Width, Height, Depth, Format,
Data, Properties`) and `Graphic::Image3D` + `Graphic::API::Vulkan::VulkanImage3D` using
`VK_IMAGE_TYPE_3D` / `VK_IMAGE_VIEW_TYPE_3D`, supporting `Storage | Sample`, single mip only.

> **CORRECTION (architect, during T3 review).** The specification originally carried a `Mips` field
> constrained to `Mips = 1`. The field is gone: the engine has no 3D mip generator, so a `Mips` argument
> could only ever be honoured at the value 1 — a setting the caller can set and the engine cannot obey is
> a dead setting, which DC §1.3 forbids. Raised by the T3 developer, who declined to add it.
>
> If volume mips are ever needed (a distant-LOD noise chain is the plausible reason), the generator comes
> first and the field comes with it.
*Rationale:* verified absent — `Image.hpp:15,46,70` declares only `Image`, `Image2D`, `ImageCube`;
`VK_IMAGE_TYPE_3D` appears only in commented-out vendored code (ENG §3.5).
*Acceptance:* creation of a 128³ `RGBA8F` `Storage|Sample` image succeeds and reports the correct byte size
(8 388 608); `Image::CalculateImageSize` gains a depth-aware overload and `GetBytesPerPixel` no longer
silently returns 0 for the formats we use (`Image.cpp:91-110`); a unit test covers the size arithmetic
(pure function, no GPU).

**CLD-11 — SPIR-V reflection must classify image resources by type, not by name.**
*Statement:* in `VulkanShader::Reflect`, replace the `resource.name.find("Env") || find("Cube")` heuristic
for `sampled_images` and the unconditional `StorageImage2DSamplers` funnel for `storage_images` with a
dispatch on `spirv_cross`' `type.image.dim` (`Dim2D` / `Dim3D` / `DimCube`), adding
`ShaderResources::Image3DSampler` and `StorageImage3D` reflection types and the matching descriptor-layout
entries.
*Rationale:* re-verified in the tree (`VulkanShader.cpp`, the `// Samplers` and `// Storage Images` loops):
a `sampler3D` named `u_ShapeNoise` would be registered as a **2D** combined image sampler and a
`writeonly image3D` as a 2D storage image — silently, with no error. This is the single most dangerous
latent trap in the whole programme.
*Acceptance (architect: the check must exist; its form is the developer's to choose):* a shader declaring
`sampler2D`, `sampler3D`, `samplerCube`, `image2D` and `image3D` simultaneously must be shown to reflect into
five distinct buckets. Preferred form is a unit test over the reflection data. Whether
`VulkanShader::Reflect()` can run without a `VkDevice` is **unverified** — if it cannot, the developer moves
the check into `DShaderTool` as a `--reflect` mode and **says so in the report**. What is not acceptable is
shipping without the check: a `sampler3D` silently bound as 2D does not crash, it quietly draws garbage, and
that is the most expensive failure mode in this programme.

**CLD-12 — `ComputePipeline` must accept 3D images.** `SetInput( binding, Image* )` and
`SetOutput( binding, Image*, mip )` already take `Image*`, so the change is in the Vulkan backend's
descriptor write and in the `ComputeImageBeginWrite`/`ComputeImageEndWrite` layout transitions, which
currently handle `Image2D` only (ENG §3.4).
*Acceptance:* the S0 noise-generation dispatch writes a 128³ volume and a subsequent dispatch samples it;
no validation error reported.

**CLD-13 — a depth-aspect layout transition helper, and the ability to bind the scene depth image as a
sampled input.**
*Statement:* `VulkanImage2D::TransitionLayout` and the `ComputeImageBeginWrite`/`EndWrite` pair currently
hardcode `VK_IMAGE_ASPECT_COLOR_BIT` (ENG §3.3, `VulkanImage.cpp:443-456`); the depth image's tracked layout
is `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` and `ComputePipeline::SetInput` binds the tracked layout verbatim
(`VulkanPipelineCompute.cpp:91-100`). Add an aspect-aware transition (the engine's depth format is
`DEPTH24STENCIL8`, `SceneRenderer.cpp:58`, so the aspect is `DEPTH | STENCIL`) and transition depth to
`SHADER_READ_ONLY_OPTIMAL` for the S2 dispatch and back afterwards.
*Rationale:* required by CLD-29, and it is the only way clouds are occluded by geometry in the Forward path,
which is the default. Architect decision: this is built, not worked around.
*Acceptance:* build; the layout-transition helper is exercised by a pure unit test over its
*aspect-mask selection* (format → aspect: `DEPTH24STENCIL8` → `DEPTH|STENCIL`, `DEPTH32F` → `DEPTH`,
colour formats → `COLOR`), which is the part that can be tested without a device; the user reports no
`VUID-VkDescriptorImageInfo-imageLayout` or aspect-mask validation warning. The runtime half **cannot be
verified locally** (no Vulkan, ENG §5.1) — called out in CLD-98.

**CLD-14 — noise-volume samplers must be created with explicit `LINEAR` filtering and `REPEAT` addressing,
independent of the global texture-filter setting.**
*Rationale:* `Utils::CreateSampler` derives filter and anisotropy from the Scene Settings texture filter via
`RenderConfig` (`VulkanImage.cpp:24-60`, ENG §3.5), and a filter change recreates every sampler
(`SceneRenderer.cpp:330-337`). A user selecting "Nearest" for texture quality would turn the clouds into
voxels. `REPEAT` is already the hardcoded address mode and is what tiling noise wants — no change needed
there.
*Acceptance:* the sampler creation path for 3D noise takes an explicit filter argument; a unit test is not
possible, so acceptance is code review plus an assertion in the creation function that the requested filter
is `LINEAR`.

**CLD-15 — the shader DSL must accept `sampler3D` uniforms and raw `layout(binding = N, rgba8) uniform
image3D` declarations.**
*Rationale:* the sugar table (`DShaderParser.cpp:806-846`, ENG §3.6) rewrites `Uniform(N) T name;`
textually, and storage-image format qualifiers are deliberately not sugared — both *probably* pass a
`sampler3D`/`image3D` through unchanged, but this is **unverified**.
*Acceptance:* a `DShaderParser` unit test in `Desert/Tests/Engine/DShaderParser/` asserting the emitted GLSL
for both declaration forms; CI's `DShaderTool Editor/Resources/Shaders` lint passes
(`.github/workflows/ci.yml:121-122`).

**CLD-16 — the cloud render system must be optional at `SceneRenderer::Init`: a failed `Initialize()` logs
`LOG_WARN` and the renderer continues.** *Rationale:* ENG §3.1 — core systems `DESERT_VERIFY(false)`,
optional ones warn (`SceneRenderer.cpp:157-158`); clouds must never take down a scene.
*Acceptance:* `Initialize()` returns `Common::BoolResultStr`; the `Init` call site branches to `LOG_WARN`.

**CLD-17 — the ECS system is registered only where clouds should render: `EditorLayer.cpp:615-633` and
`RuntimeLayer.cpp:81-94`, which must match exactly. It is NOT registered in `PreviewViewport.cpp:146` or
`AssetThumbnailRenderer.cpp:60`.**
*Rationale:* ENG §2.5 — those two build their own `SceneRenderer`; running a volumetric march per asset
thumbnail is unaffordable and pointless.
*Acceptance:* grep — the system type name appears in exactly two registration lists; a review diff of the two
lists shows the same order.

**CLD-18 — add `RGBA16F` to `Core::Formats::ImageFormat` and wire it through the format tables.**
*Statement:* a sixth entry in the enum (`ImageFormat.hpp:17-25`, today exactly five: `RGBA8F`, `RGBA32F`,
`BGRA8F`, `DEPTH24STENCIL8`, `DEPTH32F`), mapped to `VK_FORMAT_R16G16B16A16_SFLOAT`, added to
`Image::GetBytesPerPixel` (`Image.cpp:91-104`, which currently **returns 0** for anything but the two RGBA
formats). The cloud scatter target and both history images use it.

> **CORRECTION (architect, during T3 review).** This requirement said `RGBA16F` should be "gated on
> `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` at device selection **like the others**". There are no others:
> `IsFormatSupported` (`VulkanDevice.cpp:327`) has no callers at all, so no format in this engine is
> gated that way today. Adding an entry to the tables makes such a query *possible*; it does not make it
> existing practice. Introducing real device-feature gating is a separate piece of work with its own
> justification — not something to smuggle in as "like the others".
>
> Reported by the T3 developer, who checked the cited line instead of trusting the phrase.
*Rationale:* architect decision — half the history memory (CLD-34: 47.5 MiB instead of 95 MiB per live
renderer at Full), and we are already inside both the format tables and the image classes for CLD-10, so a
second visit to the same code costs more than doing it now. **Precision:** `RGBA16F` carries radiance and
transmittance. Transmittance is in `[0,1]` where half has ~3 decimal digits — far more than the
`0.005` early-out threshold needs. Radiance is pre-tonemap HDR; half's maximum is 65504 and its relative
precision is ~0.05 %, which is below the quantisation the tonemap and the 8-bit swapchain apply downstream.
If a case is found where 16F is not enough, the requirement is to **name the specific quantity and the
observed symptom**, not to widen the format globally.
*Acceptance:* `GetBytesPerPixel( RGBA16F ) == 8` and the 2D/3D size arithmetic agree — unit test, no GPU;
`ImageFormat` gains exactly one entry and every `switch` over it in the engine is revisited, enforced by
**CLD-18a** rather than by a warning flag; a `RGBA16F` `Storage|Sample` image is created successfully at
runtime (user-reported, CLD-98).

**CLD-18a — `Image::GetBytesPerPixel` must be total over `ImageFormat`, and must not have a silent
fallback. The unknown-format case fails loudly; incompleteness is caught by the compiler, not by a warning
flag.**
*Statement:* three changes to `Graphic/Image.cpp:91-104`:
1. Every enumerator of `ImageFormat` gets an explicit `case` returning its real size — `RGBA8F` 4, **`RGBA16F` 8**, `RGBA32F` 16, `BGRA8F` 4, `DEPTH24STENCIL8` 4, `DEPTH32F` 4. The function becomes total; no enumerator is left to a fallback.
2. **The trailing `return 0U;` after the switch is deleted**, and exhaustiveness is enforced by the LANGUAGE, not by a warning flag.

> **CORRECTION (architect, during T3 review).** This requirement originally reasoned that the `switch` has
> no `default:` label "so `-Wswitch` *would* fire". **It would not.** The workspace compiles with
> `warnings "Off"` (`BuildScripts/Workspace.lua:22`), which reaches the compiler as `-w` — and `-w`
> defeats `-Wswitch` *and* any `#pragma GCC diagnostic error` placed around it. The T3 developer tested
> this rather than assuming it, and built the guarantee out of the language instead: the lookups are
> `constexpr`, and a `static_assert` constant-evaluates every one of them for every enumerator, so a
> format without a case falls off the end of a constant expression — ill-formed, hence a hard build
> failure. Verified independently by the architect: adding a seventh enumerator fails the build with
> exit 2 at that `static_assert`.
>
> The general lesson, worth more than the fix: **a safety net whose mechanism has not been tested is a
> belief, not a net.** This one was believed by both the analyst and the architect.
>
> Consequence for reviewers: "no new compiler warnings" is not a meaningful acceptance criterion in this
> repository while `-w` is global. Do not report it as one.
3. The remaining unreachable path — a value outside the enum, e.g. a corrupted or cast integer — logs `LOG_ERROR` with the numeric value and the caller's context and returns via the engine's failure convention, never `0`.
Same treatment for `CalculateImageSize` (`:106-110`) and its new depth-aware overload (CLD-10), which
multiply by whatever `GetBytesPerPixel` returned and therefore inherit the zero.
*Rationale:* architect decision, and it is the contract's own rule: a silent fallback that "does not crash,
it just gives the wrong number" (`DEV_CONTRACT.md:33-36`). A bytes-per-pixel of 0 produces an allocation size
of 0 for a real image — the failure surfaces later as corrupted memory, in a place with no connection to the
format that was added. We are adding an enumerator in CLD-18, which is exactly the moment this trap arms.
The fix must not depend on warning flags being enabled in whichever configuration someone happens to build.
*Acceptance:* unit test (pure, no GPU) asserting the exact byte count for all six enumerators, so a future
seventh format that is added without a `case` fails to compile rather than failing the test; a grep confirms
no `return 0` remains in either function; `CalculateImageSize( 128, 128, RGBA16F ) == 131072` and the
3D overload `128×128×128 RGBA8F == 8388608` (CLD-95).

**CLD-19 — `RenderGraphBuilder` must produce a deterministic pass order within a phase, tie-broken by
registration order.**
*Statement:* `TopologicalSort` currently runs Kahn's algorithm over **phases**, and passes inside a phase come
out in whatever order the container yields (`RenderGraphBuilder.cpp:109-181`, ENG §3.1) — which is why
`MeshRenderer::UpdateCascades()` had to be hoisted out of the graph entirely
(`SceneRenderer.cpp:370-375`). Each `PassConfig` gains a monotonically increasing registration sequence
number assigned in `AddPass`, and the sort orders `(phase, sequence)`.
*Rationale:* architect decision — **fix the primitive, do not route around it.** A second pass registered in
any phase, by anyone, today gets an undefined draw order and finds out visually; routing clouds around the
graph would leave that trap in place *and* add another bypass path to a function that already has eighteen
steps. This is a separate task, owned by the render-graph developer, and it is a **blocking dependency** of
CLD-21a.
*Acceptance:* a unit test on the sorter alone (pure, no GPU — the builder's sort is separable): N passes
across several phases, registered in a shuffled order, produce output ordered by phase then by registration;
building the same graph twice yields identical output; and the specific case of CLD-21a — clouds registered
before particles, both in `Transparency` — is asserted explicitly.

**Deliberately NOT taken as prerequisites:** `R8`/`R16F` `ImageFormat` entries (N7); 3D mip generation
(CLD-30); async compute (ENG §3.4 — there is none, and we do not need it); a bindless/descriptor-array path;
a motion-vector target (CLD-32a constraint 1).

---

## 4. Component specification — `VolumetricCloudsComponent`

### 4.1 Shape and registration

**CLD-40 — the component uses the `Data`-block shape:**
```cpp
struct VolumetricCloudData { REFLECT() /* 95 PROPERTY fields */ };
struct VolumetricCloudsComponent { VolumetricCloudData Data; bool RequestRegenerateNoise = false; };
```
*Rationale:* ENG §2.1 — Lua binding (`ReflectionBindings.cpp:36-52`) hard-assumes a `.Data` sub-member, and
the one-line editor registration `DESERT_REGISTER_REFLECTED_COMPONENT` requires it. `RequestRegenerateNoise`
carries **no** `PROPERTY`, so it stays transient exactly as `SkyboxComponent::RequestBake` does
(`Components.hpp:1505-1506`).
*Acceptance:* serialization round-trip test through `ComponentRegistry` (model:
`Desert/Tests/Engine/ReflectionSerializer/`) — every reflected field survives save→load; `RequestRegenerateNoise`
does not appear in the JSON.

**CLD-41 — registration touchpoints, all of which must be done:**
1. `Core/Serialize/ComponentRegistry.cpp` — one `MakeReflected<…>( "VolumetricClouds", "VolumetricCloudData", &…::Data )` line (ENG §2.8 step 3). *Skipping it silently kills save/load, duplicate and undo.*
2. `Editor/.../ComponentEditorRegistrations.cpp:46-90` — one `DESERT_REGISTER_REFLECTED_COMPONENT` line **or** a custom `ComponentWidgets/VolumetricCloudsComponent.cpp` (needed for the preset row, CLD-52).
3. `Editor/.../ComponentWidgetRegistry.cpp:48-69` — add the display name to `CategoryOf()` → `"Rendering"`, or it lands in `"Other"`.
4. `Desert/Desert/Source/Engine/Generated/Reflection.gen.cpp` — regenerated by the prebuild step and **committed**; never hand-edited (ENG §2.3, §5.4).
5. `Scripting/ReflectionBindings.cpp:55-64` — add to `kReflectedComponents[]` (free, given the `.Data` shape).
*Acceptance:* the round-trip test in CLD-40 fails if (1) is missing; the Add-Component menu entry is a
consequence of (2) with no extra list to maintain (`ComponentWidgetRegistry.cpp:72-128`).

**CLD-42 — annotations.** Every field carries `PROPERTY( DisplayName(…), Category(…), Tooltip(…) )`.
Numeric fields carry `Range(min,max)`. **Every distance carries `Length`** (`Docs/UNITS.md:28-39`) so the
Details panel drags it in 1 cm steps. Colours carry `Color`. Fields that only matter when another is on carry
`EditCondition("…")` (precedent `Components.hpp:238`). Rarely-touched fields carry `Advanced` so they fold
away (`PropertyEditorBuilder.cpp:319-334`).
*Rationale:* the panel is entirely annotation-driven — there are no hand-written cloud sliders anywhere
(ENG §1.4, `SkyboxComponent.cpp:78-81`). A missing `Range` silently becomes a `DragFloat`
(`PropertyEditorBuilder.cpp:531-533`).
*Acceptance:* a unit test walks the reflected field list of `VolumetricCloudData` and asserts: every field has
a non-empty `Tooltip`; every `Float`/`Int` field has `HasRange`; every field whose name ends in
`Altitude|Thickness|Distance|TileSize|Size|Speed` has `Length` or `Units`. This is a real, automatable check
over `Reflection.gen.cpp` metadata.

**CLD-43 — the flat cloud path is deleted in the same change that replaces it.**
`Editor/Resources/Shaders/Common/Clouds.glslh`, `Graphic/CloudSettings.hpp`, the `RenderClouds` call and the
cloud fields of `SkyUB` in `ProceduralSky.shader`, the cloud members of `MaterialProceduralSky::SkyUBData`,
the `Clouds` field of `ProceduralSkyCommand`, the `CloudSettings` parameter of
`SceneRenderer::SetProceduralSky` (**three** call sites: `ProceduralSkyCommand.hpp:32`,
`PreviewViewport.cpp:177-178`, `AssetThumbnailRenderer.cpp:97`) and the six `Cloud*` fields of
`SkyboxComponent` all go. No flag, no `#ifdef`, no second path (`DEV_CONTRACT.md:139-146`).
*Rationale + ownership note:* the `SkyboxComponent` field removal and the scene migration belong to Analyst
A's task; this requirement records the dependency so the two tasks are sequenced, not so that B edits A's
files (`DEV_CONTRACT.md:39-41`).
*Acceptance:* grep for `CloudCoverage`, `CloudTiling`, `RenderClouds`, `CloudSettings` returns nothing outside
this document; the build succeeds.

### 4.2 Field table

**Legend.** *Pre* = preset-driven (§5). *Q* = quality-driven (§6). `M(x)` = `Common::Units::Metres(x)`.
Defaults are the **Partly Cloudy** preset (CLD-51). Group = `Category(...)` string.

#### Group `Cloud Layer` — 6 fields

| Field | Type | Unit | Range | Default | Visual effect | Pre | Q |
|---|---|---|---|---|---|---|---|
| `Enabled` | `bool` | — | — | `true` | Master switch; gates every other row via `EditCondition`. | — | — |
| `LayerBottomAltitude` | `float` | cm `Length` | `0 … M(20000)` | `M(1500)` | Cloud base height above the planet surface. | ✔ | — |
| `LayerThickness` | `float` | cm `Length` | `M(50) … M(15000)` | `M(1608.9)` | Vertical extent of the shell. **Derived**, not chosen: `WeatherTileSize / 8 / aspect`, where the aspect is the preset's species (`Engine/Graphic/Clouds/CloudLayerAspect.hpp`). A cloud is wider than it is tall; only a cumulonimbus is not. | ✔ | — |
| `MaxViewDistance` | `float` | cm `Length` | `M(5000) … M(400000)` | `M(150000)` | How far along the ray clouds are marched at all. | — | — |
| `HorizonFadeStart` | `float` | cm `Length` | `0 … M(400000)` | `M(60000)` | Distance where clouds start dissolving into the sky. | ✔ | — |
| `HorizonFadeEnd` | `float` | cm `Length` | `0 … M(400000)` | `M(140000)` | Distance where they are fully gone. | ✔ | — |

#### Group `Weather` — 10 fields

| Field | Type | Unit | Range | Default | Visual effect | Pre | Q |
|---|---|---|---|---|---|---|---|
| `Coverage` | `float` | — | `0 … 1` | `0.50` | Fraction of sky filled. The single biggest look knob. | ✔ | — |
| `CoverageContrast` | `float` | — | `0.2 … 4` | `1.20` | Hard-edged islands (high) vs soft blanket (low). | ✔ | — |
| `WeatherTileSize` | `float` | cm `Length` | `M(5000) … M(400000)` | `M(60000)` | World size of one weather-map tile = size of a cloud *system*. | ✔ | — |
| `WeatherSeed` | `int` | — | `0 … 65535` | `1337` | Reshuffles the whole cloudscape layout. | ✔ | — |
| `WeatherOctaves` | `int` | — | `1 … 8` | `5` | Detail richness of the coverage field. | ✔ | — |
| `WeatherWarpStrength` | `float` | — | `0 … 1` | `0.45` | Domain warp — turns circular blobs into organic fronts. | ✔ | — |
| `CloudType` | `float` | — | `0 … 1` | `0.60` | 0 = flat stratus, 0.5 = stratocumulus, 1 = towering cumulus. | ✔ | — |
| `CloudTypeVariance` | `float` | — | `0 … 1` | `0.45` | How much type varies across the map vs one uniform type. | ✔ | — |
| `AnvilBias` | `float` | — | `0 … 1` | `0.10` | Spreads cloud tops outward — the cumulonimbus anvil. | ✔ | — |
| `Wetness` | `float` | — | `0 … 1` | `0.15` | Rain-laden look: darker, denser bases. | ✔ | — |

#### Group `Shape` — 11 fields

| Field | Type | Unit | Range | Default | Visual effect | Pre | Q |
|---|---|---|---|---|---|---|---|
| `ShapeTileSize` | `float` | cm `Length` | `M(2000) … M(200000)` | `M(35000)` | World size of one base-noise tile = size of an individual cloud. | ✔ | — |
| `ShapeSeed` | `int` | — | `0 … 65535` | `7` | Regenerates the shape volume (`RequestRegenerateNoise`). | ✔ | — |
| `BaseShapeRemapMin` | `float` | — | `0 … 0.9` | `0.30` | Threshold below which base noise is empty; raises = sparser, crisper. | ✔ | — |
| `ShapeErosionStrength` | `float` | — | `0 … 1` | `0.65` | How much the FBM octaves eat into the base blob. | ✔ | — |
| `ExtinctionScale` | `float` | — | `0.01 … 8` | `1.00` | Optical density. The most important single number in the system. | ✔ | — |
| `StratusGradient` | `glm::vec4` | normalized height | each `0 … 1` | `(0, 0.08, 0.20, 0.32)` | Base-in start/end, top-out start/end for the stratus profile. | ✔ | — |
| `StratocumulusGradient` | `glm::vec4` | normalized height | each `0 … 1` | `(0, 0.18, 0.55, 0.78)` | Same for stratocumulus. | ✔ | — |
| `CumulusGradient` | `glm::vec4` | normalized height | each `0 … 1` | `(0, 0.22, 0.68, 0.92)` | Same for cumulus. | ✔ | — |
| `BaseGradientPower` | `float` | — | `0.5 … 6` | `2.00` | Flatness of the cloud bottom (envelope method, REF §C.7). | ✔ | — |
| `TopGradientPower` | `float` | — | `0.5 … 6` | `1.50` | Roundness of the cloud top. | ✔ | — |
| `DensityHeightBias` | `float` | — | `0 … 2` | `0.70` | Density increase with altitude in the layer — heavy tops. | ✔ | — |

#### Group `Detail` — 22 fields

| Field | Type | Unit | Range | Default | Visual effect | Pre | Q |
|---|---|---|---|---|---|---|---|
| `DetailStrength` | `float` | — | `0 … 1` | `0.38` | Overall erosion by the detail volume — cauliflower vs smooth. | ✔ | — |
| `DetailTileSize` | `float` | cm `Length` | `M(200) … M(30000)` | `M(4000)` | World size of one detail tile = size of a lobe. | ✔ | — |
| `DetailSeed` | `int` | — | `0 … 65535` | `13` | Regenerates the detail volume. | ✔ | — |
| `DetailTypeBias` | `float` | — | `0 … 1` | `0.50` | 0 = wispy erosion, 1 = billowy erosion (REF §C.4 step 3c). | ✔ | — |
| `BillowGradientPower` | `float` | — | `0.05 … 2` | `0.25` | How fast billow takes over from wisp as density rises (REF #35). | ✔ | — |
| `BillowNoiseScale` | `float` | — | `0 … 1` | `0.30` | Strength of the Alligator (billow) channels (REF #36). | ✔ | — |
| `HighFreqStrength` | `float` | — | `0 … 1` | `0.50` | The extra close-range detail layer (REF §C.4 step 3d). | ✔ | — |
| `HighFreqWispSharpness` | `float` | — | `1 … 10` | `4.00` | Ridge sharpness of the high-frequency wisps (REF #37). | ✔ | — |
| `HighFreqBillowSharpness` | `float` | — | `1 … 10` | `2.00` | Sharpness of the high-frequency billows (REF #38). | ✔ | — |
| `HighFreqFadeStart` | `float` | cm `Length` | `0 … M(50000)` | `M(2500)` | Distance where the high-frequency layer starts fading. | ✔ | — |
| `HighFreqFadeEnd` | `float` | cm `Length` | `0 … M(50000)` | `M(9000)` | Distance where it is gone. | ✔ | — |
| `CurlStrength` | `float` | — | `0 … 1` | `0.35` | Curl-noise warp of the detail lookup — swirls, turbulent edges. | ✔ | — |
| `CurlTileSize` | `float` | cm `Length` | `M(500) … M(60000)` | `M(9000)` | Size of one curl swirl. | ✔ | — |
| `DensitySharpenLow` | `float` | — | `0.05 … 2` | `0.30` | Contrast exponent applied to thin regions (REF #44). | ✔ | — |
| `DensitySharpenHigh` | `float` | — | `0.05 … 2` | `0.60` | Same for dense regions (REF #45). | ✔ | — |
| `DensityScalePower` | `float` | — | `1 … 8` | `4.00` | Contrast of the density-scale channel (REF #43). | ✔ | — |
| `DistanceSoftening` | `float` | — | `0 … 1` | `0.60` | Blur/attenuate detail with distance to kill aliasing (REF #46-49). | ✔ | — |
| `SofteningStartDistance` | `float` | cm `Length` | `0 … M(200000)` | `M(8000)` | Where softening begins. | ✔ | — |
| `SofteningEndDistance` | `float` | cm `Length` | `0 … M(200000)` | `M(45000)` | Where it is at full strength. | ✔ | — |
| `NearFadeStart` | `float` | cm `Length` | `0 … M(5000)` | `M(50)` | Density fade-in right in front of the camera. | ✔ | — |
| `NearFadeEnd` | `float` | cm `Length` | `0 … M(5000)` | `M(900)` | Where density is back to 100 %. | ✔ | — |
| `NearFadeMinDensity` | `float` | — | `0 … 1` | `0.25` | Density fraction at `NearFadeStart` — stops the screen filling when you fly through. | ✔ | — |

#### Group `Lighting` — 23 fields

| Field | Type | Unit | Range | Default | Visual effect | Pre | Q |
|---|---|---|---|---|---|---|---|
| `ScatteringAlbedo` | `glm::vec3` `Color` | linear | `0 … 1` per ch. | `(1, 1, 1)` | Single-scattering albedo; below 1 makes clouds dirty/grey. | ✔ | — |
| `ExtinctionTint` | `glm::vec3` `Color` | linear | `0 … 1` per ch. | `(1, 1, 1)` | Per-channel extinction — subtle colour shift through thickness. | ✔ | — |
| `LightMarchDistance` | `float` | cm `Length` | `M(50) … M(6000)` | `M(1000)` | How far the shadow ray reaches toward the sun. Long = deeper shadows. | ✔ | — |
| `LightConeSpread` | `float` | — | `0 … 1` | `0.35` | Cone half-width of the shadow samples — softens self-shadowing. | ✔ | — |
| `PhaseForwardG` | `float` | — | `0 … 0.99` | `0.80` | Forward HG lobe — the silver-lining peak around the sun. | ✔ | — |
| `PhaseBackwardG` | `float` | — | `-0.99 … 0` | `-0.15` | Backward lobe — brightness looking away from the sun. | ✔ | — |
| `PhaseBlend` | `float` | — | `0 … 1` | `0.50` | Mix between the two lobes. | ✔ | — |
| `SilverLiningIntensity` | `float` | — | `0 … 4` | `1.20` | Multiplier on the forward lobe only — pushes the rim glow. | ✔ | — |
| `PowderStrength` | `float` | — | `0 … 1` | `0.50` | Beer-powder dark-edge effect on cloud rims. | ✔ | — |
| `PowderScale` | `float` | — | `0.1 … 10` | `2.00` | Depth over which powder darkening acts. | ✔ | — |
| `MultiScatterExtinctionFalloff` | `float` | — | `0.05 … 1` | `0.50` | Per-octave extinction decay for the multi-scatter approximation. | ✔ | — |
| `MultiScatterScatterFalloff` | `float` | — | `0.05 … 1` | `0.50` | Per-octave scattering decay. | ✔ | — |
| `MultiScatterPhaseFalloff` | `float` | — | `0.05 … 1` | `0.50` | Per-octave phase-anisotropy decay (later octaves flatten). | ✔ | — |
| `AmbientSkyContribution` | `float` | — | `0 … 3` | `1.00` | Multiplier on the sky ambient term supplied by §7. | ✔ | — |
| `AmbientGroundContribution` | `float` | — | `0 … 3` | `0.25` | Multiplier on the ground-bounce ambient term. | ✔ | — |
| `AmbientHeightBias` | `float` | — | `0 … 1` | `0.50` | How much ambient favours cloud tops over bases. | ✔ | — |
| `SunLightIntensityScale` | `float` | — | `0 … 4` | `1.00` | Multiplier on the sun radiance from §7 — clouds only. | ✔ | — |
| `SunTint` | `glm::vec3` `Color` | linear | `0 … 1` per ch. | `(1, 1, 1)` | Artistic tint of the lit side (REF #74 as an override, not a replacement). | ✔ | — |
| `ShadowTint` | `glm::vec3` `Color` | linear | `0 … 1` per ch. | `(1, 1, 1)` | Artistic tint of the shadowed side (REF #72). | ✔ | — |
| `PrecipitationDarkening` | `float` | — | `0 … 1` | `0.50` | How much `Wetness` darkens the base. | ✔ | — |
| `AtmosphericPerspective` | `float` | — | `0 … 1` | `0.80` | How much sky colour bleeds into distant clouds. | ✔ | — |
| `DistanceFadeStart` | `float` | cm `Length` | `0 … M(400000)` | `M(50000)` | Where atmospheric blending begins. | ✔ | — |
| `DistanceFadeEnd` | `float` | cm `Length` | `0 … M(400000)` | `M(140000)` | Where clouds are fully the sky colour. | ✔ | — |

#### Group `Animation` — 8 fields

| Field | Type | Unit | Range | Default | Visual effect | Pre | Q |
|---|---|---|---|---|---|---|---|
| `AnimationSpeed` | `float` | — | `0 … 5` | `1.00` | Global time multiplier; 0 freezes the sky. | ✔ | — |
| `WindInfluence` | `float` | — | `0 … 3` | `1.00` | Scale on the scene-global wind (`SceneRenderer::GetWind()`). Never a second wind. | ✔ | — |
| `WindDirectionOffset` | `float` | degrees | `-180 … 180` | `0.00` | Rotates cloud drift relative to the grass/foliage wind. | ✔ | — |
| `ShapeScrollMultiplier` | `float` | — | `0 … 5` | `1.00` | Speed of the base shape lookup drift. | ✔ | — |
| `DetailScrollMultiplier` | `float` | — | `0 … 8` | `2.00` | Detail drifts faster than shape — the classic "boiling" look. | ✔ | — |
| `WeatherScrollMultiplier` | `float` | — | `0 … 3` | `0.35` | Speed at which whole cloud systems move across the sky. | ✔ | — |
| `WindHeightShear` | `float` | — | `0 … 1` | `0.30` | Higher altitudes drift faster — leaning, sheared clouds (REF #152). | ✔ | — |
| `WindUpliftSpeed` | `float` | cm/s `Length` | `0 … M(60)` | `M(4)` | Vertical drift of the detail lookup — convective churn. | ✔ | — |

#### Group `Quality` — 14 fields

| Field | Type | Unit | Range | Default | Visual effect | Pre | Q |
|---|---|---|---|---|---|---|---|
| `QualityLevel` | `enum CloudQuality` | — | `Low\|Medium\|High\|Ultra\|Custom` | `High` | Selects the tier; anything but `Custom` overwrites the 13 below. | ✗ | — |
| `ResolutionScale` | `enum CloudResolutionScale` | — | `Quarter\|Half\|Full` | `Half` | Raymarch buffer size relative to the target. | ✗ | ✔ |
| `MaxSteps` | `int` | — | `8 … 512` | `128` | Hard iteration cap of the view march. | ✗ | ✔ |
| `MinStepSize` | `float` | cm `Length` | `M(1) … M(500)` | `M(15)` | Finest step inside cloud. | ✗ | ✔ |
| `MaxStepSize` | `float` | cm `Length` | `M(50) … M(5000)` | `M(700)` | Coarsest step far away. | ✗ | ✔ |
| `StepGrowthRate` | `float` | — | `0 … 0.1` | `0.008` | How fast steps grow with distance. | ✗ | ✔ |
| `CoarseStepMultiplier` | `float` | — | `1 … 8` | `3.00` | Stride multiplier while skipping empty space. | ✗ | ✔ |
| `EmptySamplesBeforeCoarse` | `int` | — | `1 … 32` | `8` | Consecutive empty fine samples before returning to coarse. | ✗ | ✔ |
| `LightMarchSamples` | `int` | — | `1 … 16` | `6` | Shadow-ray samples per shaded sample. | ✗ | ✔ |
| `MultiScatterOctaves` | `int` | — | `1 … 4` | `2` | Multiple-scattering octaves (arithmetic only, no extra rays). | ✗ | ✔ |
| `TemporalMode` | `enum CloudTemporalMode` | — | `Off\|Reprojection` | `Reprojection` | Temporal accumulation on/off. | ✗ | ✔ |
| `TemporalBlendFactor` | `float` | — | `0.02 … 1` | `0.10` | Weight of the current frame; low = smoother, more ghosting. | ✗ | ✔ |
| `TemporalClampScale` | `float` | — | `0.5 … 4` | `1.50` | Neighbourhood-clamp box width; low = less ghosting, more flicker. | ✗ | ✔ |
| `JitterStrength` | `float` | — | `0 … 1` | `1.00` | Per-pixel ray-origin dither. | ✗ | ✔ |

#### Group `Preset` — 1 field

| Field | Type | Range | Default | Behaviour | Pre | Q |
|---|---|---|---|---|---|---|
| `Preset` | `enum CloudPreset` | `Custom\|Clear\|FairWeather\|PartlyCloudy\|Stratus\|Overcast\|Storm\|Cirrus` | `PartlyCloudy` | Selecting a value applies §5; editing any preset-driven field sets it to `Custom`. | — | — |

**Total: 95 exposed fields.** 78 preset-driven, 13 quality-driven, 4 neither
(`Enabled`, `MaxViewDistance`, `QualityLevel`, `Preset`).

### 4.3 Reference constants deliberately NOT exposed

The client asked for "all possible settings", so this list is short and each entry has a reason.

| REF # | Constant | Why not exposed |
|---|---|---|
| 2, 17 | `cloud_type` NVDF selector, modeling texture dims | The baked-NVDF path is out of v1 (N1). |
| 5 | `farclip` | Replaced by `MaxViewDistance`, same knob, better name/unit. |
| 6 | `transmittance_limit` | Fixed at `0.005`. In the reference it was ineffective (REF §J.3 #5); once correct, a user-facing value only trades a barely-visible haze for cost, and that is what the quality tier is for. |
| 7, 8, 119-125 | god-ray toggle, exposure, decay, density, weight, sample count, horizon cutoff | Post-process, not clouds (N5). |
| 9, 84-111 | every Preetham/sky constant | Analyst A's component. |
| 12, 135-147 | camera/window/mouse/anisotropy/quad-depth | Not cloud parameters. |
| 13, 29-34 | dead selectors and commented-out defines | Dead in the reference; exposing a dead knob is the anti-pattern this document exists to prevent (`DEV_CONTRACT.md:29-32`). |
| 14-16 | `VOXEL_BOUND_MIN/MAX`, SDF remap range | Box geometry replaced by the shell (CLD-23). |
| 19, 20, 81-83 | light-grid dimensions and march step | The light grid is not in v1 (N9). |
| 21 | `WORKGROUP_SIZE` | An implementation constant tuned once against the device, not an artistic choice. Fixed at 8×8 (REF §H.2 #21 notes 32×32 sits at the Vulkan minimum guarantee). |
| 22 | `EPSILON` | Overloaded for two unrelated purposes in the reference; we use two named local constants, neither user-facing. |
| 23 | `DENSITY_SCALE` | Never referenced in the reference; its intended role is `ExtinctionScale`, which **is** exposed. |
| 91, 95 | `numMolecules`, `refractiveIndex` | Physical constants. |
| 112-118 | star field | Analyst A's. |
| 126-131 | tonemap/vignette | Engine post chain owns these (CLD-31). |
| 132-134 | day length, sun distance, azimuth | The sun is the directional-light entity (`SceneSettings.hpp:209-211`); Analyst A's. |
| 148, 149 | `ATMOSPHERE_RADIUS`, outer shell factor | `PlanetRadius` is owned by `SkyAtmosphereComponent` (§7); duplicating it would violate the single-source-of-truth rule (`DEV_CONTRACT.md:56-58`). |
| 156-164 | Nubis2 UV scales as raw frequencies | Exposed instead as **tile sizes in world units**, which is the same information in a unit an artist can reason about. |
| 165 | the six hardcoded cone offsets | Replaced by `LightConeSpread` + `LightMarchSamples`; six hand-tuned magic vectors are not an artist-facing control. |
| 172 | density saturation cutoff `0.999` | Subsumed by the transmittance early-out. |
| 174, 175 | cloud base colour, ambient contribution | Replaced by the physically-derived sky ambient (§7) with `SunTint`/`ShadowTint` as artistic overrides. |

---

## 5. Presets

**CLD-50 — mechanism.** A single `constexpr` table in one header, `Graphic/CloudPresets.hpp`:

```cpp
struct CloudPresetValues { /* exactly the 79 preset-driven fields, same names, same types */ };
struct CloudPresetEntry  { ECS::CloudPreset Id; const char* Name; CloudPresetValues Values; };
inline constexpr CloudPresetEntry kCloudPresets[] = { … };          // one entry per preset

void            ApplyPreset( ECS::CloudPreset, ECS::VolumetricCloudData& );   // pure
ECS::CloudPreset MatchPreset( const ECS::VolumetricCloudData& );              // pure, Custom if none
```
`ApplyPreset` writes **only** the members of `CloudPresetValues`, so the quality fields are *structurally*
unreachable from a preset — not "by convention", by type.
*Rationale:* the engine has no generic preset system; every preset in the tree is a C++ table
(`ParticleEditorPanel.cpp:27-31, :124-127`, ENG §1.4). A `constexpr` table of *values* (not of function
pointers) keeps the mechanism free of content: adding a preset is **one enum value + one table entry, in one
file** — the litmus test the engine's own conventions apply to this exact situation
(`SKILL.md:202-218`, quoted ENG §4.8).
*Acceptance:* a unit test asserts `std::size(kCloudPresets)` equals the number of non-`Custom` enum values
and that every enum value appears exactly once — so a new enum value without a table entry fails the build's
test run rather than silently doing nothing.

**CLD-51 — the component's field defaults equal the `PartlyCloudy` entry.**
*Acceptance:* unit test — `VolumetricCloudData d{}; ApplyPreset( PartlyCloudy, d );` leaves `d`
byte-identical to a default-constructed instance.

**CLD-52 — behaviour on apply and on edit.**
1. Selecting a preset in the Details combo applies it through `Commands::MutateEntityUndoable`, so one Ctrl+Z restores the previous values (ENG §2.7 — undo serializes the component, so this is free).
2. **User edits are not preserved.** Applying a preset overwrites all 78 fields. The undo entry is the safety net; a "merge" semantic would be unpredictable.
3. Editing any preset-driven field afterwards sets `Preset = Custom`, computed by `MatchPreset` on the edited data — so returning the values by hand restores the preset name.
4. `Preset` **is persisted** — it is a reflected enum field and round-trips through the normal serializer (enum reflection is verified end-to-end: `Reflection.gen.cpp:26` shows `FieldType::Enum` with `EnumValues`, and `PropertyEditorBuilder.cpp:880-902` draws it as a combo and writes it back).
5. Presets are **never re-applied on load.** The serialized field values are the truth; loading only reads `Preset` as a label. A preset table edited in a later engine version therefore cannot silently change an authored scene.
*Acceptance:* unit tests — `ApplyPreset` then `MatchPreset` round-trips for all 7 presets; perturbing any one
preset-driven field makes `MatchPreset` return `Custom`; perturbing any *quality* field leaves `MatchPreset`
unchanged.

**CLD-53 — presets must not touch quality fields.**
*Acceptance:* unit test — for each of the 7 presets, capture the 13 quality fields + `QualityLevel`, apply,
compare; any difference fails. (Given CLD-50's type split this cannot compile wrong, but the test guards a
future refactor that flattens the structs.)

### 5.1 Preset value table

All 78 preset-driven fields × 7 presets. `M(x)` = `Common::Units::Metres(x)`.

`LayerThickness` is **derived** from the row's own `WeatherTileSize` and its species aspect:
`thickness = (WeatherTileSize / 8) / aspect` — see `Engine/Graphic/Clouds/CloudLayerAspect.hpp` for the
relation, the per-species ranges and what each preset's number is. The whole fair-weather family was
originally authored at aspects of 0.78–1.01, i.e. clouds taller than they were wide, which is the
cumulonimbus proportion and is what made a deck overhead read as a ceiling. Stratus, Storm and Cirrus
were already correct for their species and are unchanged. The `Coverage` row and several others below
predate the shipped table and are historical.

| Field | Clear | FairWeather | **PartlyCloudy** (default) | Stratus | Overcast | Storm | Cirrus |
|---|---|---|---|---|---|---|---|
| `LayerBottomAltitude` | `M(2000)` | `M(1500)` | `M(1500)` | `M(600)` | `M(900)` | `M(700)` | `M(8000)` |
| `LayerThickness` (derived — see below) | `M(1526.4)` | `M(1481.5)` | `M(1608.9)` | `M(700)` | `M(1409)` | `M(9000)` | `M(1200)` |
| `HorizonFadeStart` | `M(60000)` | `M(60000)` | `M(60000)` | `M(45000)` | `M(50000)` | `M(55000)` | `M(90000)` |
| `HorizonFadeEnd` | `M(140000)` | `M(140000)` | `M(140000)` | `M(110000)` | `M(120000)` | `M(130000)` | `M(200000)` |
| `Coverage` | `0.06` | `0.28` | `0.50` | `0.78` | `0.90` | `0.96` | `0.35` |
| `CoverageContrast` | `2.20` | `1.60` | `1.20` | `0.70` | `0.60` | `0.80` | `1.90` |
| `WeatherTileSize` | `M(60000)` | `M(50000)` | `M(60000)` | `M(90000)` | `M(120000)` | `M(70000)` | `M(140000)` |
| `WeatherSeed` | `1337` | `1337` | `1337` | `1337` | `1337` | `1337` | `1337` |
| `WeatherOctaves` | `5` | `5` | `5` | `4` | `4` | `6` | `6` |
| `WeatherWarpStrength` | `0.35` | `0.40` | `0.45` | `0.20` | `0.25` | `0.60` | `0.50` |
| `CloudType` | `0.75` | `0.70` | `0.60` | `0.05` | `0.25` | `0.90` | `0.15` |
| `CloudTypeVariance` | `0.25` | `0.35` | `0.45` | `0.10` | `0.20` | `0.50` | `0.30` |
| `AnvilBias` | `0.00` | `0.00` | `0.10` | `0.00` | `0.05` | `0.75` | `0.00` |
| `Wetness` | `0.00` | `0.05` | `0.15` | `0.50` | `0.60` | `1.00` | `0.00` |
| `ShapeTileSize` | `M(35000)` | `M(35000)` | `M(35000)` | `M(60000)` | `M(50000)` | `M(30000)` | `M(70000)` |
| `ShapeSeed` | `7` | `7` | `7` | `7` | `7` | `7` | `7` |
| `BaseShapeRemapMin` | `0.42` | `0.36` | `0.30` | `0.18` | `0.20` | `0.24` | `0.44` |
| `ShapeErosionStrength` | `0.55` | `0.60` | `0.65` | `0.35` | `0.40` | `0.70` | `0.80` |
| `ExtinctionScale` | `0.70` | `0.85` | `1.00` | `1.10` | `1.30` | `2.00` | `0.35` |
| `StratusGradient` | `(0, .08, .20, .32)` | `(0, .08, .20, .32)` | `(0, .08, .20, .32)` | `(0, .06, .24, .40)` | `(0, .08, .22, .36)` | `(0, .08, .20, .32)` | `(0, .05, .30, .55)` |
| `StratocumulusGradient` | `(0, .18, .55, .78)` | `(0, .18, .55, .78)` | `(0, .18, .55, .78)` | `(0, .16, .50, .70)` | `(0, .18, .58, .82)` | `(0, .16, .62, .88)` | `(0, .14, .60, .85)` |
| `CumulusGradient` | `(0, .22, .68, .92)` | `(0, .22, .68, .92)` | `(0, .22, .68, .92)` | `(0, .22, .68, .92)` | `(0, .22, .70, .94)` | `(0, .15, .80, 1.0)` | `(0, .22, .68, .92)` |
| `BaseGradientPower` | `2.00` | `2.00` | `2.00` | `2.60` | `2.30` | `1.60` | `2.00` |
| `TopGradientPower` | `1.50` | `1.50` | `1.50` | `1.80` | `1.60` | `1.10` | `2.20` |
| `DensityHeightBias` | `0.50` | `0.60` | `0.70` | `0.20` | `0.35` | `1.10` | `0.20` |
| `DetailStrength` | `0.30` | `0.34` | `0.38` | `0.18` | `0.24` | `0.50` | `0.55` |
| `DetailTileSize` | `M(4000)` | `M(4000)` | `M(4000)` | `M(6000)` | `M(5000)` | `M(3000)` | `M(2500)` |
| `DetailSeed` | `13` | `13` | `13` | `13` | `13` | `13` | `13` |
| `DetailTypeBias` | `0.60` | `0.55` | `0.50` | `0.15` | `0.30` | `0.70` | `0.05` |
| `BillowGradientPower` | `0.25` | `0.25` | `0.25` | `0.25` | `0.25` | `0.25` | `0.25` |
| `BillowNoiseScale` | `0.30` | `0.30` | `0.30` | `0.26` | `0.28` | `0.38` | `0.22` |
| `HighFreqStrength` | `0.40` | `0.45` | `0.50` | `0.20` | `0.30` | `0.60` | `0.70` |
| `HighFreqWispSharpness` | `4.00` | `4.00` | `4.00` | `4.00` | `4.00` | `4.00` | `5.00` |
| `HighFreqBillowSharpness` | `2.00` | `2.00` | `2.00` | `2.00` | `2.00` | `2.00` | `2.00` |
| `HighFreqFadeStart` | `M(2500)` | `M(2500)` | `M(2500)` | `M(2500)` | `M(2500)` | `M(3000)` | `M(4000)` |
| `HighFreqFadeEnd` | `M(9000)` | `M(9000)` | `M(9000)` | `M(9000)` | `M(9000)` | `M(11000)` | `M(14000)` |
| `CurlStrength` | `0.25` | `0.30` | `0.35` | `0.15` | `0.20` | `0.55` | `0.65` |
| `CurlTileSize` | `M(9000)` | `M(9000)` | `M(9000)` | `M(12000)` | `M(11000)` | `M(7000)` | `M(6000)` |
| `DensitySharpenLow` | `0.30` | `0.30` | `0.30` | `0.40` | `0.36` | `0.24` | `0.34` |
| `DensitySharpenHigh` | `0.60` | `0.60` | `0.60` | `0.70` | `0.66` | `0.52` | `0.64` |
| `DensityScalePower` | `4.00` | `4.00` | `4.00` | `2.50` | `3.00` | `4.50` | `4.00` |
| `DistanceSoftening` | `0.60` | `0.60` | `0.60` | `0.55` | `0.55` | `0.65` | `0.70` |
| `SofteningStartDistance` | `M(8000)` | `M(8000)` | `M(8000)` | `M(8000)` | `M(8000)` | `M(8000)` | `M(15000)` |
| `SofteningEndDistance` | `M(45000)` | `M(45000)` | `M(45000)` | `M(45000)` | `M(45000)` | `M(45000)` | `M(70000)` |
| `NearFadeStart` | `M(50)` | `M(50)` | `M(50)` | `M(50)` | `M(50)` | `M(50)` | `M(50)` |
| `NearFadeEnd` | `M(900)` | `M(900)` | `M(900)` | `M(700)` | `M(800)` | `M(1200)` | `M(900)` |
| `NearFadeMinDensity` | `0.25` | `0.25` | `0.25` | `0.20` | `0.22` | `0.30` | `0.25` |
| `ScatteringAlbedo` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(.97, .97, .98)` | `(.96, .96, .98)` | `(.92, .93, .96)` | `(1, 1, 1)` |
| `ExtinctionTint` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` |
| `LightMarchDistance` | `M(800)` | `M(900)` | `M(1000)` | `M(500)` | `M(700)` | `M(1400)` | `M(400)` |
| `LightConeSpread` | `0.35` | `0.35` | `0.35` | `0.30` | `0.32` | `0.40` | `0.30` |
| `PhaseForwardG` | `0.80` | `0.80` | `0.80` | `0.72` | `0.75` | `0.80` | `0.85` |
| `PhaseBackwardG` | `-0.15` | `-0.15` | `-0.15` | `-0.12` | `-0.14` | `-0.25` | `-0.10` |
| `PhaseBlend` | `0.50` | `0.50` | `0.50` | `0.45` | `0.48` | `0.60` | `0.55` |
| `SilverLiningIntensity` | `1.00` | `1.10` | `1.20` | `0.60` | `0.70` | `1.40` | `1.60` |
| `PowderStrength` | `0.50` | `0.50` | `0.50` | `0.25` | `0.30` | `0.70` | `0.40` |
| `PowderScale` | `2.00` | `2.00` | `2.00` | `2.00` | `2.00` | `2.40` | `1.60` |
| `MultiScatterExtinctionFalloff` | `0.50` | `0.50` | `0.50` | `0.55` | `0.55` | `0.65` | `0.45` |
| `MultiScatterScatterFalloff` | `0.50` | `0.50` | `0.50` | `0.50` | `0.50` | `0.55` | `0.50` |
| `MultiScatterPhaseFalloff` | `0.50` | `0.50` | `0.50` | `0.50` | `0.50` | `0.50` | `0.50` |
| `AmbientSkyContribution` | `1.00` | `1.00` | `1.00` | `1.20` | `1.30` | `1.40` | `1.00` |
| `AmbientGroundContribution` | `0.25` | `0.25` | `0.25` | `0.35` | `0.40` | `0.45` | `0.20` |
| `AmbientHeightBias` | `0.50` | `0.50` | `0.50` | `0.40` | `0.45` | `0.60` | `0.50` |
| `SunLightIntensityScale` | `1.00` | `1.00` | `1.00` | `0.90` | `0.85` | `0.75` | `1.10` |
| `SunTint` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` |
| `ShadowTint` | `(1, 1, 1)` | `(1, 1, 1)` | `(1, 1, 1)` | `(.95, .96, 1.0)` | `(.92, .94, 1.0)` | `(.86, .89, .98)` | `(1, 1, 1)` |
| `PrecipitationDarkening` | `0.50` | `0.50` | `0.50` | `0.60` | `0.65` | `0.85` | `0.50` |
| `AtmosphericPerspective` | `0.80` | `0.80` | `0.80` | `0.85` | `0.85` | `0.75` | `0.60` |
| `DistanceFadeStart` | `M(50000)` | `M(50000)` | `M(50000)` | `M(40000)` | `M(45000)` | `M(50000)` | `M(80000)` |
| `DistanceFadeEnd` | `M(140000)` | `M(140000)` | `M(140000)` | `M(110000)` | `M(120000)` | `M(130000)` | `M(200000)` |
| `AnimationSpeed` | `1.00` | `1.00` | `1.00` | `0.50` | `0.70` | `2.20` | `1.60` |
| `WindInfluence` | `1.00` | `1.00` | `1.00` | `0.80` | `0.90` | `1.80` | `1.30` |
| `WindDirectionOffset` | `0.00` | `0.00` | `0.00` | `0.00` | `0.00` | `0.00` | `0.00` |
| `ShapeScrollMultiplier` | `1.00` | `1.00` | `1.00` | `1.00` | `1.00` | `1.20` | `1.00` |
| `DetailScrollMultiplier` | `2.00` | `2.00` | `2.00` | `1.40` | `1.60` | `3.00` | `2.40` |
| `WeatherScrollMultiplier` | `0.35` | `0.35` | `0.35` | `0.25` | `0.30` | `0.60` | `0.50` |
| `WindHeightShear` | `0.30` | `0.30` | `0.30` | `0.20` | `0.25` | `0.50` | `0.15` |
| `WindUpliftSpeed` | `M(4)` | `M(4)` | `M(4)` | `M(1)` | `M(2)` | `M(18)` | `M(3)` |

**CLD-54 — every preset value must lie inside its field's declared `Range`.**
*Rationale:* an out-of-range preset produces a slider that jumps on first touch — the classic "the UI lies"
bug, and it is trivially checkable.
*Acceptance:* a unit test iterates `kCloudPresets` × the reflected field metadata and asserts
`min <= value <= max` for every `Float`/`Int` field of every preset. This test also fails if a `Range` is
tightened later without updating the presets.

---

## 6. Quality tiers

**CLD-60 — `enum class CloudQuality { Low, Medium, High, Ultra, Custom }`, applied by a pure
`ApplyQuality( CloudQuality, VolumetricCloudData& )` that writes only the 13 quality fields.** Editing any
quality field sets `QualityLevel = Custom`, computed by `MatchQuality`. Same mechanism, same file shape and
same tests as §5, in `Graphic/CloudQuality.hpp`.
*Rationale:* architect decision 9. The reference conflates the two — its "cloud type" preset selector and its
step-size constants live in the same UI block (REF §H.1), which is why its own summary has to say "quality
should be a separate tier, not a preset" (REF §H.12).
*Acceptance:* unit test — for each of the 4 non-`Custom` tiers, apply and assert the 79 preset-driven fields
are byte-identical before and after.

**CLD-61 — tier values.**

| Field | Low | Medium | **High** (default) | Ultra |
|---|---|---|---|---|
| `ResolutionScale` | `Quarter` | `Half` | `Half` | `Full` |
| `MaxSteps` | `32` | `64` | `128` | `256` |
| `MinStepSize` | `M(60)` | `M(30)` | `M(15)` | `M(8)` |
| `MaxStepSize` | `M(1500)` | `M(1000)` | `M(700)` | `M(400)` |
| `StepGrowthRate` | `0.020` | `0.012` | `0.008` | `0.005` |
| `CoarseStepMultiplier` | `4.0` | `3.0` | `3.0` | `2.0` |
| `EmptySamplesBeforeCoarse` | `4` | `6` | `8` | `8` |
| `LightMarchSamples` | `3` | `4` | `6` | `8` |
| `MultiScatterOctaves` | `1` | `2` | `2` | `3` |
| `TemporalMode` | `Off` | `Reprojection` | `Reprojection` | `Reprojection` |
| `TemporalBlendFactor` | `1.00` | `0.15` | `0.10` | `0.08` |
| `TemporalClampScale` | `1.00` | `1.25` | `1.50` | `1.75` |
| `JitterStrength` | `1.00` | `1.00` | `1.00` | `0.50` |

**CLD-62 — `MaxSteps` is a hard bound the shader loop must honour, not a hint.** *Rationale:* an unbounded
`while` over a procedural field is the reference's structure (REF §C.3) and it is how a bad parameter
combination becomes a GPU hang. *Acceptance:* the loop is a `for` with `MaxSteps` as the bound; the step
scheduler test asserts the count never exceeds it for adversarial inputs (`MinStepSize` at its minimum,
`MaxViewDistance` at its maximum).

**CLD-63 — every quality tier must produce a valid image at every preset.**
*Acceptance:* a unit test forms the 7 × 4 = 28 combinations, packs each to the GPU payload, and asserts the
payload contains no `NaN`/`Inf`, `MinStepSize <= MaxStepSize`, `HorizonFadeStart <= HorizonFadeEnd`,
`NearFadeStart <= NearFadeEnd`, `SofteningStartDistance <= SofteningEndDistance`,
`DistanceFadeStart <= DistanceFadeEnd`, and every gradient `vec4` is non-decreasing in its four components.
*Rationale:* these ordering invariants are the ones that produce a division by zero in the shader — the
review item called out at `DEV_CONTRACT.md:122-123`.

---

## 7. Interface contract with the sky half (Analyst A)

**The governing principle (architect's decision): we share the COMPUTATION, not the REPRESENTATION.**
The cloud system never receives the sky's palette in C++. It receives a narrow set of scalars, and for
anything that needs "what colour is the sky in this direction" it includes A's shader header and calls A's
function. Consequence: A can add, remove or rename a palette field and the cloud shader keeps compiling and
keeps being correct, because it never named those fields. This also removes the two-point ambient probe from
v1's design — evaluating the sky in the exact direction we need is both cheaper to maintain and more
accurate than interpolating between two probes.

**CLD-70 — `Graphic::AtmosphereEnv`, owned and filled by Analyst A, read by the cloud render system through
`SceneRenderer` exactly as `GetWind()` is (`SceneRenderer.hpp:143-146`). It is narrow and stays narrow:**

```cpp
namespace Desert::Graphic
{
    // Evaluated once per frame by the sky/atmosphere system. The NARROW contract: scalars that clouds
    // cannot derive themselves. Anything colour-shaped is EVALUATED via Common/Atmosphere.glslh, not
    // transported here — see CLD-70a. Linear throughout.
    struct AtmosphereEnv
    {
        bool      Valid;            // false = no SkyAtmosphereComponent in the scene -> clouds do not draw
        glm::vec3 SunDirection;     // NORMALIZED, points TOWARD the sun (the engine's one negation)
        glm::vec3 SunRadiance;      // linear; colour * intensity, atmosphere-attenuated
        float     SunAngularRadius; // radians
        glm::vec3 AmbientRadiance;  // linear; the sky's overall ambient term
        float     PlanetRadius;     // world units (cm) - SINGLE SOURCE OF TRUTH, never duplicated
        glm::vec3 PlanetCenter;     // world units
    };
}
```

*Rationale per field:* `SunDirection` — the engine has exactly one negation, at `SkyboxECSSystem.hpp:40`, and
everything downstream of it is "toward sun" (ENG §7.2); clouds sit on that side of the flip and must not
re-derive it. `SunRadiance` — one number, so the sky and the clouds cannot disagree about how bright the sun
is (today `DirectionalLightData::Intensity`/`Color` never reach the sky at all, ENG §7.2 last bullet — A's to
resolve). `SunAngularRadius` — softens the shadow terminator. `AmbientRadiance` — the scene-level ambient
level; directional ambient comes from CLD-70a. `PlanetRadius`/`PlanetCenter` — the shell (CLD-23) must be
concentric with the atmosphere or the horizon will not agree with itself; duplication would violate
`DEV_CONTRACT.md:56-58` and is forbidden by CLD-24a.
*Acceptance:* the struct compiles in `Desert/Desert/Source/Engine/Graphic/`; a unit test constructs it and
feeds it to `PackCloudUniforms`, asserting each field reaches a distinct payload offset. A grep asserts the
cloud C++ contains no sky-palette field name (`ZenithColor`, `HorizonColor`, `SunsetColor`, `GroundColor`,
`NightColor`, `SkyBrightness`, `HorizonFalloff`, `SunGlow`, `SunsetIntensity`, `StarIntensity`).

**CLD-70a — directional sky radiance is obtained by including `Common/Atmosphere.glslh` and calling
`EvaluateSky`, not by transporting a palette.**
*Statement:* the cloud raymarch shader does `#include <Common/Atmosphere.glslh>` and calls
`EvaluateSky( dir, sunDir, sunIntensity, sunDiskRadius, cfg )` (`Atmosphere.glslh:53`) for (a) the upward
ambient term, (b) the downward/ground-bounce term, and (c) the atmospheric-perspective colour that distant
clouds fade into (`AtmosphericPerspective`, `DistanceFadeStart/End`). The `SkyConfig` (`Atmosphere.glslh:13`)
is **not** mirrored in cloud C++; it is produced inside the shader by a loader that Analyst A owns
(see CLD-70b).
*Rationale:* architect decision. `Atmosphere.glslh` is already the shared model used by both the screen sky
pass and the IBL bake (`Atmosphere.glslh:1-5`), so this is the third consumer of an existing sharing point,
not a new mechanism. The shader cache hashes `#include`d file content recursively
(`ShaderCompiler.cpp:141-152`, ENG §3.6), so an edit to A's palette correctly invalidates the cloud shader —
the sharing is safe by construction.
*Acceptance:* `Common/Atmosphere.glslh` appears in the cloud shader's include list; `DShaderTool` compiles
the cloud **compute** stage with it (CLD-84). ⚠️ **Unverified:** that `Atmosphere.glslh` is compute-stage
clean — I read it and found only pure maths (`atm_hash13`, `smoothstep`, no `gl_FragCoord`, no `dFdx`), but
it has only ever been compiled as fragment and compute. If a fragment-only construct is found, the fix is
A's: move it behind a stage guard, not duplicate the model.

**CLD-70b — three concrete things Analyst B needs from Analyst A to make CLD-70a work:**
1. **A shared include that declares the sky parameter block and loads it**, e.g. `Common/SkyParams.glslh`, exposing exactly `SkyConfig LoadSkyConfig();` plus the block declaration at a fixed binding. B includes it and calls it; B never names a member.
2. **The block must be a `std430` storage buffer, not a uniform block**, because `ComputePipeline` has no `SetUniformBuffer` (verified, `Pipeline.hpp:157-179`) — an SSBO is the only per-slot parameter channel a compute dispatch has. It must be created **non-persistent** so it is indexed per (frame × renderer slot) (CLD-35), or the Details mesh preview will corrupt the viewport's sky exactly as documented in `Docs/RENDERER_FRAME_STATE.md`.
3. **An opaque handle to that buffer on `AtmosphereEnv`** — `ShaderResources::StorageBuffer* SkyParams;` — so B can `SetStorageBuffer( binding, env.SkyParams )` without knowing what is inside it.
*Rationale:* this is the minimum that makes "share the computation, not the representation" actually
implementable across a compute-shader boundary. Without (2) there is no way to get the palette into a compute
dispatch at all, and without (3) B would have to reach into A's material.
*Acceptance:* B's raymarch shader compiles and links against the two includes; a grep confirms B binds the
buffer by handle and never constructs its contents. **This is a backbone item — it must exist before B's
raymarch can compile** (`DEV_CONTRACT.md:173-176`).

**CLD-71 — if `Valid == false`, the cloud pass does not render and logs once at `LOG_WARN`, naming the
missing component.** No silent default sky, no invented sun.
*Rationale:* `DEV_CONTRACT.md:33-36` forbids silent fallbacks; ENG §7.2 documents three different invented
no-light fallbacks already in the engine and calls the resulting ambiguity out as a defect.
*Acceptance:* the render system's execute path has an early return guarded by `Valid` with a
`LOG_WARN`-once; grep-checkable.

**CLD-72 — clouds are lit by `SunRadiance * SunLightIntensityScale * SunTint`, and ambient-lit by
`EvaluateSky( up, … ) * AmbientSkyContribution` blended toward `EvaluateSky( down, … ) *
AmbientGroundContribution` by `AmbientHeightBias`, all scaled by `AtmosphereEnv::AmbientRadiance`.** The cloud
component contributes only *multipliers and tints*, never absolute colours.
*Rationale:* one source of truth for "how bright is the sun", and the ambient direction comes from evaluating
the shared sky model rather than from a transported probe (CLD-70a). The reference hardcodes two literal tint
colours (REF #72/#74) and the result is a cloud that ignores the time of day.
*Acceptance:* unit test on the pure ambient-**composition** function (the part that does not call the sky):
given two supplied radiances and the three multipliers, with all multipliers at 1 and `ScatteringAlbedo`/tints
at white the result equals the supplied radiance exactly; `AmbientSkyContribution = 0` zeroes only the sky
term; `AmbientHeightBias` at 0 and 1 selects the two endpoints exactly.

**CLD-73 — what the clouds contribute back to the sky in v1: nothing, except occlusion of what the sky pass
already drew.** The sky (including the sun disc, halo and stars) is rendered at `RenderPhase::Sky` (200) and
the cloud composite runs at `Transparency` (700) with a LOAD begin, so clouds occlude the sky and the sun
disc automatically, with no interface at all.
*Acceptance:* review of the pass phases; no new API between the two systems in this direction.

**CLD-74 — clouds do not participate in the IBL bake in v1 (N3), and this must be stated in the component's
tooltip on `Enabled`**, so an artist is not left wondering why an overcast sky does not darken the scene.
*Acceptance:* the tooltip text is asserted non-empty by CLD-42's test; its content is a review item.

**CLD-75 — coordination items owned jointly, listed so neither analyst assumes the other did them:**
(a) removal of the six `Cloud*` fields from `SkyboxComponent` and the scene migration — **A owns the files**;
(b) the `SetProceduralSky` signature change and its three call sites (`ProceduralSkyCommand.hpp:32`,
`PreviewViewport.cpp:177-178`, `AssetThumbnailRenderer.cpp:97`) — **A owns**;
(c) `PlanetRadius`/`PlanetCenter` landing on `SkyAtmosphereComponent` as the single source of truth — **A
owns**; B is forbidden from having its own (CLD-24a);
(d) `AtmosphereEnv` + `Common/SkyParams.glslh` (CLD-70, CLD-70b) — A defines and fills, B consumes. **Backbone:
must exist before B's raymarch can compile** (`DEV_CONTRACT.md:173-176`);
(e) **the inverted sun in the shipped sandbox scenes** — `LightGizmoRenderer.cpp:191-196` treats
`Translation` as toward-sun and negates it, opposite to `Scene.cpp:346-353` and every other reader, and
`Desert_Sandbox.desce`/`Starter.desce` author the Sun at `[0.351, 0.902, 0.251]`, i.e. below the horizon
under the canonical convention (ENG §7.2). **A owns this; B does not touch it.** Recorded here only so that
"the clouds are lit from underneath" is not diagnosed as a cloud bug on day one.
(f) `RenderGraphBuilder` intra-phase ordering (CLD-19) — **the render-graph developer owns it**, and it
blocks CLD-21a.

---

## 8. Acceptance criteria

None of these requires launching the editor. Numbered, with the verification method named.

**Build and lint**

| # | Criterion | Verification |
|---|---|---|
| CLD-80 | `make -j8 Desert config=debug`, `make -j8 Editor config=debug`, and both again with `config=release`, all succeed. | run the four commands |
| CLD-81 | No new compiler warnings versus the base commit. | diff the warning output of a clean build against `dev` |
| CLD-82 | `premake5 gmake2` re-run and the regenerated `*.make` files committed, because new `.cpp` files exist. | `grep` the new object names in `Desert.make` / `Editor.make` (ENG §5.3) |
| CLD-83 | `Reflection.gen.cpp` regenerated by the prebuild step and committed, never hand-edited. | `git diff --stat` shows it; file banner intact |
| CLD-84 | `DShaderTool Editor/Resources/Shaders` exits 0. | run the CI lint command locally (`.github/workflows/ci.yml:121-122`) |
| CLD-85 | `git-clang-format` (llvm@18) clean on changed lines, new files `git add`ed first. | `/opt/homebrew/opt/llvm@18/bin/git-clang-format --binary /opt/homebrew/opt/llvm@18/bin/clang-format --diff <base>` |
| CLD-86 | Zero `TODO`/`FIXME`/`XXX`/`HACK` in new files. | `grep -rn` over the changed file list |

**Unit tests** — all in `Desert/Tests/Engine/…`, GoogleTest, standalone `ConsoleApp`, run via
`./scripts/MacOS/RunTests.sh "$PWD" Debug` **and** `Release` (ENG §4.6). Every test must be shown to fail
when its subject is deliberately broken (`DEV_CONTRACT.md:90-91`).

| # | Criterion | Test target / method |
|---|---|---|
| CLD-87 | **Cloud math.** Shell intersection (CLD-24), step scheduling (CLD-25), coarse/fine schedule (CLD-26), HG + dual-lobe phase + in-scatter height dependence (CLD-27), multi-scatter octaves (CLD-28), Beer transmittance (CLD-25), depth linearisation (CLD-29), reprojection + neighbourhood clamp (CLD-32). | `Tests/Engine/CloudMath/` over header-only `Graphic/CloudMath.hpp` — no renderer, no GPU (model: `Tests/Engine/ShadowCascades/`). *T9: the reprojection, the clamp, the blend, the depth-guide packing and the bilateral weights live in `Tests/Engine/CloudTemporal/` instead, over `Common/CloudTemporal.glslh` compiled as C++ — the same arrangement, one binary per stage.* |
| CLD-88 | **No dead parameters.** For every reflected field of `VolumetricCloudData`, perturb it in isolation using its reflection offset/type and assert `PackCloudUniforms()` produces different bytes. Fields that are legitimately CPU-only (`Preset`, `QualityLevel`, `ResolutionScale`, `Enabled`, `ShapeSeed`, `DetailSeed`, `WeatherSeed`, `RequestRegenerateNoise`) are listed in an explicit allow-list, and the test additionally asserts the allow-list contains *exactly* those names — so growing it is a deliberate, reviewable edit. | `Tests/Engine/CloudParams/` |
| CLD-89 | **GPU payload layout.** `static_assert` + test on `sizeof`/`offsetof` of the params SSBO struct and the push-constant struct against the documented std430/std140 layout; push constant `sizeof <= 128`. | `Tests/Engine/CloudParams/` — this is what stops the `SkyUBData`-style silent mirror divergence (ENG §1.2) |
| CLD-90 | **Presets.** Table completeness (CLD-50), default == PartlyCloudy (CLD-51), Apply/Match round-trip and `Custom` on edit (CLD-52), quality untouched (CLD-53), every value in range (CLD-54). | `Tests/Engine/CloudPresets/` |
| CLD-91 | **Quality.** Apply/Match round-trip, look fields untouched (CLD-60), `MaxSteps` bound honoured for adversarial inputs (CLD-62), 28 preset×tier combinations produce finite, ordering-valid payloads (CLD-63). | `Tests/Engine/CloudPresets/` |
| CLD-92 | **Serialization round-trip.** Save→load through `ComponentRegistry` preserves all 95 fields; `RequestRegenerateNoise` is absent from the JSON; a JSON missing a field keeps the C++ default. | `Tests/Engine/CloudSerialization/` (model: `Tests/Engine/ReflectionSerializer/`) |
| CLD-93 | **Annotation completeness** (CLD-42): every field has a tooltip; every numeric field has a `Range`; every distance-named field has `Length` or `Units`. | `Tests/Engine/CloudParams/`, walking the reflection metadata |
| CLD-94 | **Traceability, field → shader uniform.** A single static table maps each reflected field name to its GLSL member name in the params block. The test asserts the table covers exactly the non-allow-listed fields of CLD-88, and that each named GLSL member exists in the `.shader` source (read as text, matched by regex). | `Tests/Engine/CloudParams/` |
| CLD-95 | **Image size arithmetic** for the new 3D path (CLD-10): 128³ RGBA8F = 8 388 608 bytes, 32³ = 131 072, and the depth-aware `CalculateImageSize` agrees. | `Tests/Engine/CloudParams/` or the existing image test target |
| CLD-96 | **DShaderParser** emits correct GLSL for `Uniform(N) sampler3D` and raw `layout(binding=N, rgba8) uniform image3D` (CLD-15). | `Tests/Engine/DShaderParser/` |
| CLD-96a | **Deterministic pass order** (CLD-19): shuffled registration across phases sorts by (phase, registration); two builds of the same graph are identical; clouds-before-particles in `Transparency` holds. | `Tests/Engine/RenderGraphSort/` — owned by the render-graph task, but it is B's blocking dependency, so B's DoD checks it exists and passes |
| CLD-96b | **`RGBA16F`** (CLD-18): `GetBytesPerPixel == 8`; 2D and 3D size arithmetic agree; the four per-renderer memory figures of CLD-34 are reproduced by the allocator's own arithmetic. | `Tests/Engine/CloudParams/` |
| CLD-96c | **Depth aspect selection** (CLD-13): format → aspect mapping for `DEPTH24STENCIL8`, `DEPTH32F` and each colour format. | pure unit test, no device |
| CLD-96d | **Temporal `Off` is real** (CLD-32a.2): with `TemporalMode = Off` the resolve output equals the raymarch output bit-for-bit. | `Tests/Engine/CloudTemporal/` — a separate binary, so the temporal stage's tests do not need the raymarch's fixtures. As landed the statement is stronger: with `Off` the resolve is not dispatched at all and the composite is bound to the raymarch target, so the test drives that decision (`CloudSelectCompositeSource`) and `memcmp`s the image the composite would read against the image the march wrote. The blend itself is separately asserted bit-exact for a weight of 1 and for unusable history, including infinite and NaN history values. |
| CLD-96e | **No silent zero on an unknown format** (CLD-18a): exact byte count asserted for all six `ImageFormat` enumerators; grep confirms neither `GetBytesPerPixel` nor `CalculateImageSize` contains a `return 0`; a locally-added seventh enumerator without a `case` is shown to **fail the build**, not the test (demonstrated once by the developer and reported, then reverted). | `Tests/Engine/CloudParams/` + a one-off build demonstration in the report |

**Not verifiable here — must be listed as such in the developer's report**

| # | Item | Why |
|---|---|---|
| CLD-97 | Every visual claim ("looks like a storm", "no seam", "no ghosting"). | No Vulkan in this environment (`DEV_CONTRACT.md:82-86`, ENG §5.1). The report must say *what was checked* (built, test passed, number matched), never "verified visually". The five temporal artefacts of CLD-32b are the concrete list of things that will look wrong and are **expected**, so they are reported in advance rather than rediscovered. |
| CLD-98 | Absence of Vulkan validation errors from the new 3D-image, depth-aspect and compute-barrier paths (CLD-10, CLD-12, CLD-13). | Requires a run; the user must paste `VulkanDebugCallback` output. This is the highest-risk unverifiable item in the programme. |
| CLD-99 | Frame cost of each quality tier. | Requires a GPU. Profile scopes (`DESERT_PROFILE_SCOPE`) must be present on all five stages from day one (ENG §4.8) so the numbers exist the moment someone can run it. |

---

## 9. Decision record

All nine v1 open questions were decided by the architect. This section is the record: the question, the
decision, and the requirement it became. Nothing here is still open.

| # | Question (v1) | Decision | Became |
|---|---|---|---|
| **D-1** | Intra-phase pass order in `Transparency` is non-deterministic (`RenderGraphBuilder.cpp:109-181` sorts phases, not passes). Fix the graph, or route the cloud composite around it? | **Fix the primitive.** Stable tie-break by registration order in `RenderGraphBuilder`, documented and unit-tested. The bypass is rejected: it would leave the trap in place for the next person who registers a second pass in any phase, and add a nineteenth step outside the graph. Clouds composite **before** particles. Separate task, render-graph owner. | **CLD-19**, **CLD-21a**, CLD-96a, CLD-75(f) |
| **D-2** | Temporal accumulation in v1 or v1.1? | **v1** — without it "Storm" boils, i.e. the feature is unusable and we do not ship halves. Three constraints: camera-only reprojection (no per-object motion vectors — wind is already in the march); `TemporalMode = Off` is a full tested path, not a failure branch; the bought artefacts are named in writing before the code. Its own task, its own acceptance — not "part of the raymarch". | **CLD-32**, **CLD-32a**, **CLD-32b**, CLD-96d |
| **D-3** | Build the depth-aspect transition (CLD-13), or use `GBufferC` and support Deferred only? | **Build CLD-13.** Forward is the default `RenderingPath`; "occluded only in Deferred" is the half-result the contract forbids. One code path for both. | **CLD-13** (hardened), **CLD-29** (no per-path branch), CLD-96c |
| **D-4** | Add `RGBA16F` now or later? | **Now.** Halves the history memory, and we are already inside the format tables and image classes for CLD-10 — a second visit costs more. 16F is sufficient for radiance and transmittance; a shortfall must be reported as a *named quantity and symptom*, not a blanket widening. Confirmed alongside it: the cloud system is **never** registered on preview or thumbnail renderers. | **CLD-18**, CLD-34 (revised figures), CLD-17 (confirmed), CLD-96b |
| **D-5** | Should clouds darken scene lighting in v1? | **No.** Recorded as a v1.1 item in the shape B proposed: a scalar sky-occlusion coefficient computed on the CPU from coverage / extinction / layer thickness — therefore unit-testable, which is the point of that shape. | N3, **§1.3 V1.1-a** |
| **D-6** | The inverted sun in the two shipped sandbox scenes. | **Analyst A's**, who already owns the light and the migration. B keeps the pointer so it is not diagnosed as a cloud bug, and does not touch it. | **CLD-75(e)** |
| **D-7** | Camera-relative maths and km-space shell intersection. | **Approved, with the proving test.** Separately: a cloud-only planet radius is **forbidden** — the radius is a shared physical constant of sky and clouds, and a divergence produces a horizon that does not agree with itself. If artefacts survive camera-relative maths, escalate; the radius then changes **once, for both subsystems**. | **CLD-24**, **CLD-24a** |
| **D-8** | Can the sampler-classification check run without a device? | **The check must exist; its form is the developer's.** If `Reflect()` needs a `VkDevice`, it moves to `DShaderTool --reflect` and that is stated in the report. A `sampler3D` silently bound as 2D does not crash — it quietly draws garbage. | **CLD-11** |
| **D-9** | C++ preset table vs data-driven preset assets. | **`constexpr` C++ table, one file, one entry per preset** — decided engine-wide for both halves. A preset asset is an explicit non-goal with its reason (new asset type + registry + resolver + migration, for seven rows of numbers). B's `CloudPresetValues` split — quality fields structurally unreachable from a preset — is kept: better than policing discipline with a test. | **CLD-50** (unchanged), **N10** |
| **D-10** | *(architect-initiated)* The shape of the contract with Analyst A. | **Share the computation, not the representation.** `AtmosphereEnv` stays narrow — sun direction, sun radiance, ambient term, valid flag (plus the planet geometry required by D-7). Directional sky colour is obtained by including `Common/Atmosphere.glslh` and calling `EvaluateSky`, not by transporting a palette struct into cloud C++. A palette edit then cannot break B's shader. | **§7 rewritten: CLD-70, CLD-70a, CLD-70b, CLD-72** |
| **D-11** | *(architect-verified)* Are non-persistent storage buffers per (frame × slot)? | **Yes**, verified in the source. B's "unverified" mark removed. | **CLD-35** (`VulkanStorageBuffer.hpp:36`, `.cpp:123,130`) |

### 9.1 What B raised in response to the decisions

Not objections — consequences that need an owner.

| # | Item | Status |
|---|---|---|
| R-1 | **CLD-19 blocks CLD-21a.** Clouds cannot guarantee they draw under particles until the sorter is deterministic. If the graph task slips, the cloud composite still works — only the ordering versus particles is unguaranteed, and that must then be stated as a known limitation rather than silently accepted. | needs sequencing by the architect |
| R-2 | **CLD-70b asks three concrete things of Analyst A** (a `SkyConfig` loader include; the sky parameter block as a **std430 SSBO**, because `ComputePipeline` has no `SetUniformBuffer`; and an opaque buffer handle on `AtmosphereEnv`). Without them "call `EvaluateSky` from compute" is not implementable. | **Closed — approved by the architect and sent to A**, all three points. The handle stays opaque, so what crosses the boundary is a descriptor, not the palette layout. |
| R-3 | **`Atmosphere.glslh` has never been compiled as a compute stage.** I read it and found only pure maths, but that is a read, not a compile. Marked unverified; first acceptance is `DShaderTool`. | verified by CLD-84 on first build |
| R-4 | **`ImageFormat` gains an entry (CLD-18), so every `switch` over it must be revisited.** `Image::GetBytesPerPixel` returns 0 for unhandled formats (`Image.cpp:91-104`) — a silent wrong answer, not a compile error. Re-read: the `switch` has no `default:` label, so `-Wswitch` *would* fire, but the unconditional `return 0U;` on line 103 lets it compile and return the wrong size regardless. | **Closed — promoted to a requirement.** Architect: this is a forbidden silent fallback, and the fix must not depend on warning flags. See **CLD-18a** and CLD-96e. |

---

*Analyst B, v2. Every factual claim above is cited to `RESEARCH_REFERENCE.md`, `RESEARCH_ENGINE.md`, or a
`file:line` re-read in the working tree at commit `b02730b`. Three items remain **unverified** and are marked
as such at their requirement: whether `VulkanShader::Reflect()` can run without a `VkDevice` (CLD-11),
whether the DSL passes `sampler3D`/`image3D` through unchanged (CLD-15), and whether
`Common/Atmosphere.glslh` compiles as a compute stage (CLD-70a). The renderer-slot dimension of
non-persistent storage buffers is no longer among them — it is verified at
`ShaderResources/API/Vulkan/VulkanStorageBuffer.hpp:36` and `.cpp:123,130` (CLD-35).*

---

## 10. v3 — Nubis-Cubed source-deck parity pass (2026-08-13)

**Trigger.** The default loading scene renders clouds as uniform slate-blue putty: lit faces barely
brighter than shadowed ones, no billow hierarchy, no wispy edges. Diagnosed against a **complete
per-page analysis of the SIGGRAPH Advances 2023 deck** (`Docs/Clouds/PdfAnalysis/pages-*.md`, all 208
pages, cited below as *PDF p.N*) plus a code audit of the ambient/lighting path. §L/§M of
`RESEARCH_REFERENCE.md` remain valid; this section supersedes their open items.

### 10.1 Reference facts the implementation must match

| # | Fact | Source |
|---|---|---|
| F1 | Ambient base form: `ambient_scattering = pow(1.0 - dimensional_profile, 0.5)` — a function of the **unerroded profile**, not the eroded density. | PDF p.141 |
| F2 | Ambient occlusion column: `* exp(-summed_ambient_density)`, where the sum is taken **vertically** (along up), amortised in a coarse buffer. | PDF p.144 |
| F3 | Depth-modulated multiple scattering keeps cores luminous: effective extinction falls from **0.25 at the surface to 0.05 deep inside** the cloud. | PDF p.136 |
| F4 | Ambient colour tracks the **whole sky dome the cloud hangs against** — blue-grey but *luminous* at midday, mauve/taupe at sunset, warm beige inside a deck. Shadows are never near-black and never pure zenith-blue. | PDF pp.17, 83, 127, 142-146, 180, 202, 205 |
| F5 | Detail noise: **4-channel 128×128×128** — Low/High-freq **Curly-Alligator** (wispy pair), Low/High-freq **Alligator** (billowy pair). Alligator = bright cells with sharp dark crevice networks; Curly-Alligator = feathery swirled filaments. | PDF pp.93-94, 205 |
| F6 | Detail composite formulas: `wispy = lerp(n.r, n.g, profile)`; `billowy_gradient = pow(profile, 0.25)`; `billowy = lerp(n.b*0.3, n.a*0.3, gradient)`; `composite = lerp(wispy, billowy, type)`; HHF ridged folds with exponents 4 (wisps) / 2 (billows); `ValueErosion(profile, composite)`; density-scale sharpening `pow(d, lerp(0.3, 0.6, pow(scale,4)))`. | PDF pp.99, 104-118 |
| F7 | Billows are **self-similar across scales** (fractal cauliflower); billow frequency selection is driven by the profile. | PDF p.103-104 |
| F8 | Lit/shadow separation is carried by **luminance**, not only hue: lit tops white/cream, shadow sides mid-grey tinted by the sky. | PDF pp.17, 83, 180, 205 |

### 10.2 Audit defects → v3 requirements

Each requirement keeps the v2 form: statement — rationale — acceptance.

**CLD-100 (ambient sky radiance is hemispheric).** `AtmosphereEnv::ZenithRadiance` for clouds becomes a
solid-angle-weighted blend of the zenith and horizon palette colours (weight toward horizon), not the
zenith texel alone. *Rationale:* audit — with `ZenithColor (0.08,0.26,0.70)` alone the ambient R:B ratio
is 0.11 and every shadow is deep blue; F4 says shadows take the dome's hue, and the dome is mostly
horizon by solid angle. *Acceptance:* `sky_rules_test` asserts the blend; a rendered midday shot shows
shadowed cloud faces as luminous blue-grey, not navy.

**CLD-101 (ground bounce is sunlit-ground bounce).** `GroundRadiance` becomes
`GroundColor × (sun irradiance × max(sunDir.y,0) / π + hemispheric sky)` day-blended — an order of
magnitude brighter at noon than the palette tone it replaces. *Rationale:* audit — cloud bases were lit
by a 0.1-luminance constant; F4 undersides are mid-grey, never black. *Acceptance:* unit test on the
formula; bases in the midday shot read grey, not black.

**CLD-102 (sun ramp tracks the disc, not the sky blend).** The `(1-NightFactor)` factor on
`SunIrradiance` is replaced by a disc-visibility ramp (`smoothstep(-0.06, 0.06, sunDir.y)`), while the
sky's own day blend keeps `smoothstep(-0.10, 0.20, y)`. *Rationale:* audit — direct light lost 34% at
5.7° elevation, killing golden hour. *Acceptance:* `sky_rules_test` updated; sunset shot keeps warm
direct light on cloud sides.

**CLD-103 (vertical ambient column).** The ambient-occlusion column read from the sun-space shadow map
is projected to the vertical by `× clamp(sunDir.y, 0.15, 1.0)`, and the column term fades out over the
outer 10% of the shadow-map UV before the map boundary. *Rationale:* F2 wants the vertical stack; the
slant path is 1/sin(elevation) longer at low sun exactly when direct light is scarce; the hard map edge
is a brightness seam (audit). *Acceptance:* CloudMath test for the projection factor; no visible seam in
a horizon shot.

**CLD-104 (depth-modulated ambient extinction).** `CLOUD_AMBIENT_COLUMN_EXTINCTION` becomes a range:
the effective extinction on the ambient column falls from 0.25 at the cloud surface to 0.05 deep inside,
driven by the profile (F3's `ms_volume` idea through the field we have). *Rationale:* F3 — this is what
keeps cores luminous instead of black. *Acceptance:* CloudMath test asserts monotonic behaviour;
overcast shot keeps deck interiors readable.

**CLD-105 (ShadowTint means shadow).** `ShadowTint` weights the ambient by sun occlusion —
`mix(white, ShadowTint, 1 - exp(-tauSun))` — instead of multiplying all ambient. *Rationale:* audit —
a "shadow" tint that tints lit tops is a global colour cast; presets tinted the whole sky blue.
*Acceptance:* CloudMath test: tint has no effect at tauSun = 0.

**CLD-106 (in-scatter probability takes the profile).** The Nubis2 in-scatter probability receives the
**cheap (unerroded) density** as its density argument, not the fully eroded one; the density seam gains
`CloudDensityProfileAt`-style access so the raymarch can ask. *Rationale:* the shader comment already
concedes this; erosion makes edge densities tiny and the probability crushes exactly the thin lit rims
the silver lining needs (audit). *Acceptance:* march uses the cheap value; silver lining visible in an
into-sun shot.

**CLD-107 (powder fades toward the sun).** The powder term blends to 1 as `cosTheta → 1`
(`mix(powder, 1, smoothstep(0.5, 0.95, cosTheta))`): the dark-edge effect is a reflection-side
phenomenon and must not dim the forward-scattered rim. *Rationale:* audit defect "powder × in-scatter
crush the silver lining". *Acceptance:* CloudMath test: powder factor is 1 within the forward cone.

**CLD-108 (multi-scatter octaves).** Default `MultiScatterOctaves` rises 2 → 3 (component default and
every preset that does not override). *Rationale:* audit — at tauSun ≳ 3 both octaves vanish and
interiors collapse onto ambient alone; published implementations run ≥ 3. *Acceptance:* payload test
updated; storm shot keeps interior luminance.

**CLD-109 (ambient height bias).** Default `AmbientHeightBias` rises 0.5 → 0.75. *Rationale:* audit —
at 0.5 the blend spans only [0.25, 0.75] and the top/base ambient gradient is halved; F8 wants the
separation legible. *Acceptance:* defaults/presets updated together; visible top-vs-base gradient in
the fair-weather shot.

**CLD-110 (Alligator detail volume).** The detail volume becomes **128³** and its four channels become
LF/HF Curly-Alligator (wispy) and LF/HF Alligator (billowy), generated by new periodic
`CloudAlligator` / curl-warped variants in `CloudNoise.glslh`; `CloudDensity.glslh`'s binding comment,
`CloudNoiseVolumes` and the consuming composite keep their existing contracts (F6 formulas already
match ours). *Rationale:* F5 — a 32³ Worley FBM has neither the crevice networks nor the filaments;
at the authored 4 km tile one detail texel was 125 m, which is the putty look. *Acceptance:* CloudNoise
tests cover periodicity, determinism and range of the new functions; detail texel ≤ ~16 m at the
default tile; fair-weather shot shows cauliflower lobes and wispy fringes.

**CLD-111 (detail tile rescale).** Default `DetailTileSize` 4 km → 2 km across component and presets
(Cirrus keeps its larger tile ratio). *Rationale:* with 128³ texels this puts detail features at
~15 m — the scale F7's self-similar billows need; at 4 km/32³ the smallest feature was 125 m.
*Acceptance:* presets updated in the same change; no visible tiling at the horizon in the shots.

**CLD-112 (ambient contribution rebalance).** With CLD-100/101 the raw ambient magnitudes rise;
`AmbientSkyContribution` defaults are re-authored (1.8 → 1.0 baseline, presets rescaled in proportion)
so the final lit:shadow luminance ratio lands near F8's 2.5-4:1 at midday. *Rationale:* audit note —
contrast was carried by hue instead of luminance. *Acceptance:* measured pixel ratio between a lit top
and a shadowed base in the midday shot falls in [2, 5].

**CLD-113 (depth-modulated multiple scattering on the DIRECT term).** The optical depth the
multi-scatter octaves integrate is modulated by the same p.136 pair CLD-104 gave the ambient column,
expressed as a ratio to the surface coefficient so the calibrated case is untouched:
`tau_ms = tauSun * (Remap(cosTheta, 0, 0.9, 0.25, mix(0.25, 0.05, profile)) / 0.25)`, seeded by the
unerroded profile the density sample already returns (CLD-106 — no extra fetch). At profile 0, or for
any view that does not face the sun, the multiplier is exactly 1 and the march is bit-identical to
before. *Rationale:* p.135→136 — light that has already scattered penetrates a core a collimated beam
cannot reach, and CLD-104 applied that insight to the smaller of the two terms it governs.
*Acceptance:* CloudMath asserts the bounds, both monotonicities, the unchanged rim/front-lit cases and
a strict brightening of deep backlit samples against the pre-CLD-113 formula kept as a local reference.
*Measured:* the port is faithful and free, but worth only +0.1 to +0.3 luma (8-bit) in the Storm,
Sunset and UEShowcase backlit shots, with a ceiling of +2.5 measured by forcing the core coefficient
everywhere. The reason is structural and belongs in the record: at `CLOUD_EXTINCTION_PER_WORLD_UNIT`
a dense cloud is opaque within ~40 m, so the samples the modulation acts on sit behind a skin the eye
never sees through, while the samples the eye does see have a small tauSun the modulation is designed
not to touch. Reaching pp. 136/137 therefore needs the ambient side of the audit's §4 division of
labour (AmbientOcclusion 0.55 → 0.85-1.0) as well, which is a separate item.
*Superseded by CLD-114*, which found the structural reason to be one level deeper than the extinction:
not the skin, but the fact that our profile never reaches the values the formula is written against.

**CLD-114 (the dimensional profile is normalised onto the reference's meaning).**
`CloudProfileDepth(profile) = saturate(profile / CLOUD_PROFILE_INTERIOR)` in `CloudGeometry.glslh`,
applied in the two places the deck keys on "profile" — `CloudMultiScatterExtinction` (p.136) and
`CloudAmbientOcclusion` (p.141) — and `AmbientOcclusion` moves 0.55 → 0.95 on the convective presets,
1.00 on Storm, 0.70/0.65 on Stratus/Overcast, becoming a preset-driven field (79, not 78).
*Rationale, measured rather than argued:* Nubis³'s dimensional profile is an authored voxel field that
is **1 throughout a cloud's interior** with a soft falloff over the last voxel or two of its surface
(pp. 82-86). Ours is the VP method's product of three smooth [0,1] factors, and it is small exactly
where the eye looks. The raymarch was instrumented to write the contribution-weighted mean profile of
each ray into the frame and threshold it: over Clouds_UEShowcase and Clouds_Sunset the samples the eye
integrates sit at **0.06-0.20**, isolated cores pass 0.20, essentially nothing reaches 0.30. Both
depth-keyed formulas were therefore evaluated in the bottom fifth of their domain: at profile 0.1,
p.136 gives an effective extinction of 0.23 against its core value of 0.05 (an 8% reduction of the sun
optical depth instead of 80%), and p.141 gives sqrt(1 - 0.1) = 0.95, i.e. **5% occlusion where the
reference's interior gets 100%**. A second instrumented frame confirmed the composite occlusion factor
`local × column` running at 0.6-0.9 over most cloud volume, which is why the strength knob could be
raised without darkening anything. *The extinction itself is NOT the fault:*
`CLOUD_EXTINCTION_PER_WORLD_UNIT` is 0.05 m⁻¹ at density 1 — mid-range for cumulus (0.01-0.1 m⁻¹) —
and view transmittance reaches 0.5/0.1/0.01 at 13.9/46.1/92.1 m, which is what a real cloud does.
*Acceptance:* CloudMath asserts the conversion's bounds, its monotonicity, that a rim wisp keeps >0.8
of its sky while a body sample keeps <0.5, and that the two deck formulas stay inside the paper's own
ranges; the existing CLD-104/113 relations restated at the profile values that mean surface and
interior here. *Measured on 90-frame shots:* lit:shadow (linear, 95th/5th percentile over cloud pixels)
1.89 → 2.74 on Clouds_UEShowcase at 20°, 1.48 → 4.41 on Clouds_PartlyCloudy, 2.17 → 3.87 on the backlit
Storm; the backlit Sunset rim brightens (p95 0.921 → 0.927) instead of darkening. Blankets darken and
had to be retuned below the audit's 0.85 floor — at 0.85 a daylight Overcast base rendered as a black
ceiling (frame mean 0.104 → 0.040), because a blanket's base is lit almost entirely by light
transmitted from above and the octave chain under-carries exactly that; at 0.65 it is 0.104 → 0.087
with the deck's structure now visible in it.

### 10.3 Explicitly rejected in v3

* A vertical summed-density buffer (F2's literal 256×256×32) — CLD-103's projection of the existing
  sun-space map approximates it with zero new passes; the buffer is a named v1.2 option.
* Any change to the march loop structure, the temporal stage, or the composite — the defects are in
  lighting composition and noise character, not in the integrator.
* Porting reference code verbatim — unchanged from §K; formulas above are from the published deck.
