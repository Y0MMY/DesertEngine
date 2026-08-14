# UE5 Sky Atmosphere vs DesertEngine — full research

*Researcher deliverable, 2026-08-14.*

**Sources.** Everything in section 1 and the fog section was read from the actual private
`EpicGames/UnrealEngine` source (branch `release`, fetched 2026-08-14 via `gh api`), not from the paper or
from memory. Files studied, cited below by short name + line number as fetched:

| Short name | Repo path |
|---|---|
| `SkyAtmosphereRendering.cpp/.h` | `Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.{cpp,h}` |
| `SkyAtmosphere.usf` | `Engine/Shaders/Private/SkyAtmosphere.usf` |
| `SkyAtmosphereCommon.ush` | `Engine/Shaders/Private/SkyAtmosphereCommon.ush` |
| `SkyAtmosphereComponent.h/.cpp` | `Engine/Source/Runtime/Engine/{Classes/Components,Private/Components}/SkyAtmosphereComponent.{h,cpp}` |
| `FogRendering.cpp` | `Engine/Source/Runtime/Renderer/Private/FogRendering.cpp` |
| `VolumetricFog.cpp` | `Engine/Source/Runtime/Renderer/Private/VolumetricFog.cpp` |
| `HeightFogCommon.ush` | `Engine/Shaders/Private/HeightFogCommon.ush` |
| `HeightFogPixelShader.usf` | `Engine/Shaders/Private/HeightFogPixelShader.usf` |
| `ExponentialHeightFogComponent.h/.cpp` | `Engine/Source/Runtime/Engine/{Classes/Components,Private/Components}/ExponentialHeightFogComponent.{h,cpp}` |

The published basis of UE's implementation is Sébastien Hillaire, *"A Scalable and Production Ready Sky and
Atmosphere Rendering Technique"* (EGSR 2020) — useful because **we must implement from the paper, not port
Epic's code** (see Open Questions, Q1).

---

## 1. How UE renders Sky Atmosphere

### 1.1 The physical model

The atmosphere is a spherical shell around a planet. All sky math runs in **kilometres**
(`CM_TO_SKY_UNIT 0.00001` — SkyAtmosphereCommon.ush:23) with positions **relative to the planet centre**.
The medium at height *h* above the ground (SkyAtmosphereCommon.ush:321-353, `SampleAtmosphereMediumRGB`):

```
DensityMie = exp(MieDensityExpScale      * h)        // MieDensityExpScale      = -1/1.2 km   (Earth)
DensityRay = exp(RayleighDensityExpScale * h)        // RayleighDensityExpScale = -1/8.0 km   (Earth)
DensityOzo = tent(h; tip 25 km, width 15 km)         // two clamped linear segments (AbsorptionDensity0/1)

ScatteringMie = DensityMie * MieScattering       AbsorptionMie = DensityMie * MieAbsorption
ScatteringRay = DensityRay * RayleighScattering  AbsorptionRay = 0
ScatteringOzo = 0                                AbsorptionOzo = DensityOzo * AbsorptionExtinction
Extinction    = sum of all three extinctions
```

Per-scene constants (uniform struct `FAtmosphereUniformShaderParameters`, SkyAtmosphereRendering.h:40-58):
`MultiScatteringFactor, BottomRadiusKm, TopRadiusKm, RayleighDensityExpScale, RayleighScattering,
MieScattering, MieDensityExpScale, MieExtinction, MiePhaseG, MieAbsorption, AbsorptionDensity0/1
LayerWidth/Constant/Linear terms, AbsorptionExtinction, GroundAlbedo`.

Earth defaults (SkyAtmosphereComponent.cpp constructor):
`BottomRadius = 6360 km`, `AtmosphereHeight = 60 km` (top 6420), Rayleigh β = `(0.005802, 0.013558,
0.033100) /km` (stored as colour × scale 0.0331), Rayleigh scale height 8 km, Mie scattering
`0.003996 /km` white, Mie absorption `0.000444 /km`, `MieAnisotropy g = 0.8`, Mie scale height 1.2 km,
ozone absorption `(0.000650, 0.001881, 0.000085) /km` with tent (25, 1.0, 15), `GroundAlbedo = 0.4`.

Phase functions used in the march (SkyAtmosphere.usf:579-590): Henyey-Greenstein for Mie
(`HenyeyGreensteinPhase(MiePhaseG, -cosθ)`), classic Rayleigh phase `3/(16π)(1+cos²θ)`, and the uniform
phase `1/4π` where no directionality is wanted (transmittance/multi-scattering LUTs).

### 1.2 The single shared integrator

Everything — every LUT and the per-pixel fallback — is one function:
`IntegrateSingleScatteredLuminance` (SkyAtmosphere.usf:448-846). Per step (Frostbite's analytical form,
usf:801-810):

```
S    = Illuminance * (PlanetShadow * TransmittanceToLight * PhaseTimesScattering
                      + MultiScatteredLuminance * Medium.Scattering)
Sint = (S - S * exp(-Extinction*dt)) / Extinction      // analytic integral over the segment
L   += Throughput * Sint
Throughput *= exp(-Extinction*dt)
```

Details that matter for parity:
* **TransmittanceToLight** is a Transmittance-LUT lookup keyed on (sample height, light zenith cos)
  (usf:390-403 → SkyAtmosphereCommon.ush:175-192).
