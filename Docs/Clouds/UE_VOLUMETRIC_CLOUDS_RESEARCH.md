# UE5 Volumetric Clouds vs Nubis³ vs DesertEngine — full research

Date: 2026-08-14. Researcher pass over Epic's shipped source (`EpicGames/UnrealEngine`,
branch `release`, fetched via authenticated `gh api` today), reconciled against
`Docs/Clouds/NUBIS3_FULL_AUDIT.md` (read first, §3 ranked list is the baseline) and the
sky program in `Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md` (§4 phases, §5 teamlead decisions).

**Licence rule (binding, same position as REF §K and Sky research §5 Q1):** UE source is
Epic-EULA. Everything below is *behavioural* description — parameter names, defaults,
constants, composition order, buffer formats. No shader text may be copied into
DesertEngine; implementations are written from the ideas, our own idioms, and the openly
published papers (Hillaire 2016/2020, Wrenninge, Schneider).

UE files studied end to end (all citations are `file:line` on `release`):

* `Engine/Source/Runtime/Renderer/Private/VolumetricCloudRendering.cpp` (3246 L) + `.h`
* `Engine/Source/Runtime/Renderer/Private/VolumetricRenderTarget.cpp` (1468 L) + `.h`
* `Engine/Shaders/Private/VolumetricCloud.usf` (2485 L), `VolumetricCloudCommon.ush`,
  `VolumetricCloudMaterialPixelCommon.ush`, `VolumetricRenderTarget.usf` (1443 L)
* `Engine/Source/Runtime/Engine/Classes/Components/VolumetricCloudComponent.h` + private `.cpp`
* `Engine/Source/Runtime/Engine/Classes/Components/DirectionalLightComponent.h` + `.cpp`
* `Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionVolumetricAdvancedMaterialOutput.h`,
  `MaterialExpressionCloudLayer.h`
* Consumers of the cloud shadow map, located by code search: `DeferredLightPixelShaders.usf`,
  `SkyAtmosphere.usf`, `VolumetricFog.usf`, `BasePassVertexShader.usf`,
  `TranslucentLightInjectionShaders.usf`, `ReflectionEnvironmentPixelShader.usf`,
  `Lumen/LumenSceneDirectLightingShadowMask.usf`, `MegaLights/MegaLights.ush`

---

## 1. UE's volumetric cloud pipeline, as shipped

### 1.1 The architectural split: a 20-field component, a material that owns the density

This is the headline structural difference from our system. `UVolumetricCloudComponent`
owns **only geometry and ray budgets** — the complete field list
(VolumetricCloudComponent.h:34-153): `LayerBottomAltitude` (km, default 5),
`LayerHeight` (km, default 10), `TracingStartMaxDistance` (350 km),
`TracingStartDistanceFromCamera` (0), `TracingMaxDistanceMode`
(DistanceFromCloudLayerEntryPoint | DistanceFromPointOfView), `TracingMaxDistance`
(50 km), `PlanetRadius` (6360 km, only used when no SkyAtmosphere is present),
`GroundAlbedo` (linear 0.4), the `Material` soft pointer,
`bUsePerSampleAtmosphericLightTransmittance` (off), `SkyLightCloudBottomOcclusion` (0.5),
four sample-count scales, `ShadowTracingDistance` (15 km),
`StopTracingTransmittanceThreshold` (0.005), four AP art-direction distances, and three
rendering flags (holdout / main pass / real-time sky captures). Defaults in
VolumetricCloudComponent.cpp:38-73.

**Everything we hold in our 78-field `VolumetricCloudsComponent` — density, noise,
erosion, coverage, type, phase, multi-scatter, powder, ambient — lives in a Volume-domain
material** in UE. The tracing loop is a *host* that repeatedly evaluates a material graph:

* Each march step calls `UpdateMaterialCloudParam` → `CalcPixelMaterialInputs`
  (VolumetricCloud.usf:857, 903-907). The material returns per sample:
  **Extinction** (from the Subsurface/`VOLUMETRICFOGCLOUD_EXTINCTION` input, clamped to
  [0, 65000] m⁻¹, usf:109-121), **Albedo** (BaseColor, saturated, usf:135-147),
  **EmissiveColor** (usf:123-133), and optionally **AmbientOcclusion** (usf:149-159).
* The sample position is delivered to the graph through material parameters set by the
  host: `CloudSampleAltitude`, `CloudSampleAltitudeInLayer`,
  `CloudSampleNormAltitudeInLayer`, `ShadowSampleDistance`
  (VolumetricCloudMaterialPixelCommon.ush:60-75), exposed to artists as the
  `CloudSampleAttribute` expression (MaterialExpressionCloudLayer.h:11-21).
