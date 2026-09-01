# Sky & clouds programme — state and what is left

Written 2026-08-18 at the end of a 27-merge programme, so a new session can start from facts rather
than from archaeology. `dev` is at **`cdf09cc5`**, every merge green on macOS + Windows Debug/Release.

> ⚠️ **READ THIS FIRST — 2026-09-01 (Д6).** Hours after this document was written, `03cc7349` deleted
> the **entire volumetric cloud subsystem** (310 files, −40 116 lines) and `622a01a6` rebuilt it from
> nothing; `dev` is now `8c6589ed`, 315 commits later. **§1 has been rewritten** and is current. **§2
> has been re-checked row by row** and each row now carries a verdict. **§3 and §5 have NOT been
> rewritten and are substantially dead** — see the notes at the head of each. Nothing outside §1 and
> §2 has been re-verified against today's tree.

---

## 1. Cloud light parity — one item shipped, two still open

**Rewritten 2026-09-01 (Д6) against `dev` @ `8c6589ed`.** The section this replaces described the work
as in flight, and had been wrong since 2026-08-18. **Items 1 and 2 below are still open and this
section is their only record in the repository** — do not delete it until they are closed.

### The worktree is gone, and none of its code can be merged

`.claude/worktrees/agent-aa8c4f7c0551d7754` was reaped. The teamlead preserved its staged changes on
2026-08-25 as **`163d9fdb`** on branch `worktree-agent-aa8c4f7c0551d7754` — **17 files, +864 / −39**,
not the 19 this section claimed. Which two the count lost cannot now be established; the working tree
is gone and the commit is the whole record. The commit message says in as many words that it was made
durable, not finished, and that nothing in it was verified.

**Do not merge it, and do not try.** Its base `090e016d` is dated 2026-08-18 00:47; **fifteen hours
later the same day**, `03cc7349` (16:08) deleted the entire volumetric cloud subsystem, **310 files and
−40 116 lines**, and `622a01a6` rebuilt it from nothing the next morning. The work was already
un-mergeable a week before the teamlead preserved it.
`git merge-tree dev worktree-agent-aa8c4f7c0551d7754`
conflicts in **13 of the 17 files**, four of them modify/delete because the file no longer exists.
`VolumetricCloudsComponent.hpp`, `Common/CloudShadow.glslh`, `CloudParams.glslh`'s `CloudLayerBlock`,
`CloudDensityCheap`, `CloudSelectLayer` and `kCloudMaxLayers` are all gone, and the payload is
single-layer now. The branch's *reasoning* is worth reading; its code is not portable.

### Which of the three items that commit implements

The question this section asked and never got an answer to: **items 1 and 3. Item 2 is not in it at
all** — it touches no bake, panorama or environment file.

### Item 3 — the vertical summed-density volume: **CLOSED**, by a different design

Р4 (`f2fc535e`) built the sky-light occlusion volume, Р7 (`cf0facf2`) corrected a hemisphere error
inside it, and Р12 (`75fc74e9`) made it the shipped default — `VolumetricCloudComponent.hpp:794`,
`bool SkyOcclusionVolume = true`, at **0.410 ms and a fixed 2.00 MiB per view** (`:773`).

It is not the branch's design and does not resemble it beyond the idea:

| | branch `163d9fdb` | shipped `dev` |
|---|---|---|
| grid | 256 x 256 x layers, four heights packed into one RGBA texel | 128 x 16 x 128 (`CloudSkyOcclusionPayload.hpp:31-32`) |
| footprint | camera-centred square of the layer's Max View Distance | the procedural **modelling volume's own region**, which is exactly periodic and therefore has no outside |
| stores | raw density-length, handed to the existing `CloudAmbientOcclusion` | the cosine-weighted **hemispherical** transmittance `2·E3(τ)`, converted once per texel (`CloudLighting.glslh:175`) |

Two things are worth carrying forward rather than rediscovering.

**The plan's numbers shipped; the branch was the one that departed from them.** This section specified
"~128x128x16 half precision" and the shipped volume is exactly that. Only the *footprint* was
overruled, and `Common/CloudLighting.glslh:264-271` argues against this section by name: a
camera-snapped square is zero outside itself, and at the 7° elevation where the discrepancy is largest
the protocol's own ray runs 18.29–47.27 km through the shell, mostly outside a 30 km square. The
branch's 150 km square only moves that edge outward; the periodic region deletes it.