* **Planet shadow** is an analytic sphere test per step (usf:713-714), plus optionally the light's real
  shadow map / Virtual Shadow Map (`SAMPLE_OPAQUE_SHADOW`, usf:720-733 — this is what gives crepuscular
  rays in the sky itself), plus the volumetric-cloud shadow map (`SAMPLE_CLOUD_SHADOW`, usf:739-743) and
  cloud sky-AO on the multi-scattering term (usf:734-738).
* **MultiScatteredLuminance** comes from the multi-scattering LUT (usf:381-388); it is *not* attenuated by
  planet shadow or transmittance (it already represents diffused light — comment at usf:744-745).
* `MultiScatAs1 += Throughput * Scattering * dt` (usf:787-789) is the transfer used to build the
  multi-scattering LUT.
* Sample count is **variable with distance**: `lerp(MinSampleCount, MaxSampleCount,
  saturate(tMax/DistanceToSampleCountMax))`, with a squared (non-linear) sample distribution along the ray
  and per-pixel blue-noise offset (usf:567-643).
* When depth is in front of the atmosphere exit, `tMax` is clamped to the depth-buffer distance — that
  clamping is exactly what turns the same function into **aerial perspective** (usf:540-562).
* Two atmosphere lights maximum (`NUM_ATMOSPHERE_LIGHTS = 2`, second under
  `SECOND_ATMOSPHERE_LIGHT_ENABLED`).
* Optional per-channel **Mie-only / Rayleigh-only** outputs feed separate AP volumes for volumetric-cloud
  compositing (`SEPARATE_MIE_RAYLEIGH_SCATTERING`).

### 1.3 The LUT set — names, sizes, formats, cadence

All LUT passes are **compute**, scheduled by `RenderSkyAtmosphereLookUpTables`
(SkyAtmosphereRendering.cpp:1478-1900), either **before the base pass** on the gfx pipe or **before the
pre-pass on async compute** (`GetSkyAtmospherePassLocation`, cpp:371-379). Formats:
`GetSkyLutTextureFormat` = `PF_FloatRGB` (R11G11B10, or RGBA16F where alpha is needed) (cpp:1284-1296);
`r.SkyAtmosphere.LUT32` upgrades everything to 32-bit float.

| LUT | Default size / format | Scope & cadence | What it stores | Source |
|---|---|---|---|---|
| **Transmittance LUT** | 256×64, FloatRGB (opt. RGBA8 "small format" with `sqrt` encode) | **per scene**, only re-rendered when atmosphere state changes (`r.SkyAtmosphere.StateVersioning`, cpp:1509-1519) | transmittance from a point at height *r* toward zenith angle μ up to the top of atmosphere; 10 samples | cvars cpp:210-233; CS usf:1166-1207; UV mapping ush:156-192 |
| **Multi-Scattering LUT** | 32×32, FloatRGB | per scene, same state-versioned cadence | Ψms: multiple-scattering transfer as function of (sun zenith cos, height); 15 samples; default cheap 2-direction integration, `HighQuality` = 64 uniform sphere dirs; infinite-order sum via `L*(1+f+f²+f³+f⁴)` of `MultiScatAs1` (usf:1321-1329); multiplied by component `MultiScatteringFactor` | cvars cpp:237-255; CS usf:1224-1338 |
| **Distant Sky Light LUT** | **one float4** (structured buffer, PF_A32B32G32R32F) | per scene, every frame | average sky luminance: 64 uniform directions integrated at a fixed 6 km altitude (`r.SkyAtmosphere.DistantSkyLightLUT.Altitude`), reduced in groupshared memory to `Illuminance * 1/4π`. Consumed by **height fog ambient** and cloud ambient | cvars cpp:259-267; CS usf:1428-1519; read helper ush:356-365 |
| **Sky-View LUT** | 192×104, FloatRGB(A) (A = grey transmittance) | **per view**, every frame | full distant-sky radiance in a lat/long-ish parameterisation **non-linearly squeezed around the horizon** (`SkyViewLutParamsToUv`, ush:194-225: `uv.y` split at the exact horizon angle, √-warped on both sides; `uv.x` = azimuth). Built in a "local referential" with Z = up at the camera so the LUT stays undistorted at altitude (usf:1350-1408); variable 4→32 samples | cvars cpp:135-168 |
| **Camera Aerial-Perspective volume** ("CameraAP", froxel LUT) | **32×32×16** 3D texture, RGBA16F (`PF_FloatRGBA`) | per view, every frame, **double buffered** on view state (cpp:1386-1396) | per froxel: `rgb` = in-scattered luminance camera→froxel, `a` = grey transmittance. Depth range **96 km** (`AerialPerspectiveLUT.Depth`), slice distribution **squared** (`Slice *= Slice`, usf:1573-1575, i.e. sampled back with `sqrt`), ≈1-2 ray samples per slice, growing with slice index (usf:1677). Voxels below the horizon get a reconstructed ground-hugging ray so fog on terrain below the horizon stays stable (usf:1586-1640) | cvars cpp:179-198; CS usf:1534-1709 |

Optional extra targets: `CameraAPVolumeMieOnly` / `RayOnly` when volumetric clouds ask for separated
phases (cpp:1385-1405), and dedicated SkyView/360-AP LUTs for the real-time sky-light reflection capture
(cpp:1626-1760).

### 1.4 The sky pass — per-pixel composition

`RenderSkyAtmosphere` (cpp:2155-2254) renders **after the base pass and lighting, onto scene colour**, as
one full-screen triangle per view:

* **Vertex trick**: the triangle is emitted at `Z = StartDepthZ`, the projected AerialPerspectiveStartDepth
  (cpp:2059-2077, usf:856-865), with depth test `CF_DepthNearOrEqual` and **depth write off**
  (cpp:2116-2119). Early-Z therefore skips every pixel closer than the AP start distance for free.
* **Blend state**: `RGB = One × SourceAlpha` (cpp:2113) — the shader outputs premultiplied luminance with
  alpha = **grey-scale transmittance** (`PrepareOutput`, usf:869-890, incl. a clamp to half of fp10 max to
  leave headroom for bloom).
* If the platform supports it and the pass only applies AP (a sky-dome material owns the sky pixels),
  **depth-bounds testing** excludes the far-plane pixels entirely (cpp:2131-2138).

Pixel shader `RenderSkyAtmosphereRayMarchingPS` (usf:910-1147), in order:

1. `DeviceZ == far` → this is a **sky pixel**: add the **sun disc** analytically —
   `GetLightDiskLuminance` = outer-space disc luminance × Transmittance-LUT toward the sun, with a soft
   outer edge `saturate(2(VdotL - cosApex)/(1 - cosApex))` (ush:269-287). The disc is *not* in the
   Sky-View LUT.
2. `FASTSKY_ENABLED` (`r.SkyAtmosphere.FastSkyLUT`, default 1) and camera below the atmosphere top:
   **sample the Sky-View LUT** through the horizon-warped mapping and return (usf:1003-1038).
3. Else — opaque pixel with `FASTAERIALPERSPECTIVE_ENABLED`
   (`r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque`, default 1): reconstruct the world position
   from depth, **sample the CameraAP froxel volume** with `GetAerialPerspectiveLuminanceTransmittance`
   (ush:42-121): slice = `sqrt(distKm / 96km)`; near-camera contribution is faded (`Weight = (2·slice²)`
   below the first half-slice plus a 1 cm fade); output `rgb·Weight`, `a = 1-(Weight·(1-a))`.
4. Fallback (both "fast" paths off, or camera above the atmosphere): full per-pixel
   `IntegrateSingleScatteredLuminance` ray march clamped by scene depth (usf:1072-1144).

Art-direction multipliers are applied here: `SkyLuminanceFactor` on sky pixels only,
`SkyAndAerialPerspectiveLuminanceFactor` inside every LUT integration (usf:971, 1116-1117, 1402-1403).

**Translucency & clouds:** translucent materials sample the *same* CameraAP volume per vertex/pixel
(that is what the volume exists for — cvar help text cpp:200-206). Volumetric clouds get atmosphere applied
either by sampling the AP volume, or per-pixel with `SAMPLE_ATMOSPHERE_ON_CLOUDS` where the trace distance
is the **cloud depth in km** and the result is composited
`CloudLuminance × greyTransmittance + CloudCoverage × AP` (usf:946-958, 1127-1134).

### 1.5 The sun light coupling

`PrepareSunLightProxy` (SkyAtmosphereRendering.cpp): the directional light's *used* colour =
outer-space illuminance × `FAtmosphereSetup::GetTransmittanceAtGroundLevel(sunDir)` — a CPU analytic
evaluation of the same transmittance integral. So **the sun reddens and dims at sunset automatically**;
`TransmittanceMinLightElevationAngle` clamps how far that goes. The same post-transmittance illuminance is
published on the View UB (`AtmosphereLightIlluminanceOnGroundPostTransmittance`) where **height fog** reads
it (HeightFogCommon.ush:334, 344).

### 1.6 How Sky Atmosphere interacts with Exponential Height Fog and Volumetric Fog

Three couplings, all real code paths:

1. **Ambient in-scattering**: height fog adds
   `SkyAtmosphereAmbientContributionColorScale.rgb × View.SkyAtmosphereHeightFogContribution ×
   GetViewDistanceSkyLightColor()` to its in-scattering colour (HeightFogCommon.ush:189-195).
   `GetViewDistanceSkyLightColor()` reads the **Distant Sky Light LUT buffer** (ush:356-365).
   `HeightFogContribution` lives on the *SkyAtmosphere* component (h:158-160); the colour scale on the
   *fog* component (ExponentialHeightFogComponent.h:45-47). Project gate:
   `r.SupportSkyAtmosphereAffectsHeightFog` (cpp:97-102).
2. **Directional in-scattering**: the fog's sun-lobe colour =
   `DirectionalInscatteringColor + HeightFogContribution × AtmosphereLightIlluminanceOnGroundPostTransmittance[i]`
   × `pow(saturate(V·L), DirectionalInscatteringExponent) × 1/4π` for both atmosphere lights
   (HeightFogCommon.ush:328-351).
3. **Composition order**: fog is applied **over** aerial perspective, "because AP is usually optically
   thinner": `Final.rgb = Fog.rgb + AP.rgb × Fog.a; Final.a = Fog.a × AP.a`
   (`GetAerialPerspectiveLuminanceTransmittanceWithFogOver`, ush:123-154). The full-screen height-fog pass
   itself samples the CameraAP volume on opaque via this helper
   (`PERMUTATION_SAMPLE_SKYATMOSPHERE_ON_OPAQUE`, HeightFogPixelShader.usf:156-176), then combines
   volumetric fog (`CombineVolumetricFog`, .usf:184) and multiplies by the light-shaft occlusion mask
   (.usf:185-187).