* A `VolumetricAdvancedOutput` custom-output node feeds the *lighting model knobs* from
  the graph (MaterialExpressionVolumetricAdvancedMaterialOutput.h:17-92): PhaseG /
  PhaseG2 / PhaseBlend (per-pixel or per-sample, artist's choice, usf:767-782 / 909-920),
  MultiScatteringContribution / Occlusion / Eccentricity (per-pixel, usf:744-756),
  **ConservativeDensity** (float4; x = cheap conservative density evaluated *before* the
  full graph, usf:66-70 of the pixel-common ush), plus static bools: octave count (0-2),
  GroundContribution, GrayScaleMaterial (compile the whole loop as scalar — an
  optimisation we should note), RayMarchVolumeShadow, ClampMultiScatteringContribution.
* A second custom output, `VolumetricCloudEmptySpaceSkippingOutput.ContainsMatter`,
  feeds the empty-space structure (§1.6).

The shipped default material `m_SimpleVolumetricCloud_Inst` is a binary asset
(soft-referenced at VolumetricCloudComponent.cpp:74; the comment calls it "the relatively
heavy default material with few 3D textures") — i.e. even Epic's out-of-the-box look is a
graph over tiling 3D noise + a height envelope, architecturally the same *class* of
density model as our procedural VP method, just authored in the graph instead of C++.
Every shader permutation above (`MATERIAL_VOLUMETRIC_ADVANCED_*`) is compiled from what
the artist connected. **Consequence:** UE ships no fixed cloud shape model at all. The
quality of any given UE sky is the quality of its material; Epic's engine contribution is
the *host* — temporal amortization, shadow maps, atmosphere coupling — which is exactly
the part our audit rated us weakest against no one (we built it ourselves) and the part
this report mines.

### 1.2 Layer & planet geometry

Same world model as ours: a spherical shell between `BottomRadiusKm = PlanetRadius +
LayerBottomAltitude` and `TopRadiusKm = Bottom + LayerHeight`
(VolumetricCloudRendering.cpp:1760-1762), centered on the SkyAtmosphere planet center
when present. Entry/exit by two sphere intersections with careful inside/below/above case
handling (usf:527-560); depth-buffer clamp of TMax and skip-if-occluded (usf:588-639);
`TracingStartMaxDistance` rejects rays whose layer entry is beyond 350 km (usf:576-580);
`TracingMaxDistance` caps the *in-layer* marched length at 50 km with a mode toggle for
distance-from-camera (usf:668-703). No second shell, no cloud dome: one layer, one
component per scene.

### 1.3 The step schedule — uniform steps, count ∝ trace length

UE does **not** use adaptive step growth. The whole schedule is
(VolumetricCloudRendering.cpp:52-70, component.h:235; usf:709-718):

```
SampleCountMax = min(96 × ViewSampleCountScale, r.VolumetricCloud.ViewRaySampleMaxCount = 768)
StepCount      = max(SampleCountMin = 2, SampleCountMax × saturate(traceLength / 15 km))
StepT          = traceLength / StepCount            // UNIFORM per ray
```

So at defaults: a ray crossing 15 km of layer gets 96 samples → **156 m uniform steps**;
a 5 km crossing gets 32 samples → 156 m; the density never changes with distance along
the ray. `r.VolumetricCloud.SampleClampCount` (default INT_MAX) exists purely to cut long
GPU waves (cpp:62-65). Ray start is jittered by **blue noise over the full step length**,
re-seeded per frame with `StateFrameIndexMod8` (usf:797-806), and the reflection path is
un-jittered. Early-out when all channels of view transmittance drop below
`StopTracingTransmittanceThreshold` = 0.005 (usf:1442-1446).

**Why 156 m steps look fine in UE:** the burden is carried by resolution amortization +
temporal integration (§1.4) and by TAA over the jittered start. This is the *opposite*
philosophy to the Nubis deck (1-8 m steps, per-pixel, √-growth) — see §2's table and the
roadmap's item 1 discussion, because this disagreement is the most consequential one for
us.

### 1.4 Temporal scheme — the Volumetric Render Target

Clouds render into a dedicated buffer chain, not the scene (VolumetricRenderTarget.cpp):

* **Modes** (`r.VolumetricRenderTarget.Mode`, default 0, cpp:32-35, 121-151): mode 0 =
  trace at ¼ view res, *reconstruct* at ½ view res, then upsample; mode 1 = trace ½,
  upsample; mode 2 = trace ¼, reconstruct full res; mode 3 = cinematic full res.
* Each frame traces **all** texels of the tracing buffer, but each traced texel maps to
  one position of a 2×2 (or 4×4 in mode 2) quad in the reconstruction buffer, advanced
  per frame in **Bayer order** — `{0,2,3,1}` for 2×2, `{0,8,2,10,…}` for 4×4
  (cpp:306-322). So at default mode 0 exactly ¼ of the half-res buffer (= 1/16 of screen
  pixels) receives fresh trace data per frame.
* The other texels are **reprojected from history using the cloud front depth** the trace
  wrote (km, transmittance-weighted mean sample depth, usf:1225-1236, 1728-1732).
  Reconstruction heuristics (VolumetricRenderTarget.usf:338-464): use-new-sample on
  disocclusion (history scene depth differs > 2 km), on large per-pixel min/max cloud
  depth spread near geometry ("removes cloud over trees"), skip disocclusion logic
  entirely when everything is beyond `MinimumDistanceKmToDisableDisoclusion` = 5 km
  (cpp:67-70) — distant clouds are treated as a stable layer, blended without upsampling.
  `MinimumDistanceKmToEnableReprojection` (default 0) can disable reprojection for close
  clouds when flying through the layer (cpp:62-65). Optional neighbourhood clamp
  (`ReprojectionBoxConstraint`, default 0 — Epic ships it OFF, cpp:57-60).
* Final composition upsamples with **bilateral upsampling** (UpsamplingMode 4 default of
  0-4, cpp:37-40) guided by the cloud front-depth texture; the min of a 2×2 gather of
  front depth is used to avoid reprojection trails (VolumetricCloudCommon.ush:88-95).
* A `CLOUD_MIN_AND_MAX_DEPTH` permutation traces **two result layers per pixel** (color +
  closest-part color, usf:501-507, 592-620) against a half-res min/max depth pyramid, so
  one trace serves both "behind the tree" and "in front of the tree" — that is UE's
  answer to cloud/geometry intersection at low trace res.
* Fog/AP can be applied **late, during upsampling** (`r.VolumetricCloud.ApplyFogLate`,
  default 1, VolumetricCloudRendering.cpp:102-105) so reconstruction-lagged cloud pixels
  still get this-frame fog.

Comparison with ours: our checkerboard + neighbourhood-clamped reprojection + bilateral
upsample (`CloudTemporal.glslh`, `CloudTemporalResolve.shader`, `CloudComposite.shader`)
is the same family; UE's distinguishing upgrades are (a) the *Bayer-sequenced quad
refresh* making the refresh cadence deterministic and evenly distributed, (b)
reprojection keyed on **cloud front depth** rather than screen motion alone, and (c) the
distance-based disocclusion bypass that stops the resolve from chewing distant layers.

### 1.5 Direct lighting — multi-scatter octaves, phase, shadowing

The participating-media model (usf:339-432) is Wrenninge's multi-scatter octave
approximation, cited in the source comment to the Oz paper:

* `MSCOUNT = 1 + octaves`, octaves authored 0-2 in the material node (header:62-64),
  **default 0** — i.e. the base UE cloud is single-scattering unless the artist opts in.
* Octave chain (usf:387-399): scattering ×= `MsScattFactor`, extinction ×=
  `MsExtinFactor`, and *each factor squares itself each octave* (a, then a², cumulative
  a³) — a faster falloff than our constant-ratio 1.0/0.5/0.25 chain. Phase per octave
  blends toward isotropic: `lerp(Isotropic, basePhase, MsPhaseFactor)` with the factor
  also squared per octave (usf:413-432).
* **Phase**: dual-lobe HG, `Phase = lerp(HG(g), HG(g2), blend)` (usf:327-336) — a `mix`,
  not the classic `max`. Our audit §5.4 worried our `mix` was non-canonical; **UE uses
  `mix` too** — two of three references now agree with our form.
* **No powder term and no in-scatter probability anywhere in the loop.** Dark-edge/rim
  behaviour is left to the material (via AO output or density authoring). We and Nubis
  are richer here.
* **Shadowing toward the sun, two modes** chosen by the material's
  `bRayMarchVolumeShadow` (default true):
  * *Ray-marched* (usf:1078-1148): per shaded sample, a secondary march over
    `ShadowTracingDistance` (default 15 km!) with `10 × ShadowViewSampleCountScale`
    samples (component.h:236), **x²-distributed** so density is concentrated near the
    shaded point (usf:1095-1107) — the same idea as Nubis' near-weighted cone. Per-octave
    accumulators apply reduced extinction per octave (usf:1137-1148): the deep-core
    brightening lives *here* in UE, in the shadow term, exactly where the deck's p. 136
    formula operates.
  * *Shadow-map* (usf:1069-1077): sample the cloud shadow map (§1.7) once —
    `transmittance = exp(-OutOpticalDepth × MsExtinFactorᵐˢ)`. Cheaper, "infinite"
    distance, greyscale (the material node's own tooltip, header:86-88).
* **Shadows of the world onto clouds**: when the light has `bCastShadowsOnClouds`, the
  march multiplies per-sample light transmittance by the light's opaque shadow map (CSM +
  Virtual Shadow Map, usf:1038-1058) — mountains shadow the cloud deck at sunset.
* **Local lights** (point/spot/rect) can light clouds behind
  `r.VolumetricCloud.EnableLocalLightsSampling` (default 0, "for cinematics",
  cpp:242-249; usf:1242-1352) through the light grid with short shadow marches.
* **Integration**: Frostbite/Hillaire energy-conserving analytic step integration
  (usf:1396-1408) — identical family to our `CloudIntegrateInScatter`.
* **Emissive** is an additive luminance source in the integral (usf:1381-1384).

### 1.6 Conservative density & empty-space skipping

Two mechanisms, both material-fed:

1. **Conservative density** (default path, always on when the material wires the pin):
   before the full graph runs at a sample, the cheap `ConservativeDensity.x` is
   evaluated; if ≤ 0 the loop `continue`s, advancing
   `r.VolumetricCloud.StepSizeOnZeroConservativeDensity` steps (default 1, cpp:77-80;
   usf:860-877). The same skip is inside every secondary shadow march (usf:1110-1117).
   This is UE's equivalent of our two-tier miss-counting march — theirs skips the
   *material evaluation*, ours grows the *step*.
2. **Empty-space skipping** (EXPERIMENTAL, default OFF — `r.VolumetricCloud.
   EmptySpaceSkipping` = 0, cpp:254-257): a compute pre-pass over a low-res screen tile
   grid marches froxel slices over `VolumeDepth` = 40 km (cpp:259-262), evaluates the
   material's `ContainsMatter` output at slice center + 8 corners, and groupshared-min-
   reduces a per-tile **start-tracing distance** texture (usf:1849-2044). The view trace
   then fast-forwards `t` to that distance with fixed step size to keep sample alignment
   (usf:808-831). Note it only skips *up to the first cloud*, not gaps between clouds —
   weaker than an SDF, and Epic does not ship it on.

**Finding for us:** UE's shipped default relies on cheap-density skipping plus the small
uniform sample count, not on a spatial structure. Our two-tier march is already at parity
with UE's *shipped* behaviour; the froxel structure is not worth copying (Epic itself has
it off by default and marked experimental).

### 1.7 Cloud shadow maps — and shadows CAST ON the world (confirmed)

Per atmospheric light with `bCastCloudShadows` (default **off**,
DirectionalLightComponent.cpp constructor; params at DirectionalLightComponent.h:184-241):

* **Projection**: orthographic, sun-facing, centered on the planet surface point under
  the camera, extent `CloudShadowExtent` = **150 km radius**, snapped to a
  `r.VolumetricCloud.ShadowMap.SnapLength` = 20 km world grid *and* to the shadow-map
  pixel grid (anti-shimmer, VolumetricCloudRendering.cpp:1817-1880). Far plane = 4× the
  extent radius along the light.
* **Resolution**: 512 × `CloudShadowMapResolutionScale`, clamped by
  `r.VolumetricCloud.ShadowMap.MaxResolution` = 2048 (cpp:425-432) → default texel
  ≈ 586 m over 300 km.
* **Content — the important design**: the trace (usf:2048-2216) marches the whole layer
  along the light with 16×scale samples (up to `RaySampleMaxCount` = 128, ×2 near the
  horizon via `RaySampleHorizonMultiplier`, cpp:1889-1895) and stores **three channels**
  (usf:2209-2215): `r` = front depth km (first sample with medium), `g` = mean extinction
  along the lit part, `b` = max optical depth. A receiver reconstructs transmittance
  *analytically at its own depth*:
  `OD = meanExt × max(0, sampleDepth − frontDepth); OD = min(OD, maxOD); T = exp(−OD)`
  (VolumetricCloudCommon.ush:54-68). One 2D texture therefore shadows **any point in 3D**
  — ground, mid-air, atmosphere — with depth-correct penumbra-ish falloff. Our 4-slice
  sun-space map approximates the same 3D query with slices; UE's front-depth + mean-σ
  encoding is cheaper (RGB16F 512²) and has no slice quantisation.
* **Filtering**: spatial pass = 2× downsample from a half-res trace where filtered front
  depth = mean − |deviation| (usf:2330-2345); optional temporal accumulation exists but
  is **disabled by default** (`TemporalFiltering.NewFrameWeight` = 1.0 ⇒ off,
  cpp:215-218, 1743-1744), with history cut when the light rotates > 10°
  (cpp:220-224).
* **Consumers — this is "shadows cast on the world", verified in source**:
  * Deferred lighting of every opaque pixel: `Out *= lerp(1, GetCloudVolumetricShadow(…),
    CloudShadowmapStrength)` (DeferredLightPixelShaders.usf:198-204), strength =
    `CloudShadowOnSurfaceStrength`.
  * Forward base pass (BasePassVertexShader.usf), translucency lighting volume injection
    (TranslucentLightInjectionShaders.usf), volumetric fog light scattering
    (VolumetricFog.usf), reflection environment, Lumen scene direct lighting, MegaLights
    — the same 3-channel map read everywhere.
  * **The Sky Atmosphere raymarch itself** (SkyAtmosphere.usf:739-743, 770-774):
    `PlanetShadow ×= GetCloudVolumetricShadow(...)` per AP/sky-view sample, strength =
    `CloudShadowOnAtmosphereStrength` — this is what produces UE's colossal-scale
    crepuscular rays *in the sky itself*, not just as a screen-space light-shaft post.
* **Separate strengths**: `CloudShadowStrength` (on clouds via the map),
  `CloudShadowOnAtmosphereStrength`, `CloudShadowOnSurfaceStrength` (h:199-213) — artists
  dose each receiver class independently.

### 1.8 Ambient — much simpler than ours — and the SkyAO map

* Per-sample sky ambient is a **constant luminance**: `GetViewDistanceSkyLightColor()` —
  the Sky Atmosphere's Distant Sky Light value (computed at 6 km; our Sky research §1.5
  covers it) — with two TODOs in the comments admitting no occlusion and no
  directionality (usf:720-739). It is attenuated only by a linear bottom gradient
  `saturate(SkyLightCloudBottomVisibility + normAltitude)` (usf:945-954), or the
  material's AO output when wired. It is added **only to the ms=0 octave**, with the
  comment that including it in MS octaves "would make clouds look flat" because occlusion
  is not handled (usf:1372-1375).