**Р7's step is in neither this section nor the branch.** `CloudSkyOcclusion`
(`CloudLighting.glslh:210-254`) composes the upper hemisphere into the *sphere* the ambient is a mean
over, instead of scaling one by the other. Without it the deck goes brown at full strength — the sky
light was not attenuated but annihilated, leaving only the reddened sun.

**What is buried with the branch:** the sun-space projection and its `CloudAmbientColumnVertical`
tests — that is, the approach being replaced. Nothing of the replacement is lost.

### Item 1 — per-sample atmospheric sun transmittance: **STILL OPEN**

Nothing on `dev` implements it. `PerSampleAtmosphereTransmittance`, `CloudSunTransmittanceRatio` and
`AtmosphereEnv::TransmittanceLut` appear nowhere in the tree.

The rebuild did not merely leave the defect standing — it landed **precisely on UE's off state**.
`SkyboxRenderer.cpp:552` sets `SunIlluminanceOnGround = OuterSpaceIlluminance * SunTransmittanceAtGround`;
`CloudPayload.hpp:617` packs that as the physical model's sun; it travels as **one** `vec4`
(`CloudPayload.hpp:63`, `SunColour`), which `CloudRaymarch.shader:620` applies to every sample. That is
UE's `SunOuterSpaceIlluminance * T(groundLevel)` exactly: one colour for the whole shell.

**That makes the branch's central design argument obsolete, in the direction of simpler.** The branch
applied a RATIO `T(sample)/T(ground)` (branch `CloudGeometry.glslh:1196`) because our base was then
`SunIrradiance` — the sky's elevation-tinted disc brightness, never multiplied by `T(ground)` — so the
absolute form would have discarded the CLD-100/101/102 calibration. On today's `dev` the physical
path's base **is** `illuminance × T(ground)`, so `illuminance × T(sample)` is directly available and no
ratio is required. Do not inherit that argument unexamined: it was right about a tree that no longer
exists.

The sky half is all still standing: `Programs/Sky/SkyTransmittanceLut.shader`,
`SkyMedium.glslh:214`'s `SkyTransmittanceLutUvFromParams`, and `SkyboxRenderer::m_TransmittanceLut`.
The branch's two sky-side files — `AtmosphereEnv.hpp:126` (publish the handle) and `SkyboxRenderer.cpp`
(publish it only when `m_LutsValid`) — are the only two that **still merge cleanly into `dev`**, and
are worth reading before rewriting. Reusable beside them: `CloudLocalUp` and `CloudAltitudeKm` (branch
`CloudGeometry.glslh:239,253`) — over the shell's 150 km reach the local zenith tilts more than a
degree, so the far deck's sun angle is not the near deck's. Keep the clamp
(`CLOUD_SUN_TRANSMITTANCE_RATIO_MAX`, branch `:1176`) or an equivalent in whatever form is chosen: at
the horizon the quotient of two half-precision transmittances is an infinity.

Component flag, default off as UE ships it, bit-for-bit no-op when off.

**Reconnaissance done 2026-09-01 by the teamlead; nothing implemented, tree left clean.** Four facts
that were not in this section, each verified against today's `dev` rather than reasoned about:

- **The two branch files really do merge, and they build.** Publishing `Image2D* TransmittanceLut` on
  `AtmosphereEnv` and `m_Atmosphere.TransmittanceLut = m_LutsValid ? m_TransmittanceLut.get() : nullptr`
  in `SkyboxRenderer.cpp` (anchor: the line ending `kMultiScatterLutSize );`) were applied, compiled
  warning-free, and then REVERTED — a published handle with no consumer is a dead field (DC §1.3), so
  they belong in the change that reads them and nowhere else.
- **The outer-space illuminance has to be published too, and NOT recovered by division.**
  `AtmosphereEnv` carries `SunIlluminanceOnGround` and `SunTransmittanceAtGround` but not their
  quotient's numerator; it lives in `SunLightFx::OuterSpaceIlluminance` and is used at
  `SkyboxRenderer.cpp:558`. Dividing the two published values back out is what to avoid: at a low sun
  the transmittance reaches zero channel by channel, so the reconstruction is an infinity in whichever
  channel went first — the same hazard the branch's ratio had, arriving through a different door.