**Volumetric fog** (VolumetricFog.cpp) is a separate froxel system: camera-frustum 3D grid at
`r.VolumetricFog.GridPixelSize = 16` px/froxel and `GridSizeZ = 64` slices (cpp:118-129), passes
MaterialSetup (density/albedo/emissive injection) → light scattering (with temporal reprojection,
cpp:134-137) → final integration into `IntegratedLightScattering`, which the height-fog pass and
translucency then sample. Height fog's global density feeds it via `VolumetricFogExtinctionScale` etc. It
is out of scope for a first DesertEngine fog, but the parameter block on the component matters for API
shape (see §3).

### 1.7 The full USkyAtmosphereComponent parameter list (as the Details panel groups it)

From SkyAtmosphereComponent.h:56-177 (+ defaults from the .cpp constructor):

* **Planet**: `TransformMode` (PlanetTopAtAbsoluteWorldOrigin | PlanetTopAtComponentTransform |
  PlanetCenterAtComponentTransform), `BottomRadius` ("Ground Radius", km, 6360), `GroundAlbedo`
  (colour, 0.4 linear).
* **Atmosphere**: `AtmosphereHeight` (km, 60), `MultiScatteringFactor` ("MultiScattering", 1.0),
  `TraceSampleCountScale` (advanced, 1.0).
* **Atmosphere - Rayleigh**: `RayleighScatteringScale` (0.0331), `RayleighScattering` (colour),
  `RayleighExponentialDistribution` (km, 8.0).
* **Atmosphere - Mie**: `MieScatteringScale` (0.003996), `MieScattering` (white),
  `MieAbsorptionScale` (0.000444), `MieAbsorption` (white), `MieAnisotropy` (0.8),
  `MieExponentialDistribution` (km, 1.2).
* **Atmosphere - Absorption**: `OtherAbsorptionScale` ("Absorption Scale", 0.001881), `OtherAbsorption`
  (colour), `OtherTentDistribution` {TipAltitude 25, TipValue 1, Width 15}.
* **Art Direction**: `SkyLuminanceFactor` (colour, white), `SkyAndAerialPerspectiveLuminanceFactor`
  (colour, white), `AerialPespectiveViewDistanceScale` (1.0), `HeightFogContribution` (1.0),
  `TransmittanceMinLightElevationAngle` (deg, −90), `AerialPerspectiveStartDepth` (km, 0.1).
* **Rendering (advanced)**: `bHoldout`, `bRenderInMainPass`.
* Notable **Blueprint API**: `OverrideAtmosphereLightDirection`,
  `GetAtmosphereTransmitanceOnGroundAtPlanetTop`, `GetAtmosphericLightToMatchIlluminanceOnGround`
  (h:179-249).

---

## 2. How WE render the sky today

Our sky is an **artistic gradient**, not an atmosphere. There is no scattering, no transmittance, no LUT,
no aerial perspective on geometry. What exists is well-factored and single-sourced, which is the good news
for a rewrite.

### 2.1 The model

`Editor/Resources/Shaders/Common/Atmosphere.glslh` is *the only sky evaluation in the engine* (stated and
true — header lines 1-13). `EvaluateSky(dir, sunDir, sunIntensity, sunDiskRadius, cfg)` (lines 106-169):

* day/night blend `day = smoothstep(-0.10, 0.20, sunUp)`; sunset window from |sunUp|;
* horizon→zenith `smoothstep` gradient + sunset band (Gaussian in elevation `exp(-up²·8)` toward the sun
  azimuth);
* **Gaussian sun disc** `exp(-ang²/r²) · sunIntensity · 0.06` + fixed `pow(cosθ,8)·0.10` halo — no
  transmittance, so the disc neither reddens nor dims with elevation except through the authored
  `sunsetColor` mix (line 145);
* hash-cell **stars** at night; painted **ground tone** below the horizon.

Parameters travel as **7 vec4s** (`SKY_PACKED_VEC4_COUNT`, glslh:54;
`Graphic::SkyGpuPayload` + static_asserts, `Desert/Desert/Source/Engine/Graphic/SkyPayload.hpp:23-48`) in
one SSBO at binding `kSkyPayloadBinding = 1` (SkyPayload.hpp:58).

### 2.2 The passes

| UE concept | Our equivalent | Where |
|---|---|---|
| Sky pixel rendering | `ProceduralSky.shader` full-screen quad, no depth test/write, in-graph `RenderPhase::Sky` (runs **before** geometry; the deferred lighting composite later `discard`s non-geometry texels so the sky shows through) | `Editor/Resources/Shaders/Programs/ProceduralSky/ProceduralSky.shader`; `SkyboxRenderer::RegisterPasses/Render` (`…/Systems/Scene/Skybox/SkyboxRenderer.cpp:226-255`); discard at `Programs/Deferred/DeferredLighting.shader:245-251` |
| Sky-View LUT | **none** — the gradient is cheap enough to evaluate per pixel | — |
| Transmittance LUT | **none** | — |
| Multi-scattering LUT | **none** | — |
| Distant Sky Light LUT | closest analogue: **IBL bake** — `BakeProceduralSky.shader` renders the same `EvaluateSky` into an equirect RGBA32F panorama (512×256/1024×512/2048×1024), then the standard PanoramaToCubemap→irradiance→prefilter chain; **WaitDeviceIdle**, throttled by sun-angle threshold + settle/defer timers | `Programs/Compute/BakeProceduralSky.shader`; `SkyboxRenderer::EnsureProceduralEnvironment` (SkyboxRenderer.cpp:143-224); rules in `Graphic/SkyRules.hpp:120-167` |
| CameraAP froxel volume | **none**. The one aerial-perspective effect in the engine is the **cloud raymarch** calling `EvaluateSky` with sun intensity 0 and lerping the cloud colour toward it by distance (`AtmosphericPerspective` scalar) | `Programs/Clouds/CloudRaymarch.shader:445-465` |
| AP on opaque/translucent | **none** — geometry receives no distance haze at all | — |
| Distant/ambient light for other systems | `AtmosphereEnv` — CPU mirror of the gradient: `SunIrradiance` (elevation-tinted), `ZenithRadiance` (dome-weighted `mix(zenith, horizon, 0.65)`), `GroundRadiance` (Lambertian sunlit-ground bounce), `NightFactor`, `PlanetRadius`; published by `SceneRenderer::GetAtmosphere()`; consumed by the cloud payload | `Graphic/AtmosphereEnv.hpp:28-112`; `Clouds/CloudPayload.hpp:493-514` |
| Sun light transmittance coupling | **none** — the sky's `SunColor×SunIntensity` (radiance in-picture) and the directional light's `Color×Intensity` (surface illuminance) are deliberately independent, documented at `ECS/SkyAtmosphereComponent.hpp:40-48`. Nothing dims/reddens the *light* at sunset except the artist |
| Height fog / volumetric fog | **none**. The only mention of fog in the engine is a comment (`ECS/Components.hpp:422`) noting UE's fog Scattering Intensity has no analogue here | — |