* **Ground contribution** (material opt-in `bGroundContribution`): a 5-sample
  x²-distributed march **straight down** accumulates optical depth, then adds a Lambert
  ground bounce `NdotL × GroundAlbedo/π × 2π × IsotropicPhase` lit by the (transmittance-
  tinted) suns, attenuated by `exp(−OD)` (usf:967-1036). Same physics as our CLD-101
  sunlit-ground term, but with a *marched* occlusion column below the sample where we use
  a height blend.
* **Cloud SkyAO map** (for the *world*, not for the clouds): a second top-down ortho map
  (512 × scale, R11G11B10, `CloudAmbientOcclusionExtent/Strength/ApertureScale` on the
  Sky Light; cpp:470-517, 1914-1964), traced straight down with 10 samples, then filtered
  by a **64-sample hemisphere integration at ground level** — visibility
  (`exp(−maxOD)`), not extinction, is averaged over an artist-scaled aperture
  (usf:2279-2326). Consumed by the sky light on opaque and by the atmosphere's
  multi-scattered term (SkyAtmosphere.usf:734-738). This is "clouds darken the ground's
  sky light", a system we have no equivalent of.

**Verdict vs ours**: our cloud-*receiving* ambient (dome-weighted zenith + ground bounce
+ profile-based local occlusion + shadow-map column, audit §4) is *substantially richer
than UE's*, and the audit's plan to keep it stands. What UE has that we lack is ambient
*cast by* clouds onto the world (SkyAO map) — a new roadmap item.