- **Descriptor 14 is free** (0-13 are taken; `kCloudSkyOcclusionBinding` is 13, the noise slots are
  3/10/11/12). And the sampler must be bound ALWAYS, gate or no gate, with the fallback-texture pattern
  `kCloudDistantSkyLightBinding` uses: an unfilled descriptor makes the set invalid, and this backend
  answers an invalid set by skipping the whole dispatch — every cloud disappears with nothing logged.
- **`CloudGpuPayload` has no spare slot.** It is packed tight behind a chain of `offsetof` static_asserts,
  and `Aerial` is a `vec3` and deliberately last (that is what keeps the block at 268 bytes instead of
  272). A new `vec4` shifts every offset behind it and the GLSL block has to match exactly.

**And the expectation, written before the work rather than after it.** Р13 (D-31) closed the whole
"change the composition of the noise" avenue with eight measured refusals and named the binding
constraint as a number: a 657 m line integral against a 160 m correlation length. **Item 1 does not move
that number** — it is light along the ray to the sun, not shape. Р9 priced the lighting branch at 16x
extinction for the same 2.8x. A ninth measured refusal is a likely and acceptable outcome here.

### Item 2 — clouds in the IBL bake: **BLOCKED ON ITS CONSUMER, and the cost was never the problem**

Not in the branch, not on `dev`. `ComputeImages::BakeProceduralPanorama` (`ComputeImages.cpp:37`) binds
the sky parameter block and the two atmosphere LUTs and nothing else, and
`Programs/Compute/BakeProceduralSky.shader` includes only `Atmosphere.glslh`, `SkyMedium.glslh` and
`SkyScattering.glslh` — there is no cloud term to switch on. All of that is still true.

**What was NOT true is the sentence that made it worth fixing.** This section said the panorama "is the
sole source of the scene's diffuse irradiance and prefiltered specular", and concluded that the ground
ambient therefore carries a clear sky under an overcast. Measured 2026-09-01 (Р15), by zeroing the baked
panorama in the bake shader and rendering the same scene again:

| scene | render path | pixels changed by `panorama x 0` |
|---|---|---|
| `Clouds_ShadowsOnGround` | Deferred | **0 of 980 480**, three independent runs, byte-identical |
| `SIL_Stratus` | Deferred | **0 of 980 480** |
| `Sky_PhysicalShowcase` | Deferred | **0 of 980 480** |
| `MAT_ProbeShadows` | Forward | 370 579 of 980 480 (37.80 %), max 56/255, bias -7.39 |

The repeat-shot noise floor of every one of those scenes is 0 pixels, so the zeroes are real. The cause
is `Programs/Deferred/DeferredLighting.shader:396` — `vec3 ambient = albedo * ao * (vec3(0.08) +
indirect)`. **The deferred path reads neither cube.** `StaticMeshGBuffer.shader:117-119` declares them
and multiplies them by `1e-20` at `:154-159` purely to keep the reflected descriptor layout identical to
the forward shader. The cubes reach only the forward PBR materials (`MaterialPBRBase.cpp:118-131`) and
`StaticMeshGlass.shader:162`, which is drawn in the Transparency stage in both paths.

**And every scene in the repository that has a `VolumetricCloud` component is Deferred** — all 37 of
them (36 when this was written; one has landed since) carry `RenderingPath: 1`; the only two Forward
scenes are `MAT_ProbeShadows` and `MAT_ProbeUnlitShadows`, and neither has clouds. Re-counted
2026-09-01 by the lead, with one addition the original count missed: **three scenes write no
`RenderingPath` at all** (`MainMenu`, `Terrain_Grass`, `Terrain_MatProbe`) and inherit the C++ default,
which is `Deferred` — so the split is 51 Deferred to 2 Forward, not 50 to 3. A `.desce` carries only
the fields that differ from the defaults, so counting a render path by grepping the scene files alone
undercounts it every time. So a cloud term added to this bake today would be
invisible in every scene the feature exists for. That is a dead setting under DEV_CONTRACT §1.3, and it
is why this item did not ship as written.