### 2.3 The component

`ECS::SkyAtmosphereData` (`ECS/SkyAtmosphereComponent.hpp:49-152`) — reflected via `REFLECT()/PROPERTY()`
macros, serialized with rfl::json through the component registry. Fields: Enabled, SkyBrightness,
HorizonFalloff; Zenith/Horizon/Ground/Night colours; SunIntensity, SunColor, SunAngularDiameter (deg),
SunGlow, SunsetColor, SunsetIntensity; StarIntensity; time-of-day block (DriveSunFromTimeOfDay, TimeOfDay,
DayLengthSeconds, Latitude, NorthOffset); environment-bake block (AutoRebake, RebakeSunAngleThreshold,
EnvironmentResolution); preset enum; **PlanetRadius (km, 6360)** — already the single planet radius shared
with clouds. Artist units convert exactly once in `MakeSkySettings`
(`Graphic/SkySettings.hpp:57-83`).

Sun direction ownership: the atmosphere sun *light*'s TransformComponent, with the engine's single
negation in `ECS::Rules::AtmosphereSunDirection`; time-of-day is pure math in
`Graphic/SkyRules.hpp:77-108`, pinned by `Desert/Tests/Engine/SkyRules/sky_rules_test.cpp`.

### 2.4 Infrastructure facts that change the cost estimates

These matter because `Docs/Clouds/RESEARCH_ENGINE.md` (§3.3, §3.5) is now partially stale:

* **3D textures exist** — the cloud noise volumes are `Image3D` 128³ RGBA8 (`Clouds/CloudNoiseVolumes.hpp:16-17`),
  so the CameraAP froxel volume can be a genuine `RWTexture3D`/`sampler3D`.
* **Depth is sampled from compute** — the cloud raymarch binds the depth attachment as a sampler with
  `Renderer::ComputeImageBeginRead(depthImage)` handling the layout round-trip
  (`Systems/Scene/Clouds/VolumetricCloudRenderer.cpp:451-466`). The "no depth-aware transition helper" gap
  from RESEARCH_ENGINE §3.3 is fixed.
* The frame has an established hook for "compute + composite after the deferred lighting composite":
  `SceneRenderer::ExecuteVolumetricClouds()` at `SceneRenderer.cpp:571`. A fog/AP pass slots in exactly
  there (same reasoning as RESEARCH_ENGINE §3.2 — anything drawn in-graph before the composite gets
  painted over).
* Per-frame sky state lives in the shared parent material / non-persistent buffers — one copy per
  (frame-in-flight × renderer slot) — so any new LUT that is per-*view* must follow the same pattern
  (SkyboxRenderer.cpp:59-64 and memory note "One SceneRenderer per frame").

---

## 3. Gap analysis

### 3.1 Sky Atmosphere feature table