### 1.9 Atmosphere coupling — per-sample sun colouring, AP on clouds, sun disc

* **Sun illuminance, two quality levels** (usf:645-650, 922-939):
  * Default: `AtmosphereLightIlluminanceOnGroundPostTransmittance` — the sun already
    tinted by ground-level atmosphere transmittance, uniform for the whole trace.
  * `bUsePerSampleAtmosphericLightTransmittance`: use *outer-space* illuminance and
    multiply per sample by `GetAtmosphereTransmittance(samplePlanetPos, lightDir)` from
    the **transmittance LUT** (usf:923-930) — every sample gets the transmittance of *its
    own altitude and horizon geometry*. This is what makes UE sunsets graze the cloud
    field with reddening that varies across kilometers of deck. The comment at
    component.h:76-77 notes it is on the cloud component (not the light) to limit shader
    permutations, and that it is an art/look decision. Requires a SkyAtmosphere in scene
    (VolumetricCloudRendering.cpp:519-523).
* **Aerial perspective ON clouds**: sampled from the camera AP volume **once per pixel**
  at the transmittance-weighted mean cloud depth `tAP` (usf:1225-1236, 1489-1541),
  composed as `L = AP.rgb × cloudCoverage + AP.a × L` — AP *over* the cloud. Optional
  split into **Mie-only and Rayleigh-only AP volumes** with separate artist start/fade
  distances (component.h:129-140; usf:1520-1529) — the art knob that keeps Mie haze off
  nearby clouds while distant decks still blue-shift. `r.VolumetricCloud.
  HighQualityAerialPerspective` (default 0) traces a second per-pixel AP pass instead
  (cpp:82-85). Height fog + volumetric fog + local fog volumes are applied over the cloud
  the same way (usf:1564-1616).