**Fix the consumer first.** Until the deferred composite's flat `0.08` becomes the irradiance cube, the
bake has no reader worth feeding. The payoff after that is real and was measured the same way: replacing
the baked dome with a neutral one at 45 % radiance — a stand-in for a stratus deck — moves the same
37.80 % of the Forward frame by up to 28/255 and drops the ground's mean saturation from 0.103 to 0.041.
The blue on the shaded side is exactly what the symptom describes, and it does go away.

**The cost objection is dead, and this is the number that kills it.** The old plan assumed a full march
into the panorama was expensive and reached for a cheap dedicated path. Measured on
`Clouds_ShadowsOnGround`, Debug, MoltenVK, machine shared, minimum of five interleaved runs:

| stage of ONE bake, Medium 1024x512 | ms |
|---|---|
| panorama dispatch (32-sample atmosphere march) | 28.5 (pooled min over 15 runs: 12.2) |
| `PanoramaToCubemap` -> radiance cube | 32.0 |
| `DiffuseIrradiance` (6 144 texels x 65 536 samples) | 301.8 |
| `PrefilterEnvMap` (8.39 M texels x 1 024 samples) | 346.9 |
| **total** | **727.5** |

A bake already costs three quarters of a second and idles the device for all of it; the prefilter alone
is 8.59 G texture samples. `Clouds: March` on the same scene is **1.709 ms** of GPU self time for a
320x192 trace, i.e. **27.8 ns per ray**, so marching the panorama's 524 288 texels at the screen pass's
own full fidelity is **14.6 ms — 2.0 % of one bake**. Against the densest march this project has
recorded (7.589 ms, `Docs/GPU_TIMESTAMPS.md`) it is 64.8 ms, **8.9 %**. The marginal price of one more
sample per panorama texel is **0.0868 ms** (32 -> 512 lever, fixed part ~9.4 ms).

**So when this is picked up: march, do not approximate.** Cost does not discriminate between the three
options, and an analytic cloud dome would be a SECOND model of the clouds beside the march — the mirror
that drifts, which this project has already paid for once in the grey-clouds defect. The bake shader's
own header says why: "the baked environment and the visible sky cannot drift apart."

**Cadence: the sun trigger, extended by a cloud-parameter fingerprint, and explicitly NOT by wind.** The
irradiance cube is a 65 536-sample cosine convolution per texel; it integrates the arrangement of the
field away and responds only to the dome's mean, so advection changes nothing it can see. What does move
it is coverage, cloud type, density, extinction, albedo and the sun. The prefiltered specular's mip 0 is
the one surface that sees arrangement, and it must get the same clouds as the diffuse — they come from
one panorama, and splitting them would be two sources of truth for one sky.

**Two premises of the old plan are now false, not one.** The second, recorded earlier: this section said
the bake "must NOT spin up a second live SceneRenderer (see the one-SceneRenderer-per-frame rule)". That
rule was repealed — renderers lease a slot from `Engine/Core/RendererSlotPool.hpp` and several live views
are legal (`SceneRenderer.hpp:77-83`).

**And the seam this has to cross, for whoever scopes it next.** `EnvironmentManager::CreateProcedural`
has exactly one caller, `SkyboxRenderer.cpp:651`, and `SkyboxRenderer` owns none of the cloud resources —
the params SSBO, the per-renderer modelling volume and the shared noise volumes all belong to
`VolumetricCloudRenderer`, its sibling under `SceneRenderer` (`SceneRenderer.cpp:117` and `:211`). There
is no route from the clouds to the bake that does not add an argument to that call, so the change cannot
be confined to `ComputeImages` + `SceneEnvironment` + the bake shader however it is designed. One more
trap on the way: `CLOUD_PARAMS_BINDING` is 1 (`Common/CloudParams.glslh:27`) and so is
`kSkyPayloadBinding`, so a bake that reads both blocks needs that `#define` to become overridable.

### The calibration anchor moved

This section pointed at `Docs/Clouds/Shots/README.md` for the lit:shadow estimator. That file was
deleted with the subsystem. The current instruments are `Docs/Clouds/CALIBRATION.md` (the six protocol
points; §Р12 covers the shipped occluder) and `Docs/Clouds/DIAGNOSIS_CARTOON.md` (the ranked
discrepancies). Measure on `Clouds_Protocol`.

---

## 2. Known defects, recorded and not fixed