| UE feature | Our status | What it would take |
|---|---|---|
| Physical participating medium (Rayleigh + Mie + ozone) | **Missing** — artistic gradient | New `AtmosphereSettings` fields on the component; medium sampler in a new `SkyMedium.glslh` (testable as C++); ~1 week incl. tests |
| Single scattering integrator | **Missing** | Port of the paper's integrator into `SkyScattering.glslh`; the cloud raymarch proves the compute idioms exist |
| Transmittance LUT (256×64, cached) | **Missing** | One `ComputePipeline` + RGBA16F `Image2D`; state-versioned re-render (hash the atmosphere block — we already do change-driven work for the IBL bake) |
| Multi-scattering LUT (32×32) | **Missing** | Second compute pass reading the transmittance LUT; cheap 2-direction variant first |
| Sky-View LUT (192×104/view) + horizon-warped mapping | **Missing** | Per-view compute pass each frame; `ProceduralSky.shader` becomes a LUT sampler; mapping functions shared C++/GLSL for tests |
| Sun disc via transmittance | **Partial** — Gaussian disc, no transmittance | Replace the `core·0.06` term with `discLuminance × TransmittanceLUT(view)`; soft edge per ush:282 |
| CameraAP froxel volume (32×32×16) | **Missing** (clouds fake AP by re-evaluating the sky) | 3D image per view (double-buffered per renderer slot), compute fill, `GetAerialPerspective…` sampler in glslh |
| AP applied on opaque | **Missing** | Full-screen pass after `DeferredLightingRenderer::Execute` (new `ExecuteAtmosphericPass()` beside `ExecuteVolumetricClouds`), sampling target depth + AP volume, blend `One × SrcAlpha` |
| AP applied on translucent/clouds | **Partial** (clouds only, non-physical) | Clouds sample the AP volume at their depth instead of `EvaluateSky(sunIntensity=0)` |
| Distant Sky Light LUT | **Partial** — full IBL bake (much more expensive, WaitDeviceIdle, throttled) | Keep the IBL bake for specular; add the 1-texel average-sky compute for fog/cloud ambient — it is cheap enough to run every frame, which removes the "ambient lags the sun" artifact class |
| Sun light colour × transmittance at ground | **Missing** (documented as independent by design) | CPU analytic transmittance (`FAtmosphereSetup::GetTransmittanceAtGroundLevel` equivalent) feeding the directional light path — **needs a teamlead decision** because it reverses a documented engine decision (SkyAtmosphereComponent.hpp:40-48) |
| Two atmosphere lights (sun + moon) | **Missing** — hard limit of one directional light (RESEARCH_ENGINE §7.3) | Out of scope for v1; keep the shader single-light |
| Shadow map / VSM / cloud-shadow sampling inside sky march | **Missing** (cloud shadow map exists for clouds) | Our cloud shadow map (`kCloudShadowMapBinding`) could plug into the Sky-View/AP integrations later — same coupling UE has (usf:739-743) |
| Sky affects height fog | **N/A** — no fog | See §3.2 |
| Planet-scale correctness (space views, horizon below sea level) | **Missing** | The paper's `MoveToTopAtmosphere` + below-horizon voxel handling; low priority for a ground-level engine |
| Holdout / alpha propagation | **Missing** | Skip — compositing-pipeline feature |
| Art direction factors (SkyLuminanceFactor etc.) | **Partial** — SkyBrightness is a crude analogue | Carry `SkyLuminanceFactor`, `SkyAndAerialPerspectiveLuminanceFactor`, `AerialPerspectiveViewDistanceScale`, `HeightFogContribution` on the new component |
| Old artistic look preservation | **We have it, UE doesn't** | Keep the gradient as a mode (see §4, Phase 0 decision) — presets/star field/painted palette are engine personality worth keeping as `SkyMode::ArtisticGradient` |

### 3.2 Fog — what UE has and what a DesertEngine FogComponent needs

**UExponentialHeightFogComponent parameters** (ExponentialHeightFogComponent.h; defaults from .cpp ctor):

* *Exponential Height Fog*: `FogDensity` (0.02), `FogHeightFalloff` (0.2), `SecondFogData`
  {FogDensity 0, FogHeightFalloff, FogHeightOffset} (second exponential term),
  `FogInscatteringLuminance` (colour), `SkyAtmosphereAmbientContributionColorScale` (white),
  `FogMaxOpacity` (1), `StartDistance` (0), `EndDistance` (0 = off; clamps the ray on the XY plane),
  `FogCutoffDistance` (0 = off; beyond it fog is removed entirely).
* *Directional Inscattering*: `DirectionalInscatteringExponent` (4), `DirectionalInscatteringStartDistance`
  (10000), `DirectionalInscatteringLuminance` (black — the sky atmosphere light usually supplies it).
* *Inscattering Texture*: `InscatteringColorCubemap` + angle/tint/`FullyDirectional…Distance` (100000) /
  `NonDirectional…Distance` (1000), `SkyLightCaptureAffectsHeightFogStrength/Roughness`.
* *Volumetric Fog*: `bEnableVolumetricFog`, `VolumetricFogScatteringDistribution` (0.2, phase g),
  `VolumetricFogAlbedo` (white), `VolumetricFogEmissive`, `VolumetricFogExtinctionScale` (1),
  `VolumetricFogDistance` (6000), `VolumetricFogStartDistance`, `VolumetricFogNearFadeInDistance`,
  `VolumetricFogStaticLightingScatteringIntensity`, `bOverrideLightColorsWithFogInscatteringColors`.
* *FSSS (experimental screen-space multiple scattering)*: skip.

**The shader math** — the whole reason exponential height fog is cheap is a *closed-form* line integral.
Density along a ray is `d(z) = GlobalDensity · exp(−HeightFalloff · (z − FogHeight))`. The CPU collapses
the camera-height term once per frame (`InitFogConstants`, FogRendering.cpp:395-413):

```
CollapsedFogParameter = Density * 2^clamp(−HeightFalloff·(ObserverHeight − FogHeight), −125, 126)

ExponentialFogParameters  = (CollapsedFogParameter0, HeightFalloff0, MaxObserverHeight, StartDistance)
ExponentialFogParameters2 = (CollapsedFogParameter1, HeightFalloff1, Density1, Height1)      // second fog
ExponentialFogParameters3 = (Density0, Height0, hasCubemap, FogCutoffDistance)
ExponentialFogColorParameter = (InscatteringColor.rgb, 1 − FogMaxOpacity)
```

The GPU per pixel (`CalculateLineIntegralShared`, HeightFogCommon.ush:207-214, then
`GetExponentialHeightFog` ush:228-418):