* **Sun disc occlusion**: the disc is rendered by the sky pass as outer-space luminance ×
  transmittance LUT (SkyAtmosphere.usf:357-375); clouds are a premultiplied buffer
  composited **over** the sky, so `cloud.a` (mean transmittance, usf:1716) occludes the
  disc with no special code. Identical topology to ours; bloom/light-shafts read the
  post-cloud buffer. Nothing to port beyond what Sky Phase 2 already plans.
* **Clouds in the IBL**: the same trace CS renders into real-time sky-capture **cubemap
  faces** (`TargetCubeFace`, usf:1799-1811; `bVisibleInRealTimeSkyCaptures`,
  `ReflectionRaySampleMaxCount` = 80, `ReflectionViewSampleCountScale`) — the sky light
  the world receives contains the clouds. Reflection traces use ~80-sample budgets and
  no jitter (usf:798).

### 1.10 The cvar lever board (perf-relevant, with defaults)

| Lever | cvar | Default |
|---|---|---|
| Trace/reconstruct resolution | `r.VolumetricRenderTarget.Mode` | 0 (trace ¼, reconstruct ½) |
| Upsample filter | `r.VolumetricRenderTarget.UpsamplingMode` | 4 (bilateral) |
| View samples | `r.VolumetricCloud.ViewRaySampleMaxCount` | 768 (base 96 × scale) |
| Sample distribution distance | `r.VolumetricCloud.DistanceToSampleMaxCount` | 15 km |
| Secondary shadow samples | `r.VolumetricCloud.Shadow.ViewRaySampleMaxCount` | 80 (base 10 × scale) |
| Shadow map resolution | `r.VolumetricCloud.ShadowMap.MaxResolution` | 2048 (base 512) |
| Shadow map samples | `r.VolumetricCloud.ShadowMap.RaySampleMaxCount` | 128 (base 16, ×2 horizon) |
| Shadow map temporal | `…ShadowMap.TemporalFiltering.NewFrameWeight` | 1.0 (= disabled) |
| SkyAO | `r.VolumetricCloud.SkyAO` (+ MaxResolution 2048, TraceSampleCount 10) | 1 (needs Sky Light opt-in) |
| Empty-space skip | `r.VolumetricCloud.EmptySpaceSkipping` | 0 (experimental) |
| Skip stride on zero conservative density | `r.VolumetricCloud.StepSizeOnZeroConservativeDensity` | 1 |
| Early out | component `StopTracingTransmittanceThreshold` | 0.005 |
| Local lights | `r.VolumetricCloud.EnableLocalLightsSampling` | 0 |

All `ECVF_Scalability` — Epic's quality tiers move these, which matches our
quality-tier-owns-knobs rule (CloudPresets.hpp:107-116).

---

## 2. Three-way comparison table

Legend: **=** agreement, **≠** disagreement; "audit→N" = NUBIS3_FULL_AUDIT §3 item N
already recommends the winning side.