**Re-checked 2026-09-01 (Д6) against `dev` @ `8c6589ed`. Two of the five are dead; one got much worse.**
The original rows are unchanged; the verdict column is new.

| what | where | why it was left | verdict 2026-09-01 |
|---|---|---|---|
| Three hand-written scenes carry **metres-era gravity under a units stamp** — `Fog_Showcase` and `Sky_PhysicalShowcase` say `UnitVersion:1` but `Gravity:9.81`; `MainMenu` has no `Settings` block | scene files | the stamp is lying about them, but changing values is an *edit*, not a migration, and moves a frame | **STILL TRUE, and now 38 scenes, not three.** `SceneSettings.hpp:336` is `Gravity = 981.0f` (cm/s²). 38 `.desce` files carry `UnitVersion:1` with `Gravity:9.81`, and **36 of them were added after this document was written** — new scenes are being cloned from a bad template. Only five carry the cm-era value. `MainMenu`'s sub-claim is **closed**: it has a `Settings` block now (added by the tonemap commits) and no `Gravity` key, so it inherits the correct default |
| **Grass clump texture** baked 512x256 every scene and bound as `u_GrassClump`, which no shader declares any more | `BakeGrassClumpTexture` | not fully dead — the same image doubles as the cull compute's splat fallback, so removing it is a real change | **STILL TRUE, every clause.** `TerrainRenderer.cpp:107` bakes it, `:270` on every init, `:428` binds `u_GrassClump`; no shader declares it (the only three files containing the string are this doc and `TerrainRenderer.{hpp,cpp}`). The splat fallback is still live at `:505` |
| `Collider` / `CharacterController` length fields **not marked `PROPERTY(..., Length)`** although Terrain/Camera/PointLight/SpotLight ones are | reflection | cosmetic today; it is the census the editor's unit-formatted fields use | **CLOSED.** `Components.hpp` — `HalfExtents` (:1591), `Radius` (:1594), `HalfHeight` (:1597), `CharacterController` `Radius` (:1670), `Height` (:1673) all carry `Length`; `Reflection.gen.cpp:569-571,599-600` emit `.IsLength = true` |
| A hero cloud at 13 km still reads **smoother than the deck** around it | voxel path | inherent to phase 1's vocabulary; the distance fade hands the frame back instead | **OBSOLETE.** The voxel path was deleted with the subsystem: `.dvol` has zero references in source, and its design doc `VOXEL_CLOUD_PATH.md` is gone. Hero clouds were rebuilt on `.dcmv` (`HeroCloudComponent.hpp`, `CloudModellingVolumeAsset`). Whether the *rebuilt* path has the same symptom is **untested** — no measurement either way |
| **Storm base measures median 24/255** — dark but structured | `Clouds_Storm` | judged preset taste, not a defect. Revisit only if the owner disagrees | **OBSOLETE.** `Clouds_Storm.desce` was deleted by `03cc7349` along with the whole `Scenes/Clouds/` subdirectory. The measurement has no subject |

---

## 3. The roadmap items nobody has started

> ⚠️ **NOT RE-VERIFIED, AND MOSTLY DEAD (2026-09-01).** Both source documents were deleted by
> `03cc7349`, and all five items below are phrased in the vocabulary of the subsystem that commit
> removed — "Wave 4's Cloud Type", "the voxel path", "the cheap density". The *ideas* may survive the
> rebuild; the items as written do not describe today's code. The current roadmap documents are
> `Docs/Clouds/PLAN_CLOUD_TYPES.md`, `PLAN_AUTHORED_CLOUDS.md`, `PLAN_WEATHER.md` and
> `PLAN_REALISM_AND_AUTHORING.md`. Treat what follows as history, not as a backlog.

From `Docs/Clouds/UE_VOLUMETRIC_CLOUDS_RESEARCH.md` §4 and `Docs/Clouds/NUBIS3_FULL_AUDIT.md` §3
(**both deleted**), in the order they would pay:

1. **Type-as-LUT depth** — Wave 4 gave Cloud Type authored profile curves and per-cell heights. The
   audit's fuller form adds per-cell *bottom/top type indices* so neighbouring cells differ in
   species, not only in height. Cost M.