```
Falloff      = max(−127, HeightFalloff · RayDirection.z)                       // exp2 domain guard
LineIntegral = RayOriginTerms · (1 − exp2(−Falloff)) / Falloff                 // closed form
             ≈ RayOriginTerms · (ln2 − ½ln²2 · Falloff)                        // Taylor when |Falloff| < 0.01
OpticalDepth = (LineIntegral_fog0 + LineIntegral_fog1) · RayLength
ExpFogFactor = max(saturate(exp2(−OpticalDepth)), 1 − FogMaxOpacity)           // transmittance
DirectionalInscattering = DirLobe · (1 − saturate(exp2(−SharedIntegral · max(RayLength − DirStartDist, 0))))
   where DirLobe = (DirColor + HeightFogContribution·SunIlluminancePostTransmittance)
                   · pow(saturate(V·L), DirExponent) · 1/4π                    // ush:328-368
FogColor = InscatteringColor · (1 − ExpFogFactor) + DirectionalInscattering    // ush:407
return (FogColor, ExpFogFactor)                                               // composited: scene·a + rgb
```

with StartDistance handled by re-deriving `RayOriginTerms` at the exclusion intersection point
(ush:287-306) and the camera height clamped to `MaxObserverHeight` for float safety (ush:246-248).

**Where it runs**: `FDeferredShadingSceneRenderer::RenderFog` (FogRendering.cpp:688) — a full-screen pass
on scene colour **after the sky/AP pass, before translucency**; translucent materials evaluate
`CalculateHeightFog` themselves per vertex/pixel. The fog pass also: samples the CameraAP volume and
composes itself *over* it (HeightFogPixelShader.usf:156-176), merges volumetric fog froxels (:184), and
multiplies by the light-shaft occlusion texture (:185-187 — we have `LightShaftRenderer`, so this hook is
real for us too).

**What a DesertEngine FogComponent needs (v1):**

1. `ECS::ExponentialHeightFogComponent` (reflected/serialized like SkyAtmosphereData) with the
   *Exponential Height Fog* + *Directional Inscattering* groups above (skip cubemap, volumetric, FSSS).
   Fog height comes from the entity's TransformComponent Y, matching UE's use of the component transform.
2. A `FogGpuPayload` (4-5 vec4s: the three packed parameter vectors + inscattering colour + directional
   colour/exponent) mirrored in `Common/HeightFog.glslh` with the offsets static_asserted — the exact
   CloudPayload.hpp pattern. `CollapsedFogParameter` computed CPU-side per frame per view like UE.
3. `Common/HeightFog.glslh` holding `CalculateLineIntegralShared` + `GetExponentialHeightFog` — **pure
   math, no samplers**, so it compiles as C++ for tests (the closed form vs a brute-force numeric integral
   is a perfect unit test).
4. A full-screen `HeightFog` pass reading target depth (or GBufferC world position in Deferred — the
   established pattern per RESEARCH_ENGINE §3.3), run from a new `SceneRenderer::ExecuteAtmosphericFog()`
   right after the deferred composite / before `ExecuteTransparency()` (same slot family as
   `ExecuteVolumetricClouds`, SceneRenderer.cpp:571). Blend: `dst·srcA + src.rgb`.
5. Sun coupling: `DirectionalInscattering` reads `AtmosphereEnv::SunIrradiance` and `SunDirection` — the
   struct already exists and is published per frame. Ambient coupling: v1 uses
   `AtmosphereEnv::ZenithRadiance` where UE uses the Distant Sky Light LUT; when Phase 4 lands (below),
   switch to the real average-sky value. Add `HeightFogContribution` to the sky component and
   `SkyAtmosphereAmbientContributionColorScale` to the fog component to mirror UE's two-sided control.
6. Interaction contract with clouds: fog must apply to the cloud composite too (UE does fog-on-clouds —
   FogRendering.cpp:637 `RenderFogOnClouds`); v1 can approximate by running the fog pass *after* the cloud
   composite so clouds are fogged as "scene", accepting that fog uses cloud-top depth = scene depth.

---

## 4. Architecture proposal — the UE LUT pipeline on DesertEngine idioms

Guiding constraints (all house rules, all already proven by the cloud subsystem):

* **One layout, two languages**: every GPU block gets a `*.glslh` block + C++ `*Payload.hpp` mirror with
  offset static_asserts (`CloudPayload.hpp:150-181` is the template).
* **Compute passes** via `ComputePipeline` with explicit binding constants (`kSky*Binding` — the
  binding-number trap is documented at SkyPayload.hpp:50-58).
* **Testable as C++**: every pure-math glslh gets a `Tests/Engine/.../*Reference.hpp` that `#include`s the
  same file under glm aliases (`Desert/Tests/Engine/CloudTemporal/CloudTemporalReference.hpp:1-53` is the
  template; remember `CI=true premake5 gmake` regenerates the test makefiles). Testable candidates:
  medium sampling, phase functions, both LUT UV mappings (transmittance ↔ params, sky-view ↔ params — UE's
  are exactly invertible pairs, ideal round-trip tests), the scattering integrator itself against a
  brute-force reference, AP slice distribution, and the entire height-fog closed form.
* **Per-view resources** follow the per-renderer-slot rule (Sky-View LUT + AP volume per SceneRenderer;
  transmittance + multi-scatter LUTs can be per-renderer too at first — 256×64 + 32×32 RGBA16F is ~140 KB,
  not worth cross-renderer sharing complexity).
* **ECS**: extend `SkyAtmosphereData` (reflection + rfl::json + SceneMigration default-fill handles old
  scenes); new `ExponentialHeightFogComponent` follows the RESEARCH_ENGINE §2 checklist.

### Phasing — each lands independently and ships a visible improvement