| Subsystem | UE 5 (shipped) | Nubis³ (deck) | Ours (dev) | Who disagrees, who's right for us |
|---|---|---|---|---|
| Geometry | One spherical shell, bottom+height km, planet-coupled (cpp:1760-62) | 4 km authored voxel domain + separate distance dome (p. 54, 76) | One spherical shell, 150 km march extent | UE = ours ≠ Nubis. Keep shell; Nubis' *dome* idea survives as audit→5 (second layer). |
| Density authoring | **Material graph**; engine ships no density model (usf:900-907) | Authored NVDF voxel fields + VP-method weather maps | C++/GLSL procedural VP method, 78-field component | Three different answers. UE's lesson is the *separation* (host vs density), not the graph itself — see §5 Q2. |
| Cloud type / profile | Whatever the graph does with `CloudSampleNormAltitudeInLayer` (ush:64) | Authored Top/Bottom Type LUT columns + per-column Min/Max heights (p. 19) | 3 fixed trapezoids × 1 scalar | UE is agnostic; Nubis wins; audit→3 stands unchanged. |
| Detail/erosion | Material's business; default material = "a few 3D textures" (cpp comment:73) | Alligator/curly noise, full-strength ValueErosion at 1-8 m sampling | Same formulas at 0.38 strength, 60-500 m features | UE offers no evidence either way; audit→2 stands on Nubis' authority. |
| Step schedule | **Uniform** step; count = 96 × saturate(len/15 km), 156 m typical (usf:710-713) | `max(1, √d×0.08)` m adaptive, 1-8 m (p. 163) | 15 m + 0.008d linear growth, 700 m cap | **UE ≠ Nubis, biggest disagreement.** Audit→1 assumed Nubis; UE proves an alternative: coarse uniform steps + heavier temporal amortization. Roadmap item 1 is revised to a hybrid (see §4). |
| Temporal | Bayer-sequenced quad refresh (1/16 px/frame), front-depth reprojection, disocclusion heuristics, bilateral upsample, late fog (VRT.cpp:306-329, VRT.usf:338-464) | Checkerboard + near/far res split at 200 m (pp. 49-50, 171-174) | Checkerboard, neighbourhood clamp, bilateral upsample | UE is the most aggressive amortizer of the three. UE's front-depth reprojection + distance-gated disocclusion are worth adopting (§4 item 1b). |
| Direct light | Octaves w/ squared-factor falloff (default **0 octaves**), x² shadow march or shadow map, no powder, no in-scatter prob. (usf:374-432, 1078-1148) | Profile-seeded depth-modulated MS 0.05-0.25 (p. 136), powder heritage | 3 octaves ×0.5 falloff, powder, in-scatter prob., cone march + 4-slice map | We are richest; the gap is Nubis' depth modulation (audit §2.4 #22). UE's per-octave *shadow-side* extinction reduction is the same mechanism in different clothes — implement once, as audit says. |
| Phase | Dual-lobe HG, **`mix`** blend (usf:334) | Dual-lobe, classic form is `max` | Dual-lobe, `mix` | UE = ours ≠ Nubis-classic. Audit §5.4's `max` experiment is now *low* priority — two references use `mix`. |
| Multi-scatter params | Artist floats: contribution/occlusion/eccentricity, squared per octave (usf:390-399, 424-428) | Fixed constants in shown code + depth modulation | Octaves + falloff + eccentricity authored per preset | All compatible; ours already artist-authored. |
| Ambient on clouds | Constant Distant-Sky-Light × bottom gradient, ms=0 only (usf:723-739, 945-954); optional marched ground bounce | `pow(1−profile, 0.5)` local + vertical summed-density column (pp. 141-144) | Both Nubis terms + physically-united dome/ground (audit §4: MATCH+) | **We are ahead of UE.** Keep ours; Sky Phase 4 will swap the dome source per teamlead Q4. |
| Shadows on the WORLD | **Yes**: front-depth+meanσ+maxOD map consumed by deferred/forward/fog/translucency/Lumen/reflections/**atmosphere** (§1.7) | Sun-space summed grid exists but deck shows only cloud-internal use (pp. 122-124) | Sun-space 4-slice map, consumed **only inside the cloud march** | **UE-only capability. New roadmap item (§4 item 3).** |
| Sky light shadowed by clouds (world) | SkyAO top-down map + hemisphere aperture filter (§1.8) | — | — | UE-only. Folded into §4 item 3 as phase 2. |
| Aerial perspective on clouds | AP volume sampled once at weighted mean depth; Mie/Ray split distances; late-fog (§1.9) | Deck's own AP is bespoke per title | Bespoke `AtmosphericPerspective` hack (CloudRaymarch.shader:445-465) | UE = teamlead's already-decided Sky Phase 3 plan. Adds two refinements: weighted-mean-depth application point and Mie/Ray start-distance art knobs. |
| Sun per-sample colouring | Optional per-sample transmittance-LUT tint (usf:923-930) | Time-of-day inherited globally (p. 182) | Global `AtmosphereEnv` sun tint (CLD-102) | UE-only refinement; needs Sky Phase 1 LUT. §4 item 6. |
| Weather/coverage authoring | Material's business (graph textures) | 512² NDF over 16 km, 31 m texels, painted/projected | 512² over 60 km, 117 m texels, procedural gen | UE agnostic; audit→8 stands on Nubis' authority. |
| Empty space | Conservative-density material pin (on), froxel start-distance texture (off, experimental) | SDF max-step (p. 163) | Two-tier miss counting | Three answers; ours ≈ UE's shipped path. No change needed (validated). |
| Early-out | All-channel T < 0.005 stop (usf:1442-46) | — (reference had the bug) | Monotone transmittance gate | = across the board. |
| IBL / reflections | Clouds traced into sky-capture cubemap at reduced budget (§1.9) | n/a | Clouds absent from `BakeProceduralSky` IBL | UE-only. §4 item 8. |
| Local lights in clouds | Off-by-default cinematic path | n/a | none | Non-goal (matches Epic's own default). |

---

## 3. What UE has that neither we nor the Nubis³ audit covered

Confirmations/refutations of the expected candidates, from source:

1. **Shadows cast on the ground/terrain — CONFIRMED, and broader than expected.** Not a
   light function: a first-class 3-channel analytic shadow map consumed by *eight*
   receiver systems including the atmosphere raymarch itself (§1.7). We have the
   sun-space map already but nothing outside the cloud march reads it. The
   front-depth + mean-extinction + max-OD encoding is the enabling design: it answers
   "transmittance at arbitrary 3D point" from a 2D texture, which is what a terrain
   pixel, a fog froxel, and an atmosphere sample all need. Also note the *three separate
   strength dials* per receiver class and the double snapping (world grid + pixel grid)
   that kills shimmer at 586 m texels.
2. **Per-sample atmosphere light colouring — CONFIRMED** (§1.9). Off by default; an
   opt-in look upgrade gated on a transmittance LUT. Depends on our Sky Phase 1.
3. **Empty-space-skipping structure — CONFIRMED to exist, REFUTED as a practice.**
   Experimental, default off; shipped skipping is the per-sample conservative-density
   pin. Our two-tier march needs no replacement.
4. **Distant cloud LODs / far raymarch mode — REFUTED.** No far mode, no dome, no LOD
   chain. UE simply bounds the problem: `TracingStartMaxDistance` 350 km,
   `TracingMaxDistance` 50 km *in-layer*, samples spread over ≤ 15 km, and the temporal
   pipeline treats > 5 km clouds as a stable reprojected layer. The audit's worry about
   our 150 km shell burden is answered by UE with *caps*, not with a second
   representation: bounding the finely-marched range (audit item 1c) is exactly what UE
   does, with numbers we can copy as defaults.
5. **Ray budget adaptivity — PARTIAL.** Count ∝ trace length (up to 15 km), min 2, wave
   clamp; shadow-map samples ×2 at horizon; ×scale in reflections. But *within* a ray:
   uniform steps, no growth, no importance seeding. Adaptivity in UE lives at the
   pixel/буфер level (which pixels get traced this frame), not the sample level.
6. **The material graph as artist surface — CONFIRMED** as the deepest architectural
   difference. What matters for us is not adopting UMaterial, but the *contract* it
   implies: the host passes normalized layer coordinates in and receives
   {extinction, albedo, emissive, AO, conservative-density}; every lighting knob
   (phase g/g2/blend, MS triplet) is data, not code. Our `CloudDensity.glslh` seam
   (`:38-46`) is already this contract in miniature — audit items 3/6/7 all plug into it.
7. **Uncovered bonus findings**: (a) `GrayScaleMaterial` static permutation runs the
   whole loop scalar — we march RGB extinction nowhere, we are already scalar,
   parity; (b) min/max-depth dual-layer tracing for geometry intersection (§1.4) —
   relevant when we get flying-through-clouds scenes; (c) late-fog during upsample;
   (d) `bHoldout`/alpha for compositing — non-goal; (e) two atmosphere lights everywhere
   — already a recorded sky non-goal, and UE itself admits only one can cast opaque
   shadows on clouds (usf:314-315).

---

## 4. Reconciled roadmap

Audit §3 items 1-8 merged with UE evidence into one ordered list. Order preserves the
audit's "each unlock makes the next visible" logic; UE-only items are slotted by
dependency and payoff. Costs: S < 1 day, M ≈ days, L ≈ week+.

**1. Step schedule + temporal amortization — MODIFIED per UE evidence.** (audit→1)
The audit prescribed Nubis' √-growth toward ≤ 30 m steps to 10 km. UE demonstrates the
complementary lever: pay for finer steps with *fewer traced pixels per frame*, not only
with cheaper steps. Revised plan:
   * (a) Keep the audit's fine tier (√ growth or lowered linear growth) — the deck is
     right that 100-200 m lobes need ~sub-30 m sampling to exist at all.
   * (b) Adopt UE's budget model for the *count*: distribute a fixed sample budget over
     the **in-layer trace length only**, clamped, with the finely-marched range capped
     (UE: 15 km distribution / 50 km in-layer max / 350 km entry max — sane defaults to
     start from). Beyond the cap, today's coarse schedule (this is audit 1c, now with
     UE's numbers).
   * (c) Strengthen the temporal side to carry the residual noise: sequence our
     checkerboard refresh in Bayer order, reproject on **cloud front depth** (we already
     write the guide depth, CloudRaymarch.shader:150), and add UE's ≥ 5 km
     "stable-layer" disocclusion bypass so distant decks stop being clamped into mush.
   * Effect: mid-distance silhouettes become resolvable (the p. 150 band); temporal
     shimmer bounded. Cost M-L total; risk carried by (c). No sky dependency.

**2. Erosion at full strength + second octave — KEEP AS-IS.** (audit→2) UE is silent
(density is the artist's material); Nubis' p. 81/118 evidence stands. Cost S-M, after 1.

**3. Cloud shadows on the WORLD — NEW (UE-only), high leverage, independent.**
Two phases:
   * (a) **Sun shadow**: re-encode (or augment) our sun-space `CloudShadowMap` with UE's
     analytic triple {front depth, mean extinction, max optical depth} and read it in the
     deferred lighting pass for the directional light (`lerp(1, T, strength)`), and in
     `LightShaftRenderer`'s occlusion input if applicable. Expose per-receiver strengths
     (on-surface / on-atmosphere-when-Phase-3-exists) on the light, UE defaults 1.0,
     master toggle default **off** like UE. Grounding effect: moving cloud shadows over
     terrain are the single strongest "the sky is real" cue in UE scenes. Cost M.
     Dependency: none (map exists; one encoding change + one consumer).
   * (b) **Sky-light AO**: top-down map + hemisphere-aperture filter dimming our IBL
     ambient on the ground under decks. Cost M. Dependency: none technically, but
     recalibration overlaps Sky Phase 4 — schedule with it.
   * Note: (a)'s encoding also *replaces* the ambient column projection seams (audit §4
     column-occlusion seams a/b/c) if we additionally trace a small vertical-sum variant
     — decide in §5 Q3.

**4. Depth-modulated multiple scattering (dark cores) — PROMOTED from audit §2.4 #22.**
Nubis p. 136 (0.05-0.25 effective extinction, deeper ⇒ more transparent) and UE's
per-octave shadow-extinction reduction are the same physics; the audit's analysis showed
this is why our backlit cores run dark. Implement Nubis' form (profile-seeded,
view-dependent) inside `CloudMultiScatter`/the shadow term; UE corroborates applying the
reduction to the *shadow/optical-depth side*. Effect: pp. 136/137 luminous interiors —
the largest remaining *lighting* gap. Cost S-M (formula + preset re-tune). No dependency;
do before/with 2 since erosion retune changes optical depths.

**5. Type-as-LUT + per-cell layer heights — KEEP AS-IS.** (audit→3) UE's material
architecture is the *generalisation* of this; our LUT plan is the C++-idiomatic subset.
Cost M.

**6. Detail-type from weather — KEEP AS-IS** (audit→4, cost S) and **weather-map
sharpening + density-scale full range — KEEP AS-IS** (audit→8, cost S; includes the §5.1
half-range bug).

**7. Per-sample atmospheric sun transmittance — NEW (UE-only).** After Sky Phase 1
(transmittance LUT exists): optional component flag, default off like UE; per-sample
`T_atm(sampleAltitude, sunDir)` from the LUT × outer-space illuminance replaces the
global sun tint inside the march (and the ground-bounce sun colour). Effect:
altitude-correct sunset grading across the deck. Cost S once the LUT exists.
Dependency: **Sky Phase 1**.

**8. AP-on-clouds via the AP volume — ALREADY DECIDED (Sky Phase 3), two UE refinements.**
When `ExecuteAtmosphericFog()` lands: sample the AP volume **once per pixel at the
transmittance-weighted mean cloud depth** (accumulate the weighted sum in the march as UE
does), and reserve component fields for Mie/Rayleigh start+fade distances (UE's four
floats) so scenes don't migrate twice. Effect: physically-unified haze; removes the
CloudRaymarch AP hack. Cost folded into Sky Phase 3.

**9. Second cloud layer — KEEP AS-IS.** (audit→5) UE has strictly one layer per scene —
we would *exceed* UE here; the deck's cirrus-over-cumulus target justifies it. Cost M.

**10. Clouds in the IBL bake — NEW (UE-only), small.** Render the cloud layer into
`BakeProceduralSky`'s panorama at a reduced budget (UE: ~80 samples, no jitter, no
temporal) so ground-level ambient and reflections contain the deck. Guard the
one-SceneRenderer-per-frame rule (bake path is offscreen — check against
[one-scenerenderer-per-frame] memory). Cost S-M. Best after 3b so ambient isn't
double-darkened. Dependency: none hard; recalibrate with Sky Phase 4.

**11. Local envelope entities — KEEP AS-IS.** (audit→6, cost L.) UE's equivalent is "the
artist adds SDF nodes to the graph"; our ECS CloudMass is the same capability in our
architecture.

**12. NVDF import path — KEEP AS-IS, last.** (audit→7, cost L-XL.) UE corroborates the
seam design: their ConservativeDensity/material split maps 1:1 onto our
`CloudDensity.glslh` seam's cheap/full pair, so the baked branch drops in without march
changes.

**Explicitly not adopted from UE** (record as non-goals): UMaterial-style runtime graph
for density (see §5 Q2), froxel empty-space skipping (experimental, off in UE; our
two-tier march is parity), local lights in clouds, second atmosphere light (matches sky
non-goal), holdout/alpha, min/max dual-layer depth tracing (revisit if a
fly-through-clouds scene class appears), `mix`→`max` phase change (UE agrees with our
`mix`; audit §5.4 experiment now low priority).

---

## 5. Open questions for the teamlead

> **DECIDED (teamlead, 2026-08-14).**
> **Q1** Hybrid, staged: keep Nubis adaptive fine steps (Wave 1, in flight, perf-gated); the UE
> amortization side (Bayer-sequenced refresh beyond the existing 1/2 checkerboard, front-depth
> reprojection, ≥5 km stable-layer bypass) is its own later item — on MoltenVK with fast editor
> cameras we prefer temporal stability over cheapness, so amortization deepens only as far as the
> shots stay clean. If Wave 1's gates force coarser steps than the deck wants, amortization is the
> recovery lever, not bigger steps.
> **Q2** "Authored data, fixed sampler" is the position through item 12 (Type-LUTs → NVDF import).
> No runtime-compiled density graph/Lua callback — revisit only if item 12 ships and proves
> insufficient. Recorded as a non-goal.
> **Q3** Two encodings for now: the UE-style analytic triple serves the NEW world-shadow consumers
> (item 3a); the in-cloud march and ambient column keep the 4-slice map — zero regression risk.
> Convergence on one encoding + vertical-sum buffer is a separate, measured experiment gated by the
> ~2.3:1 lit:shadow reference.
> **Q4** Confirmed: per-light/component flag, default OFF (as UE ships); presets/showcase scenes turn
> it on.
> **Q5** Cap only the FINE tier (~25-50 km); the far coarse tier keeps today's 150 km horizon
> behaviour — the horizon look is a calibrated feature of shipped scenes, not a bug to inherit UE's
> caps into.
> **Q6** Dedicated low-budget bake path on the bake's own cadence (UE-style ~80 samples, no jitter).
> A view-dependent shortcut would bake camera-relative error into ground ambient. The bake is a pass
> of the SAME renderer, so the one-SceneRenderer-per-frame rule is respected by construction.

1. **Step philosophy for item 1 — how far toward UE's amortization do we go?** The hybrid
   above keeps per-ray adaptive steps (Nubis) *and* adds Bayer-sequenced refresh +
   front-depth reprojection (UE). The purist alternatives: (a) full UE — uniform steps,
   1/16-pixel refresh, accept reliance on temporal stability (cheapest, most
   ghost-prone on our fast-moving editor cameras); (b) full Nubis — fine adaptive steps
   at higher trace res (most stable, most expensive). Which risk do we prefer on
   MoltenVK-class GPUs? The hybrid is my recommendation but it is the largest single
   engineering item on the list.
2. **Do we want any artist-programmable density surface?** UE's deepest advantage is that
   density is data (a graph), so type/erosion/coverage arguments dissolve into content.
   Our equivalents in ascending ambition: presets (have), Type-LUT textures (item 5),
   authored NVDF volumes (item 12), a Lua-scripted density callback compiled to GLSL
   (new, large). Is anything beyond item 12 on the table, or is "authored data, fixed
   sampler" our permanent position?
3. **One shadow encoding or two?** Adopting UE's {front depth, mean σ, max OD} triple for
   the world-shadow consumers (item 3a) leaves a choice for the in-cloud march and the
   ambient column: keep our 4-slice map for those (two encodings, no regression risk) or
   converge everything on the triple + a small vertical-sum buffer (one encoding, fixes
   audit §4's three ambient seams, but re-tunes the whole lighting calibration). The
   audit's ~2.3:1 lit:shadow reference measurement would gate the second path.
4. **Cloud shadows on world: default on or off?** UE ships `bCastCloudShadows = 0` —
   even Epic makes it opt-in per light. Our showcase scenes (Clouds_UEShowcase) would
   want it on; old scenes' lighting balance would shift. Proposal: component/light flag
   default off, presets/showcase turn it on — confirm.
5. **`TracingMaxDistance`-style caps change our horizon look.** Adopting UE's
   50 km-in-layer / 350 km-entry caps (item 1b) will visibly thin extremely distant decks
   that our current 150 km march renders (dimly). Accept the change (with the far
   fallback schedule keeping a horizon band), or keep our current far behaviour and cap
   only the *fine* tier?
6. **IBL bake with clouds (item 10) vs the preview-renderer constraint.** The bake must
   not spin up a second live SceneRenderer mid-frame ([one-scenerenderer-per-frame]).
   Options: reuse the main renderer's cloud pass output into the panorama (cheap,
   view-dependent error) or a dedicated low-budget bake path on the bake's own cadence.
   Which?