2. **Detail type from the weather map** (audit item 4) — feed coverage and its local gradient into
   the detail-type ramp, so a growing cell billows and a dissipating one shreds, in one sky. Cost S,
   and the cheapest remaining variety win.
3. **Local envelope entities** (audit item 6) — a placed `CloudMass` (position, radii, type, density)
   evaluated as bottom x top x edge gradients and `max`-ed into the cheap density. The *procedural*
   road to art-directed hero shapes, and a sibling of the voxel path that needs no baking. Cost L.
4. **NVDF import** (audit item 7 / voxel design item 12) — bake our own volumes from a fluid sim or
   the editor's own SDF tools. The seam already speaks the deck's language. Cost L-XL.
5. **Volumetric (froxel-lit) fog** — the one deliberate non-goal from the sky research that is a
   plausible future phase; the component's parameter group names were reserved for it so scenes do
   not migrate twice.

---

## 4. Things a newcomer will otherwise rediscover the hard way

- **Three elevations, never one.** Zenith `--look 0,0.9,-1`, mid `0,0.45,-1`, horizon `0,0.12,-1`.
  The horizon hid two defects through an entire programme.
- **Freeze animation** (`AnimationSpeed: 0`, wind 0) before any pixel comparison, and shoot the same
  build against itself to get the noise floor. These scenes are not byte-reproducible.
- **The camera can move**: `--camera-to`, `--look-to`, `--shot-sequence`, `--shot-every`. A static
  frame proves nothing about the temporal stage. *(2026-09-01: all four still exist. Since this was
  written, `--play` was added — without it the gameplay clock never advances and every captured frame
  is a frozen world, whatever the camera does. `--shot-frames N` sets the warm-up length; shoot at
  `--shot-frames 3` as well as the default when a change touches per-frame-in-flight state.)*
- **The first shot after a SHADER edit is not evidence either** *(2026-09-01, Р15)*. The verify skill's
  "swap a shader instead of rebuilding" A/B has the same trap as a fresh worktree. One run taken
  immediately after editing `BakeProceduralSky.shader` differed from its baseline by **100 % of pixels**
  and never reproduced: five later runs of the identical shader were byte-identical to the baseline. The
  shader cache is content-addressed, so an edited shader misses it and is compiled at load — and that
  first run is the one to throw away. Prime the variant with a throwaway capture, then measure.
- **CI is `cancel-in-progress`** and Windows takes ~35 minutes. A second push while a run is in
  flight cancels it; a cancelled job is not evidence.
- **`git stash` is shared across worktrees** on this machine, and so is the machine itself — timings
  taken while another agent renders measure that agent.
- All three skills (`desert-engine-dev`, `-verify`, `-contract`) were updated on `cdf09cc5` with the
  above and with the diagnosis method that actually found the defects: instrument, knock out one
  contributor at a time, state the mechanism with numbers, build a converged ground truth.

---

## 5. Where the reasoning lives

> ⚠️ **FOUR OF THESE FIVE NO LONGER EXIST (2026-09-01)** — all deleted by `03cc7349`. Only the sky
> research survives. The replacements are listed beneath each dead entry.

- `Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md` — UE's 5-LUT sky pipeline and the height-fog closed form,
  read from real Epic source. **§5 holds binding teamlead decisions.** ✅ **still current.**
- ~~`Docs/Clouds/UE_VOLUMETRIC_CLOUDS_RESEARCH.md`~~ — **deleted.** Replaced by
  `Docs/Clouds/ANALYSIS_APPROACH.md` and `Docs/Clouds/DEV_CONTRACT.md`.
- ~~`Docs/Clouds/NUBIS3_FULL_AUDIT.md`~~ — **deleted.** Replaced by `Docs/Clouds/DIAGNOSIS_CARTOON.md`
  (the ranked discrepancies against the reference) and `Docs/Clouds/LICENCE_RECORD.md`.
- ~~`Docs/Clouds/VOXEL_CLOUD_PATH.md`~~ — **deleted.** The hero-cloud design is now
  `Docs/Clouds/PLAN_AUTHORED_CLOUDS.md`, on the `.dcmv` path.
- ~~`Docs/Clouds/Shots/README.md`~~ — **deleted.** The estimator and the reference values are now in
  `Docs/Clouds/CALIBRATION.md`; the shots themselves are still under `Docs/Clouds/Shots/`.