**Phase 0 — component + payload groundwork (no visual change).**
Add the physical parameter groups to `SkyAtmosphereData` (Rayleigh/Mie/Absorption/Art-Direction, UE
defaults from §1.7), plus a `SkyModel` enum: `ArtisticGradient` (today's look, default for old scenes via
migration) vs `PhysicalAtmosphere`. Extend `SkyGpuPayload` — *append, never insert* — to carry the medium
coefficients. Decide `AtmosphereHeight` authored in km like PlanetRadius (one conversion in
`MakeSkySettings`).

**Phase 1 — transmittance + multi-scattering LUTs (cached).**
`SkyMedium.glslh` (medium + phase + UV mappings, C++-tested) → two compute passes owned by
`SkyboxRenderer`, re-run only when the atmosphere block hash changes (UE's StateVersioning). Immediately
usable even before the sky switches model: the **sun disc** and the **directional light tint** can start
reading transmittance.

**Phase 2 — Sky-View LUT + physical sky pass.**
`SkyScattering.glslh` (the integrator) + per-view 192×104 compute fill each frame; `ProceduralSky.shader`
gains the `PhysicalAtmosphere` branch: sample the LUT through the horizon-warped mapping + analytic sun
disc × transmittance. The **IBL bake** works unchanged — `BakeProceduralSky` calls the same evaluation, so
the baked environment and the screen sky stay one model (the file's own contract, Atmosphere.glslh:5-7).
This phase is where the sky *looks* like UE.

**Phase 3 — Camera aerial-perspective volume + apply on opaque.**
Per-view 32×32×16 RGBA16F `Image3D`, compute-filled each frame (squared slice distribution, 96 km
default); new `SceneRenderer::ExecuteAtmosphericFog()` between the deferred composite and
`ExecuteTransparency()` draws AP on opaque from depth. Clouds switch their `AtmosphericPerspective` hack
(CloudRaymarch.shader:445-465) to sampling the volume at cloud depth — removing a known non-physicality
and unifying cloud/terrain haze.

**Phase 4 — Distant Sky Light value + light couplings.**
One-texel average-sky compute (64 fixed directions at 6 km, groupshared reduction) run every frame;
`AtmosphereEnv.ZenithRadiance` gains a physical source; the fog ambient (Phase 5) and cloud ambient read
it. Optionally: CPU analytic ground transmittance → directional light colour (needs decision Q3).

**Phase 5 — ExponentialHeightFogComponent** (independent of Phases 2-4; only Phase 0's `AtmosphereEnv`
is required). Contents per §3.2. If shipped before Phase 3, `ExecuteAtmosphericFog()` starts life as the
fog-only pass and Phase 3 adds AP sampling *under* it (`fog over AP`, UE's exact composition,
ush:147-151).

**Deliberately not ported** (record as non-goals): second atmosphere light, holdout/alpha, mobile paths,
real-time-reflection-capture LUT variants, ortho-projection special cases, FSSS, volumetric (froxel-lit)
fog — the last one is the only likely future phase, and the component fields should reserve its parameter
group names now so scenes don't migrate twice.

---

## 5. Open questions for the teamlead

1. **Licence**: UE source is under the Epic Games EULA — we cannot copy shader/C++ text into DesertEngine.
   Implementation must be written from the **Hillaire 2020 paper** (and its MIT-licensed reference
   implementation, github.com/sebh/UnrealEngineSkyAtmosphere — verify licence header before use) with UE
   used only as a behavioural reference for parameter names/defaults/composition order. Confirm this
   position matches the one taken for the Nubis clouds (`Docs/Clouds/RESEARCH_REFERENCE.md`).
2. **Keep the artistic gradient?** Proposal says yes (SkyModel enum, old scenes migrate to
   `ArtisticGradient`). Alternative is a hard cutover with palette→coefficient approximation, which risks
   every existing scene's look, including Clouds_UEShowcase.
3. **Sun light × transmittance** reverses a documented engine decision (sky radiance vs surface illuminance
   deliberately independent, ECS/SkyAtmosphereComponent.hpp:40-48; one negation ownership). Options:
   (a) UE behaviour — light colour auto-derived, artist authors outer-space illuminance;
   (b) opt-in flag on the light ("Affected By Atmosphere Transmittance");
   (c) keep independent. UE parity argues (a); least-surprise for existing scenes argues (b).
4. **Where does the ambient (IBL) come from in the physical model?** Keep the panorama bake (specular IBL
   needs it anyway) but drive its *cadence* differently: LUT-based sky makes the bake ~free to re-render
   more often, and the Distant-Sky-Light value could replace `EvaluateAtmosphere`'s hand-tuned dome model —
   which the clouds were carefully calibrated against (CLD-100/101/102). Recalibration risk to sign off.
5. **Fog vs Forward path**: in Forward there is still no G-buffer world position; the fog/AP pass would
   read the depth attachment via `ComputeImageBeginRead` (now proven by clouds) or be Deferred-only in v1.
   Which?
6. **AP volume cost**: 32×32×16 RGBA16F double-buffered per live SceneRenderer (previews, thumbnails,
   extra scene views) is tiny (~64 KB×2), but the *fill* is a per-view compute march. Do offscreen
   renderers (thumbnails) get the physical sky at all, or does `SkyModel` fall back to gradient there?
7. **Units**: sky math in km (UE's choice, float-safe) vs our world-unit centimetres. Proposal: shader-side
   km like the clouds already do ("converts to kilometres once, inside the shader",
   CloudPayload.hpp:27-28) — confirm.
