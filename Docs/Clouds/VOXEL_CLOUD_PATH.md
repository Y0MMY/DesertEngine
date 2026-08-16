# The Voxel Density Path — design before code

**Status:** research only. No engine code or shader was modified to produce this.
**Scope:** what a second density model (baked voxel volumes, "hero clouds") has to be, how it plugs
into the seam we already have, what it costs in memory and milliseconds, how the data gets authored
without shipping anyone else's assets, and in what order it should land.

**Reference:** `Docs/Clouds/Nubis Cubed (Advances 2023).pdf`, cited by page. The per-page catalogue in
`Docs/Clouds/PdfAnalysis/pages-*.md` is the index; pages 82, 85, 86, 157, 158, 159, 160 were re-read
from the PDF itself for this document, the rest from the catalogue.

**Premise (already established, restated so the document stands alone):** the pp. 125/126/137/138/
142/143/150/166/167/168 look comes from *authored voxel volumes* — Houdini-simulated cloud shapes
baked to a grid (pp. 67–79) — not from a procedural noise field. We render a procedural noise field,
and so does UE (`UE_VOLUMETRIC_CLOUDS_RESEARCH.md` §1.1). The decision taken is to keep the
procedural path as the default and add the voxel path as a **second model, opt-in**, exactly as
`ECS::SkyModel` (`Desert/Desert/Source/Engine/ECS/SkyAtmosphereComponent.hpp:44`, field at `:180`)
splits the artistic gradient from the physical atmosphere.

---

## 1. What an NVDF actually is

### 1.1 The two NVDFs — they are different volumes with different jobs

The deck uses "NVDF" for two distinct assets (p. 158 puts them side by side):

| | **Modeling NVDFs** | **Field Data NVDF** |
|---|---|---|
| Resolution | 512 × 512 × 64 voxels | 512 × 512 × 64 voxels |
| Channels | Dimensional Profile, Detail Type, Density Scale | Signed Distance |
| Compression | BC6, 1 byte/texel | "Compression ?" on p. 158 — answered on p. 160: **BC1, 0.5 bytes/texel**, distance spread across R/G/B and reconstructed with `dot(rgb, float3(1.0, 0.03529415, 0.00069204))` |
| Size stated | **16.777 Mb** | not stated (≈ 8.4 MB at 0.5 B/texel) |
| Vertical world coverage | voxel size **8 m** (pp. 82, 86) ⇒ 64 × 8 m ≈ 512 m, matching the "500 m" band on p. 76 | **−256 m to 4096 m** (p. 157) ⇒ 4352 m / 64 ≈ **68 m per voxel vertically** |
| Horizontal coverage | 512 × 8 m = 4096 m — the 4 km × 4 km Burning Shores map (p. 76) | same footprint |

**This asymmetry is real and it is the design.** The modelling data is fine (8 m) because it carries
the shape; the SDF is coarse vertically (~68 m) and taller (up to 4 km, i.e. the flight ceiling)
because all it has to do is give a *conservative* safe step. The deck never says this in words — it
is an inference from two numbers on two slides — so treat "68 m vertical SDF voxels" as read-off,
not as a quoted figure.

### 1.2 Channel semantics — what each one means and what reads it

**Dimensional Profile** (p. 84, p. 98). Not density. It is a smooth, roughly signed-distance-like
"how deep inside the cloud am I" scalar: **1 in the core, ramping to 0 at and outside the surface**
(p. 84's cross-section is white in the interior with a soft grey falloff). p. 98 draws it as a
radial inside→outside gradient in every direction, including downward through the flat base. It is
the single most-read channel in the whole system:

* **the empty-space test** — p. 86: `if (modeling_data.mDimensionalProfile > 0.0) cloud_density = GetUprezzedVoxelCloudDensity(); else cloud_density = 0.0;`
* **the erosion base** — p. 118: `float uprezzed_density = ValueErosion(inDimensionalProfile, noise_composite);`
* **the wisp frequency selector** — p. 99: `wispy_noise = lerp(noise.r, noise.g, inDimensionalProfile)` (low-freq curly-alligator at the fringe → high-freq deep in)
* **the billow frequency selector** — p. 104: `billowy_type_gradient = pow(inDimensionalProfile, 0.25); billowy_noise = lerp(noise.b * 0.3, noise.a * 0.3, billowy_type_gradient);`
* **the ambient scattering term** — p. 141: `ambient_scattering = pow(1.0 - dimensional_profile, 0.5)`
* **the multiple-scattering seed** — p. 136: `ms_volume = dimensional_profile;`

Range: [0, 1] (all six uses above assume it; `pow(1-x, 0.5)` and `pow(x, 0.25)` both require it).

**Detail Type** (pp. 85, 108, 114). A per-voxel [0,1] selector between the two detail families:
0 = wispy, 1 = billowy (p. 108's gradient bar). p. 108: `noise_composite = lerp(wispy_noise, billowy_noise, detail_type);`
It selects the *same axis* twice — once for the normal composite (p. 108) and once for the HHF pair
(p. 114: "High High Freq Wisps" ↔ "High High Frequency Billows"). The authoring rule behind it is
physical (p. 92): *"Decreasing Density = Curly Layered Wisps / Increasing Density = Layered Billows"*
— evaporating regions get wisps, condensing regions get billows. In the p. 85 visualisation the red
(Detail Type) channel is concentrated in the flat wispy base, not the towers.

**Density Scale** (pp. 85, 118). A per-voxel [0,1] density multiplier — the green channel in p. 85,
concentrated in the base. Applied *linearly*, with the power only inside the sharpening exponent
(p. 118):

```
uprezzed_density *= inDensityScale;
uprezzed_density  = pow(uprezzed_density, lerp(0.3, 0.6, max(EPSILON, pow(saturate(inDensityScale), 4.0))));
```

**Signed Distance** (pp. 157, 158, 160, 163, 164). Negative inside the cloud, positive outside; the
lighting also reads it in world metres over the range **−128 … 0** (p. 136's
`ValueRemap(cloud_distance, -128.0, 0.0, 0.05, 0.25)`), which tells us the encoding is in metres and
that the useful interior depth is ~128 m.

### 1.3 Baked vs evaluated at runtime

| Baked into the NVDF | Evaluated in the shader every sample |
|---|---|
| Dimensional Profile (the silhouette and interior gradient) | The whole detail composite: wisps (p. 99), billows (p. 104), the HHF pair (p. 113) |
| Detail Type | `ValueErosion(profile, noise_composite)` (p. 118) |
| Density Scale | The density-scale sharpening `pow` (p. 118) |
| Signed Distance | The wind offset on the noise lookup (p. 95) and the distance mip (p. 96) |

The economy is stated outright on p. 119: *"Up-rez avoids a memory bottleneck. 0.5 meter precision.
Current Cost: 10ms @ 960x540px. Dense Grid Sampling: 30+ms."* **8 m voxels are up-rezzed to 0.5 m
effective precision in-shader, and marching an actually-0.5 m grid would be 3× the cost and
astronomically more memory.** p. 82 is the slide that names the alternative they rejected — the
caption toggles between "Dimensional Profile" and "Explicit Density"; storing explicit density is
what forces small voxels.

### 1.4 How the deck's sampler reads it — the part our march must satisfy

Reassembled from pp. 86 → 95 → 96 → 99 → 104 → 108 → 113 → 114 → 118, in order:

```
modeling_data = GetVoxelCloudModelingData(sample_pos);          // one fetch: profile, type, scale
if (modeling_data.mDimensionalProfile <= 0.0) return 0.0;       // p.86 — the empty test

pos -= float3(cCloudWindOffset.xy, 0.0) * voxel_cloud_animation_speed;              // p.95
mip   = log2(1.0 + abs(inRaymarchInfo.mDistance * cVoxelFineDetailMipMapDistanceScale));
noise = Cloud3DNoiseTextureC.SampleLOD(sampler, pos * 0.01, mip);                  // p.96

wispy   = lerp(noise.r, noise.g, profile);                                          // p.99
billow_g= pow(profile, 0.25);
billowy = lerp(noise.b * 0.3, noise.a * 0.3, billow_g);                             // p.104
composite = lerp(wispy, billowy, detail_type);                                      // p.108

hhf_wisps   = 1.0 - pow(abs(abs(noise.g*2-1)*2-1), 4.0);                            // p.113
hhf_billows =       pow(abs(abs(noise.a*2-1)*2-1), 2.0);
// HHF composite blended out over the first 150 m from the camera                   // p.114

d  = ValueErosion(profile, composite);                                              // p.118
d *= density_scale;
d  = pow(d, lerp(0.3, 0.6, max(EPSILON, pow(saturate(density_scale), 4.0))));
return d;
```

**Our `CloudDensitySample` is already this function, line for line**, with three substitutions:
`profile` comes from `gradient * coverage * baseShape` instead of a fetch
(`CloudDensityProcedural.glslh:180-188`), `detail_type` from a height ramp instead of a fetch
(`:252`), and `density_scale` from `weather.a` instead of a fetch (`:277`). Everything downstream —
the wisp lerp `:243`, the billow lerp `:244-245`, the composite `:253`, the HHF pair `:257-259`, the
distance fade `:260-261`, the erosion `:263`, the scale-and-sharpen `:278-280` — is the deck's code
with our parameter names. That is the single most important fact in this document: **the voxel branch
is a substitution of three inputs, not a new sampler.**

### 1.5 How the NVDF composes with the modelling-level fields that stay procedural

In the deck it doesn't: the voxel method *replaces* the coverage/type fields (p. 187's table lists
"Freeform Modeling: Yes" only for the voxel method, and the vertical-profile method is a separate
renderer). The NVDF's Dimensional Profile *is* the product our `CloudDensityCheapFrom` computes.

For us there are two readings, and the choice is a decision (see §7 Q1):

* **Replace.** The voxel branch answers for the whole sky. Faithful to the deck; means an authored
  volume must cover everything the camera can see, i.e. a 4 km × 4 km cloudscape, which is
  16 M voxels and (at our formats, §4) 64 MiB.
* **Union.** `density = max(procedural, voxel)` — the procedural deck fills the sky, N authored
  volumes sit inside it as hero clouds. This is what pp. 193/194 actually show in production: one
  bespoke Stormbird cumulus placed in an otherwise ordinary sky. It is also the only reading that
  makes "opt-in per entity" mean anything. It costs one extra evaluation per sample in the
  overlapping region, and it needs the voxel branch to return 0 cleanly outside every instance.

Recommendation: **union**, gated on instance count so a scene with no hero clouds pays a single
`if (count == 0)`.

---

## 2. Acceleration

### 2.1 What their SDF is

A **cloudscape-wide signed distance field**, baked from the finished "Frankencloudscape" (p. 157's
three-stage diagram: top-view cloudscape → top-view distance field → the 512×512×64 volume labelled
"Signed Distance", spanning −256 m to 4096 m). Negative inside, positive outside, in metres. It is
*not* per-cloud: one field for the whole authored scape.

### 2.2 How it is built

The deck does not show the builder. p. 157 shows only the arrow from the cloudscape to the SDF, and
pp. 154–156 cite the prior art the *marcher* uses (Dummer's cone step mapping 2006; Hart's sphere
tracing 1995; iquilezles.org; Claybook/Aaltonen GDC 2018). **How the distance transform itself is
computed is not in the deck** — do not invent it. The obvious method for a baked grid is a separable
exact Euclidean distance transform (Felzenszwalb–Huttenlocher, O(n) per axis) over the thresholded
Dimensional Profile, which is what we would write.

### 2.3 How the march uses it

p. 163, verbatim:

```
sdf_cloud_distance = GetVoxelCloudDistance(sample_pos);
adaptive_step_size = max( 1.0, max(sqrt(distance_from_camera), EPSILON) * 0.08);
jitter_offset      = distance_from_camera < 250.0 ? animated_hash : static_hash
step_size          = max(sdf_cloud_distance, adaptive_step_size) + jitter_offset;
sample_pos         = distance_from_camera + view_direction * step_size;
```

and p. 164, verbatim:

```
if (sdf_cloud_distance < 0.0)
{
    voxel_cloud_sample_data = GetVoxelCloudSampleData();
    IntegrateCloudSampleData(voxel_cloud_sample_data, pixel_data);
}
```

So: the SDF is a **lower bound on the step**, never a replacement for the adaptive schedule — the
step is `max(sdf, adaptive)`, so it can only ever make a step *longer*, never shorter than the
quality floor. And the SDF sign is the shading gate: negative ⇒ fetch the sample data and integrate.
Note what p. 164 also tells us: `GetVoxelCloudSampleData()` returns **four** things per sample —
density, direct light energy, ambient light energy, secondary light energy — because the lighting is
itself voxel-cached (pp. 123/124/144).

The compression choice (pp. 159/160) is stated as a precision trade with a named failure on each
side: *"Too low = extra steps. Too High = rendering artifacts."* Under-estimating distance is safe
and slow; over-estimating leaks through surfaces. **Any encoding we write must round toward zero.**

### 2.4 What it buys — the measured numbers

pp. 166/167/168, three shots, cost in milliseconds:

| Shot | Base | + Voxel-Based Lighting | + Adaptive & SDF Ray-March | Geometry (context) |
|---|---|---|---|---|
| p. 166 sunset valley | **10** | **6.1** | **2.2** | 7 |
| p. 167 sun-behind-tower | **12** | **8.2** | **4.0** | 5 |
| p. 168 above cloud tops | **10** | **8.0** | **4.0** | 4 |

p. 169 summarises: *"Uses Compressed SDF to avoid memory bottlenecks / Hybrid SDF, Adaptive and
Jittered samples / Cost: 2.2 to 4 Milliseconds / Performance scales."*

**Read the third column honestly.** It is "Adaptive **and** SDF" as one row — the deck never
separates the two. Our step schedule is *already* the adaptive half (`CloudStepLength`,
`CloudGeometry.glslh:230-237`, a √-type near schedule with a linear far schedule, retuned in the
High/Ultra tiers, `CloudQuality.hpp:86-145`). So the honest claim is: **the SDF's separable share of
that 2.8×/2.05×/2.0× is unknown from the deck.** Anyone quoting "SDF = 2.8× faster" is over-reading
the slide.

The p. 151 number is separable and is worth having on the record because it is the *other* half of
the win: *"Calculate and store summed density for some samples / Amortized Cost: 0.1 to 0.2 ms every
8 frames / Reduces render time by 40%"* — that is the voxel *lighting* cache (256×256×32, pp. 124/
144), which is a different feature from the density NVDF and out of scope here.

### 2.5 What our two-tier march already achieves, and whether an SDF replaces it

`CloudGeometry.glslh:239-306` documents the current scheme and is explicit that it exists *because*
we have no SDF:

> the reference skips empty space with a signed distance field … We have no SDF and cannot have one:
> our density field is procedural … What replaces it is two tiers plus the analytic shell bounds

The state machine: coarse tier steps `stride * coarseMultiplier` evaluating only `CloudDensityCheap`;
on a non-zero cheap sample it steps *back* one coarse stride and switches to fine; after
`emptyBeforeCoarse` consecutive empty fine samples it returns to coarse. Zero memory. At High
(`CloudQuality.hpp:97-104`) that is coarse 4 × 15 m near the camera, `EmptySamplesBeforeCoarse = 8`.

**An SDF does not replace it — it sits under it, and it changes exactly one line.** Concretely:

* The coarse tier's *fixed multiplier* becomes a *measured* distance. A cumulus fills roughly 20–30%
  of its bounding box; inside a hero cloud's box the coarse tier currently pays 1 cheap evaluation
  per `4 × stride` through empty box interior, and an SDF turns most of that into one step.
* The "step back one coarse stride on the first hit" correction (`CloudGeometry.glslh:289`) exists
  only because a fixed stride overshoots the leading edge. With a conservative SDF the step *cannot*
  overshoot, so the back-step becomes unnecessary in the SDF region.
* `emptyBeforeCoarse` hysteresis stays useful for the procedural branch and is harmless for the
  voxel one.

The change to `CloudMarchAdvance` is one extra argument (a conservative distance, defaulting to 0)
and `stride = max(stride, conservativeDistance)` — which is p. 163's `max(sdf, adaptive)` exactly.
That function is compiled as C++ by the CloudMath suite (`Desert/Tests/Engine/CloudMath/`), so the
new argument gets a monotonicity test for free.

**Recommendation:** keep the two-tier march. Add a third, optional seam function
(`CloudConservativeDistance`) that the procedural implementation returns `0.0` from — costing that
branch nothing and changing no picture — and that the voxel implementation answers from the
instance bounds (phase 1) and later from a baked SDF channel (phase 2).

---

## 3. How it plugs into OUR seam

### 3.1 The seam, exactly as written

`Editor/Resources/Shaders/Common/CloudDensity.glslh:6-7,25`:

```glsl
float CloudDensityCheap ( vec3 worldPos, float heightFraction );
vec2  CloudDensitySample( vec3 worldPos, float heightFraction, float distanceFromCamera );
float CloudPrecipitationAt( vec3 worldPos );
```

with the documented contract (`:13-36`): `worldPos` is **absolute world position in centimetres**;
`heightFraction` is 0 at the bottom of the layer and 1 at the top, measured along the planet radius;
`distanceFromCamera` is in world units; both density functions return [0,1]; `CloudDensitySample`
returns `x` = full eroded density and `y` = the **unerroded profile**, in one evaluation.

And `:38-46` already names this exact work as the reason the seam exists:

> baked "hero cloud" volumes (Nubis3 NVDFs) are out of v1 only because the shapes they need are not
> ours to ship. Adding them later must be a second implementation of these two functions and a second
> C++ function that binds the images they read — not an edit to the march loop, the light march, the
> compositor or the component.

### 3.2 What the voxel branch returns

**`CloudDensityCheap(worldPos, heightFraction)`** — one `sampler3D` fetch per instance whose box
contains `worldPos`, returning the `.r` (Dimensional Profile) channel, times `.b` (Density Scale) if
we want the cheap tier to track the fine tier's magnitude. This is p. 86's `mDimensionalProfile > 0`
test verbatim, and it is *cheaper* than the procedural cheap call, which today costs a 2D weather
fetch plus a 2D profile-map fetch plus a 3D shape fetch plus a profile-LUT fetch
(`CloudDensityProcedural.glslh:193, 124, 115, 186`). Outside every instance: `0.0`.

**`CloudDensitySample(worldPos, heightFraction, distanceFromCamera)`** — one `sampler3D` fetch giving
`(profile, detailType, densityScale, sdf)` in `.rgba`, then the *existing* detail composite from
`CloudDensityProcedural.glslh:233-289` unchanged: curl warp, detail fetch, wispy/billowy lerp, HHF
pair, distance fade, `CloudRemapRange` erosion, density-scale sharpen, distance softening, near fade.
Returns `vec2(density, profile)` — the same pair, with `profile` now being the *authored* one, which
is strictly better than the derived one for every consumer listed in §3.4.

**`CloudPrecipitationAt(worldPos)`** — keep reading the weather map (it is a 2D field about *where it
rains*, orthogonal to the cloud's shape), or return a per-instance constant. Either satisfies
`CloudDensity.glslh:31`'s "an implementation with no notion of precipitation returns 0 and loses
nothing else."

**New: `float CloudConservativeDistance(vec3 worldPos)`** — see §2.5. Procedural returns `0.0`.

### 3.3 What the march would need — ideally nothing, and nearly nothing is right

Audited call site by call site in `Editor/Resources/Shaders/Programs/Clouds/CloudRaymarch.shader`:

| Line | Call | Voxel branch OK? |
|---|---|---|
| `:306` | `CloudDensityCheap(worldPos, height) > 0.0` (coarse tier) | yes, unchanged |
| `:313` | `CloudDensitySample(worldPos, height, state.T)` (fine tier) | yes, unchanged |
| `:367` | `CloudDensityCheap(samplePos, sampleH)` × cone weight (sun march) | yes, unchanged |
| `:440` | `CloudPrecipitationAt(worldPos)` | yes, unchanged |
| `CloudShadowMap.shader:131` | `CloudDensityCheap(worldPos, height) * dt` | yes, unchanged |

Three things the march does that the voxel branch has to live with, and all three are fine:

1. **`CloudShellBounds`** (`CloudGeometry.glslh:145-176`) clips every ray to the spherical layer
   shell. A hero cloud must therefore live inside `[LayerBottomAltitude, +LayerThickness]`. That is
   not a limitation in practice — the deck's own field NVDF spans −256 m to 4096 m (p. 157) and our
   default layer is 1500 m … 5000 m (`VolumetricCloudsComponent.hpp:79,85`).
2. **`heightFraction`** is computed from the distance to the planet centre (`CloudLayerHeight`,
   `CloudGeometry.glslh:189-199`) and passed in. The voxel branch does not need it for density (the
   volume already knows its own shape) but must pass it through — the *lighting* uses it
   (`CloudAmbient`'s height term, `CloudInScatterProbability`'s height term).
3. **The 128-step budget.** Marching a small dense hero cloud with the same `MaxSteps` as a 150 km
   cloudscape is the failure mode already documented at `CloudRaymarch.shader:281-286` (a ray that
   exhausts `MaxSteps` mid-cloud draws a hard edge). The conservative distance is what pays for this.

So: **the march needs one optional argument on `CloudMarchAdvance` and nothing else.**

### 3.4 What the lighting needs — verified, it already speaks the deck's language

This is the strongest single finding of this review. Every place the deck feeds `dimensional_profile`
into the lighting, we already feed our `profile`:

| Deck | Formula | Ours |
|---|---|---|
| p. 141 | `ambient_scattering = pow(1.0 - dimensional_profile, 0.5)` | `CloudAmbientOcclusion(profile, columnAbove, …)` — `CloudRaymarch.shader:436`, defined `CloudGeometry.glslh:622` |
| p. 136 | `ms_volume = dimensional_profile;` then `exp(-sunSummed * Remap(sun_dot, …, ValueRemap(cloud_distance, -128, 0, 0.05, 0.25)))` | `CloudMultiScatterOpticalDepth(tauSun, profile, cosTheta)` — `CloudRaymarch.shader:380-381`, defined `CloudGeometry.glslh:479`, with `CloudProfileDepth(profile)` at `:458` and `CloudMultiScatterExtinction(profile, cosTheta)` at `:465` |
| published Nubis in-scatter | low-LOD density | `CloudInScatterProbability(height, profile, tauSun)` — `CloudRaymarch.shader:398`, and the comment at `:390-397` records that passing the *full* density here was the bug that crushed the silver-lining rim |
| p. 144 | `* exp(-summed_ambient_density)` from a 256×256×32 buffer | `CloudAmbientColumnVertical(...)` from our sun-space shadow map — `CloudRaymarch.shader:419-425` |

**Nothing in the lighting has to change.** Better: today `profile` is a *derived* quantity
(`gradient * coverage * baseShape`, `CloudDensityProcedural.glslh:180-188`) that only approximates
"how deep inside the cloud am I", because it is the product of a vertical envelope and a plan-view
coverage. From an NVDF it is the *actual* interior-depth field (p. 84's cross-section), which is what
those four formulas were derived against. The ambient occlusion and the multi-scatter depth term will
be *more* correct on the voxel path than on the procedural one, for free.

The one thing the deck has that we do not is p. 164's four-channel sample: they also fetch cached
direct/ambient/secondary light energy per sample from the voxel lighting grid. That is a separate
feature (pp. 121–151) and explicitly out of scope here.

### 3.5 Where the model switch lives

Follow `SkyModel` exactly. It is: an enum in the component header, a reflected field, a payload flag,
and a C++ branch in the renderer.

```cpp
// Desert/Desert/Source/Engine/ECS/VolumetricCloudsComponent.hpp, beside CloudQuality/CloudPreset
enum class CloudDensityModel : uint8_t
{
    Procedural, // the weather-map / envelope / erosion field — the default and the fallback
    VoxelHero   // authored volumes placed as entities, union'd over the procedural field
};
```

with, in `VolumetricCloudData`:

```cpp
PROPERTY( DisplayName( "Density Model" ), Category( "Cloud Layer" ), Summary,
          Tooltip( "Procedural is the infinite weather-driven cloudscape. Voxel Hero additionally "
                   "renders authored cloud volumes placed in the level." ) )
CloudDensityModel DensityModel = CloudDensityModel::Procedural;
```

Precedent for every piece: the enum shape is `SkyAtmosphereComponent.hpp:44`, the default-to-the-
cheap-model choice is `:180`, the reflection metadata with `EnumValues` is generated the same way
(`Reflection.gen.cpp:137` for `SkyModel`), and the renderer-side branch is
`SkyboxRenderer.cpp:413,652` (`m_Sky.Model == ECS::SkyModel::PhysicalAtmosphere`).

**How the shader gets the other implementation.** `CloudDensity.glslh:44-46` states the intent: *"A
shader includes THAT, not this file."* The engine supports `ShaderDefines`
(`Desert/Desert/Source/Engine/Graphic/Shader.hpp:13,40`), and `ComputeShaderCacheKey` already keys on
source and includes (`Desert/Tests/Engine/ShaderCacheKey/`). So:

```glsl
// CloudRaymarch.shader:32 becomes
#ifdef CLOUD_DENSITY_VOXEL
    #include <Common/CloudDensityVoxel.glslh>
#else
    #include <Common/CloudDensityProcedural.glslh>
#endif
```

and `VolumetricCloudRenderer` builds `m_RaymarchPipeline` (and `m_ShadowPipeline`) from the shader
with or without that define, rebuilding when `m_Data.DensityModel` changes — the same latch-and-
rebuild pattern it already uses for `m_ScatterScale` (`VolumetricCloudRenderer.hpp:180-183`).

For the **union** reading (§1.5) the voxel header includes the procedural one and composes:

```glsl
// Common/CloudDensityVoxel.glslh (sketch)
#include <Common/CloudDensityProcedural.glslh>   // brings in the shared detail composite

float CloudDensityCheap(vec3 p, float h)                 // shadows the procedural one? NO —
```

…which GLSL will not allow (redefinition). The clean form is to rename the procedural functions to
`CloudDensityCheapProcedural` / `CloudDensitySampleProcedural` inside a small
`Common/CloudDensityCompose.glslh` that defines the seam names once:

```glsl
float CloudDensityCheap(vec3 p, float h)
{
    float d = CloudDensityCheapProcedural(p, h);
#ifdef CLOUD_DENSITY_VOXEL
    d = max(d, CloudDensityCheapVoxel(p, h));
#endif
    return d;
}
```

That is a rename of two functions and one new 30-line header. It keeps the seam's promise (the march
never learns which model answered) and it makes "Procedural only" a *compile-time* zero-cost path.

---

## 4. Placement and scale

### 4.1 How a level holds N hero clouds

The deck's own production answer (pp. 181, 183, 185, 193): **instances with transforms.** p. 181 is a
Houdini layout of the actual game terrain with dozens of discrete cloud puffs placed over it. p. 183
is the Decima sequencer showing a `SetVoxelCloudTransformEventResource` with `Start Frame 0`,
`Stop Frame 360` and a `Translation` triple (~[3134, −2847, 544]) — **cloud instances are
transform-animated like props**. p. 185: *"Frankenclouding Works … Cinematics Memory benefits from
Re-use … Bespoke cloudscapes for Boss fights/etc are easy."* Re-use of one asset across shots is
called out as the memory win, which only makes sense with instancing.

Our shape:

```cpp
// A new component, not a field on VolumetricCloudData — one per placed cloud.
struct CloudVolumeData
{
    AssetHandle Volume;          // the baked NVDF asset
    float       DensityScale;    // per-instance multiplier on the baked scale channel
    float       DetailTypeBias;  // per-instance nudge on the baked type channel
    bool        CastsCloudShadow;
};
```

The renderer gathers up to `kMaxCloudVolumes` instances into a **second SSBO** — the pattern is
already there: `m_ParamsBuffer = ShaderResources::StorageBuffer::Create("CloudParams", …)`
(`VolumetricCloudRenderer.cpp:80-81`), bound with `SetStorageBuffer(kCloudParamsBinding, …)` at
`:565`. One record per instance:

```cpp
struct CloudVolumeInstance   // 96 bytes, std430-clean
{
    glm::mat4 WorldToLocal;   // 64 — includes the translation, the yaw and the inverse scale
    glm::vec4 BoundsHalf;     // 16 — local-space half extents; .w = the SDF encoding range in cm
    glm::vec4 AtlasTile;      // 16 — .xyz = tile origin in atlas UVW, .w = packed tile size index
};
static_assert( sizeof( CloudVolumeInstance ) == 96 );
```

**Not a sparse grid, not a streamed tile set, for v1.** A sparse grid is what you build when the
volumes cover the whole sky (the deck's case: one 4 km × 4 km field). Streaming is what you build
when the level is bigger than memory. Neither is true for "N ≤ 16 hero clouds in a scene", and both
are large pieces of machinery that would be built before anyone has seen a single voxel cloud render.

### 4.2 One volume, N volumes: **an atlas, not an array of samplers**

Sampling N distinct volumes from one shader means either `uniform sampler3D u_Volumes[N]` with a
dynamic index (needs `shaderSampledImageArrayDynamicIndexing`, and on MoltenVK lands on Metal
argument buffers — supported, but a portability risk we do not need to take) or **one atlas volume**
with tiles laid out in a grid and the tile origin carried in the instance record.

The atlas wins twice here, because our 3D sampler is created **LINEAR / REPEAT** with no way to ask
for clamp (`VulkanImage.cpp:66-68`, and `:78-87` *asserts* that a volume sampler is LINEAR/REPEAT
regardless of the global texture filter — see §7 Q4). A hero cloud must not wrap, so the shader has
to clamp its UVW arithmetic manually anyway; once it does, an atlas tile is the same arithmetic with
a different origin. Reserve a **1-voxel guard band of zeros** around each tile so trilinear taps at
the tile edge cannot bleed into the neighbour.

### 4.3 Memory arithmetic — the real numbers

**Our floor is 4 bytes/texel.** `Core::Formats::ImageFormat` (`ImageFormat.hpp:19-44`) has exactly
`RGBA8F, RGBA16F, RGBA32F, BGRA8F, DEPTH24STENCIL8, DEPTH32F`. There is no R8, no RG8, and no block
format. Worse, **Apple GPUs do not support BC at all** (they expose ASTC/ETC/PVRTC), and 3D ASTC is
not exposed through Metal — so the deck's "BC6, 1 byte/texel" and "BC1, 0.5 bytes/texel" are simply
**unavailable to us on Apple Silicon**, not merely unimplemented. RGBA8 at 4 B/texel is the floor,
and it buys us four channels: profile, detail type, density scale, **and the SDF for free in `.a`**
(exactly the four grids the reference project's VDBs carry, `RESEARCH_REFERENCE.md` §E.3).

Baseline for comparison: our existing noise set is **16.06 MiB** — two 128³ RGBA8 volumes at 8 MiB
each plus a 128² curl map at 64 KiB (`CloudNoiseRules.hpp:23-27,40-46`).

| Volume | Texels | RGBA8 size | At 8 m voxels covers |
|---|---|---|---|
| 64 × 64 × 32 | 131 072 | **0.50 MiB** | 512 × 512 × 256 m — one small cumulus |
| 96 × 96 × 48 | 442 368 | **1.69 MiB** | 768 × 768 × 384 m |
| **128 × 128 × 64** | 1 048 576 | **4.00 MiB** | **1024 × 1024 × 512 m — one hero cumulus, deck-parity voxel size** |
| 128 × 128 × 128 | 2 097 152 | 8.00 MiB | 1024 × 1024 × 1024 m — a tower |
| 256 × 256 × 64 | 4 194 304 | 16.00 MiB | 2048 × 2048 × 512 m — a cluster |
| **256 × 256 × 256** | 16 777 216 | **64.00 MiB** | the figure in the brief — and also exactly the deck's 512×512×64 texel count |
| 512 × 512 × 64 (deck's NVDF) | 16 777 216 | **64.00 MiB** uncompressed (16.777 MB as BC6) | 4096 × 4096 × 512 m — a whole cloudscape |

Atlas budgets (the number that matters):

| Atlas | Tiles | Texels | RGBA8 |
|---|---|---|---|
| 4 × 2 tiles of 128×128×64 → 512 × 256 × 64 | **8 hero clouds** | 8 388 608 | **32.0 MiB** |
| 4 × 4 tiles of 96×96×48 → 384 × 384 × 48 | **16 hero clouds** | 7 077 888 | **27.0 MiB** |
| 2 × 2 tiles of 128×128×64 → 256 × 256 × 64 | **4 hero clouds** | 4 194 304 | **16.0 MiB** |

Plus a far-LOD atlas at 1/4 linear resolution (§4.4): 4 × 2 tiles of 32×32×16 = 128 × 64 × 16 =
131 072 texels = **0.50 MiB**. Negligible; take it.

**Is 32 MiB acceptable in a frame budget on Apple Silicon?** Capacity: yes, trivially — it is unified
memory, an M-series machine has 8–128 GB of it, and the cloud renderer already logs a scaled-imagery
total in the same units (`VolumetricCloudRenderer.cpp:337-352`). 32 MiB is ~2× our noise set and less
than a single 4K PBR material set. **Bandwidth is the constraint, not capacity.** The added cost is
*one* `sampler3D` fetch on the cheap tier and *one* on the fine tier, against the 18 fetches a shaded
sample already costs (`CloudQuality.hpp:104-106` records that the cone march alone is 12 of them). On
Apple Silicon's tile memory a 32 MiB volume will not be resident in cache; the saving grace is
coherence — neighbouring rays hitting the same hero cloud read the same bricks. Expect the union
reading to cost ~10–20% on scenes that contain hero clouds and ~0% on scenes that do not (the
`count == 0` early-out). **Measure it; do not promise it.**

The recommended v1 budget: **8 instances, 4 MiB each, 32 MiB + 0.5 MiB LOD atlas**, one MiB log line
at allocation in the `CloudNoiseVolumes.cpp:257` idiom:

```
[CloudVolumes] Atlas 512x256x64 RGBA8 (32.00 MiB) + far LOD 128x64x16 (0.50 MiB) for 8 tiles of
128x128x64 at 8.00 m/voxel; 3 tiles in use, 5 free.
```

### 4.4 The LOD / distance story — what a voxel cloud does at 30 km

Work the numbers. A hero cloud 1024 m across, seen at 30 km, subtends
`1024 / 30000 ≈ 0.034 rad ≈ 1.96°`. At a 60° horizontal FOV rendered into a 960 px half-res scatter
target, that is `960 × 1.96 / 60 ≈ 31 px`. A 128-texel-wide volume sampled across 31 px is a **4:1
minification**, and:

* **We have no 3D mips.** `Core::Formats::Image3DSpecification` (`ImageFormat.hpp:249-265`) has no
  `Mips` field, with a comment explaining why: *"The engine has no 3D mip generator … Volumes are
  therefore single-level, and shaders sample them without an explicit LOD."*
* So the deck's own LOD answer — `mipmap_level = log2(1.0 + abs(distance * scale))` with an explicit
  `SampleLOD` (p. 96) — **is not available to us**, and this is a pre-existing gap: the audit already
  records that the reference project computed a mip level and then sampled without it
  (`RESEARCH_REFERENCE.md` J.3 #11).

What we have instead, and it is not nothing: `CloudDensitySample` already applies a
**distance-softening** term at `CloudDensityProcedural.glslh:287-289` (`SofteningStartDistance` 8 km
→ `SofteningEndDistance` 45 km, `VolumetricCloudsComponent.hpp:344,349`) and a **high-frequency
fade** at `:260-261` (`HighFreqFadeStart` 2.5 km → `HighFreqFadeEnd` 9 km). Those attenuate the
*detail* at distance, which is most of the aliasing, but they do nothing about aliasing in the baked
profile itself.

**The recommendation is a bake-time far atlas, not an engine mip generator.** We own the data; we can
box-filter it offline into a second atlas at 1/4 linear resolution (32×32×16 per tile) for 0.5 MiB
total, and select in-shader:

```glsl
float w   = CloudRemapRange(distanceFromCamera, u_VolumeLodStart, u_VolumeLodEnd, 0.0, 1.0);
vec4  nvdf = mix(texture(u_VolumeAtlas, uvwNear), texture(u_VolumeAtlasFar, uvwFar), w);
```

Two fetches in the crossfade band only; one outside it. This is 20 lines and no new engine feature.
Building a real 3D mip generator (a blit chain in `VulkanImage3D`, matching `MipMap2DGenerator`) is
the *right* long-term answer and should be its own ticket — it would also un-block the deck's p. 96
noise mip, which is a separate quality win on the procedural path.

Beyond ~25–30 km the honest answer is **stop marching the volume at all**: past that distance a hero
cloud is a 30 px smudge and the procedural deck behind it carries the frame. Add a per-instance
`FadeOutDistance` that ramps the instance's contribution to zero and removes it from the cheap tier's
instance loop. The deck does the corresponding thing at the near end (p. 171/174: < 200 m at
480×270, > 200 m at 960×540) — different axis, same principle of not spending pixels where the eye
cannot use them.

---

## 5. Authoring pipeline

Ranked. The ranking is by *value per unit of work under our constraints*, not by fidelity to the
deck.

### (b) — RANKED FIRST: analytic SDF primitives → bake, growing into the CubeGrid/modelling path

**What it is.** A cloud is authored as a small set of signed-distance primitives — ellipsoids,
capsules, rounded boxes — combined with a smooth minimum, then converted to a Dimensional Profile
and baked. This is `NUBIS3_FULL_AUDIT.md` §3 item 6 ("Local envelope entities") except *baked
offline into a volume* instead of evaluated per-sample, which removes its cost ceiling entirely: a
per-sample envelope evaluation caps you at ~8 primitives, a baked one has no cap.

**Why it is first.** The deck itself says the NVDF does not need to look like a cloud. p. 81 is the
proof: the left render (data fields + profiles, no noise) is *"smooth, blobby 'melted plastic'
clouds"*, and the right render (with the noise multiplied in) is a fully detailed cumulus field —
same shapes. p. 177 shows the same thing in production: *"a soft, blurry white/gray mass … the big
dimensional forms are blocked in before high-frequency detail/erosion is applied."* **All the cloud
character comes from the erosion we already ship.** The authored volume only has to supply a
silhouette and a smooth interior gradient — and a smooth-min union of three ellipsoids supplies both,
analytically, exactly, with no distance transform needed (the SDF *is* the primitive evaluation).

The conversion is two lines:
```
profile = saturate(-sdf / falloff);                 // 1 in the core, 0 at the surface — p.84
sdfChannel = quantise_toward_zero(sdf, range);      // the .a channel, conservative
```

**What we must build.** A `CloudVolumeBaker` in `Engine/Graphic/Clouds/`: a struct of primitives, a
CPU (or compute) evaluation into a `std::vector<unsigned char>`, and an upload through
`Image3D::Create` with `spec.Data` populated — a path that already exists and works
(`VulkanImage.cpp:730-765` stages the volume through a mapped buffer and one `VkBufferImageCopy`;
`Image3DSpecification::Data` is the `ImagePixelData` variant, `ImageFormat.hpp:263`).
An editor panel with a primitive list and a live preview. Serialisation of the primitive list as the
*source* asset, with the volume as its bake product.

**Artist workflow.** Place a `CloudVolume` entity; add three ellipsoids; drag them into a leaning
tower; set falloff and density scale; hit Bake; see it in the viewport. p. 137's "canyon between two
towering cumulus" is two instances. p. 193's Stormbird cloud — *"wedge/loaf-shaped, tilted
diagonally, with a cauliflower top edge, softer diffuse underside, and one distinct round lobe
protruding on the right"* — is a rounded box, a squashed ellipsoid and one sphere.

**Licence.** None. Our code, our data.

**Time.** Baker + primitives + panel + serialisation: **~1 week.** This is the phase-1 candidate.

**Growing into CubeGrid.** The editor already has a real voxel modeller: `CubeGridTool`
(`Editor/Source/Editor/Panels/ViewportPanel/Tools/CubeGridTool.hpp:38` `struct Cell` with 8 vertical
corner offsets, `:83` `CellMap m_Cells` — a sparse hash of base-resolution lattice indices, with
committed layers, greedy meshing and Corner Mode ramps all done per `Docs/CUBEGRID_TODO.md`). A
blockout sculpted there is *already* a voxel occupancy set. Turning it into an NVDF is: rasterise the
cells into a dense grid → separable exact Euclidean distance transform (Felzenszwalb–Huttenlocher,
O(n) per axis) → smooth the SDF → the same two lines above. That gives p. 175's *"'In Progress....'
and a ':-)' written in the sky as sculpted white voxel clouds"* — literally freeform modelling, the
one row where p. 187's comparison table gives the voxel method a unique Yes.
**Time for the CubeGrid → NVDF bridge: ~3–4 days on top of the baker**, because the EDT is the only
genuinely new algorithm and the tool, the sparse structure and the panel already exist.

### (c) — RANKED SECOND: offline high-resolution run of our existing procedural generator

**What it is.** Run `CloudDensityCheapFrom`'s composition (weather × envelope × base shape) on the
CPU or in a compute pass over a chosen world-space box, at a chosen voxel size, and write the result
to a volume.

**Serious assessment, both halves.**

*What it genuinely buys.* It is the **cheapest possible way to stand the runtime path up end to end**
— an hour of work produces real NVDF data with the right channel layout, so the atlas, the instance
SSBO, the seam implementation, the sampler, the guard bands, the LOD crossfade and the MiB log line
can all be built and debugged against real bytes before anyone writes a modelling tool. As
*plumbing*, it is the correct first move and I recommend it as phase 0. It also gives artists a
"freeze this bit of my sky and edit it" button, which is a genuinely nice workflow.

*Can it produce shapes the runtime procedural path cannot?* **On its own, essentially no — and this
must be said plainly.** The field is `coverage(x,z) × envelope(height) × shapeVolume(x,y,z)`
(`CloudDensityProcedural.glslh:180-188`). Baking it produces the same field, frozen. The `shapeVolume`
term does admit overhangs in principle, but it enters as a mild modulation gated by
`ShapeErosionStrength` (`:188`, default 0.65, `VolumetricCloudsComponent.hpp:184`) — it perturbs the
envelope, it does not override it. Bake it and you get the same picture, minus the animation.

*Where it becomes genuinely more powerful.* The bake is a **carrier for a generator the runtime could
never afford**, and that is the real argument for it:
* many more erosion octaves than 4;
* an actual 3D curl **advection** — iteratively displacing the field along a divergence-free velocity
  field for K steps, which is a real fluid-ish operation and is precisely what our runtime curl warp
  approximates in one tap (`:233-236`);
* morphological operations — dilate/erode with a structuring element, "grow toward the sun";
* a genuine reaction-diffusion or shallow-water-convection step;
* accumulating N seconds of simulated evolution and freezing the frame you like.

Any one of those *does* produce shapes the runtime cannot. But note what has happened: the value is
in the offline generator, not in the baking. **So: take (c) as phase 0 plumbing; do not represent it
as an authoring answer.**

**What we must build.** A `BakeProceduralRegion(box, voxelSize)` function reusing the weather/profile
maps the renderer already bakes (`m_WeatherMap`, `m_ProfileMap`, `VolumetricCloudRenderer.hpp:156-159`).
**Licence:** none. **Time: ~1–2 days** for the plumbing version.

### (d) — RANKED THIRD: importing external volumes

**Do not start with OpenVDB.** OpenVDB is Apache-2.0 (safe to link and ship) but it is a heavy
build-time dependency (TBB, Blosc, zlib), and `RESEARCH_REFERENCE.md` §E.2 already documents what
happened to the reference project: `find_package(OpenVDB REQUIRED)`, unconditional headers, and the
actual runtime path loading **TGA slice sequences** because the VDB path was commented out. They
built the dependency and then didn't use it.

**Do this instead:** define a dead-simple interchange — a `.dvol` header (magic, dimensions, voxel
size in cm, channel semantics, bounds) followed by tightly packed RGBA8 — and an importer that also
accepts a **folder of numbered PNG slices**, which every DCC on earth can export. That is a
half-day's work, it has no dependencies, and it makes Houdini, Blender, EmberGen, Embergen-style
tools and hand-painted stacks all viable sources without the engine knowing about any of them. Add
NanoVDB (header-only, part of OpenVDB, Apache-2.0) as an *editor-side, optional* reader later if
someone actually needs `.vdb`.

**Time:** 0.5 day for `.dvol` + slice folders; ~2 days for optional NanoVDB behind a build flag.

### (a) — RANKED LAST for the *pipeline*, though fine for an individual artist: Houdini

**What it is.** The deck's own path: fluid simulation (Aero solver) in Houdini, edited and composited
with Guerrilla's internal "Atlas" toolset, baked to the voxel grid (pp. 68–72, 79).

**The honest assessment.**

* **Atlas is not available.** It is Guerrilla-internal (pp. 69, 181). What is available is Houdini's
  own Pyro/Aero solvers, which is the sim but not the compositing/cutout/erosion toolkit p. 69 lists.
* **The licence is the blocker, and it is specific.** *Houdini Apprentice* is free but its EULA
  restricts the **use of files created with it** to non-commercial purposes — a `.vdb` exported from
  Apprentice cannot be shipped in a commercial product. *Houdini Indie* (~$269/yr, revenue-capped) or
  Houdini FX lift that. So: fine for a portfolio engine today, a licensing decision the day this ships.
  This is a *different* question from `RESEARCH_REFERENCE.md` §K (which is about not shipping the
  reference project's code and Guerrilla's data), and it does not conflict with it — authoring our
  own data in a tool we are licensed for is exactly what §K permits.
* **The skill and time cost is real.** A Houdini cloud sim is minutes to hours per cloud, and the
  person driving it needs to know Houdini. That is a fine thing for a specialist to do occasionally;
  it is a bad thing for the pipeline to *depend* on.

**What we must build:** nothing, if (d)'s `.dvol`/slice importer exists. That is the whole argument
for doing (d) before (a): Houdini becomes *a* source rather than *the* source.

**Time:** 0 engine days; artist-side per cloud, hours.

### Summary table

| Option | Engine work | Artist workflow | Licence | Time | Verdict |
|---|---|---|---|---|---|
| (b) analytic SDF primitives → bake | baker, panel, serialisation | place & drag ellipsoids, Bake | none | ~1 wk | **first — this is the authoring answer** |
| (b′) CubeGrid → EDT → bake | + a distance transform | sculpt a blockout, Bake | none | +3–4 d | freeform modelling, p. 187's unique row |
| (c) bake our procedural field | region baker | "freeze this sky" | none | 1–2 d | **phase 0 plumbing**; not an authoring answer alone |
| (d) `.dvol` + PNG-slice importer | file format + importer | export from any DCC | none | 0.5 d | do before (a); makes (a) optional |
| (d′) NanoVDB reader | optional editor dependency | `.vdb` drag-drop | Apache-2.0 | ~2 d | only if someone needs it |
| (a) Houdini sim | none, given (d) | Houdini sim + export | **Apprentice = non-commercial only** | 0 engine d | an artist's option, never the dependency |

---

## 6. Phasing

Written in our idioms: payload/`.glslh` mirror with `static_assert`s, `.glslh` maths compiled as C++
by a test suite, lazy latched allocation, one MiB log line.

### Phase 0 — the path exists, nothing looks different (≈ 3–4 days)

* `Common/CloudDensityVoxel.glslh` implementing the three seam functions plus
  `CloudConservativeDistance`; `Common/CloudDensityCompose.glslh` doing the `max()` union; the
  procedural functions renamed to `*Procedural`. **No march edits.**
* `CloudDensityModel` enum + reflected field on `VolumetricCloudData`; the `CLOUD_DENSITY_VOXEL`
  define on the raymarch and shadow pipelines; rebuild-on-change latched like `m_ScatterScale`.
* `Engine/Graphic/Clouds/CloudVolumeAtlas.{hpp,cpp}` in the exact shape of
  `CloudNoiseVolumes.{hpp,cpp}` — a keyed, refcounted, **lazily latched** owner with `Acquire`/
  `Release`/`Find`, failure latched per key so a missing bake is not retried at 60 Hz
  (`CloudNoiseVolumes.hpp:40-47`), and one MiB log line on generation (`CloudNoiseVolumes.cpp:257`).
* `CloudVolumeInstance` SSBO with `static_assert(sizeof(...) == 96)`, and the payload additions —
  note `sizeof(CloudGpuPayload) == 512` is asserted today (`CloudPayload.hpp:188`), so a new
  `VoxelInstanceCount` / `VoxelLodStart` / `VoxelLodEnd` run moves that assert to 528→544 and adds
  the matching `offsetof` lines. Bindings 14/15 are free (0–13 are taken, `CloudPayload.hpp:206-225`).
* Data from (c): bake a region of the current procedural sky into a tile.
* **Tests, in `Desert/Tests/Engine/CloudMath/`:** the atlas UVW-with-guard-band arithmetic, the
  world→local→tile transform, the SDF quantisation being *conservative* (a property test:
  `decode(encode(d)) <= d` for all `d`), and `CloudMarchAdvance` monotonicity with the new argument —
  all by compiling the new `.glslh` as C++ through the `CloudGeometryReference.hpp` mechanism the
  suite already uses.

**Visible change: none.** That is the point.

### Phase 1 — the first visibly better hero cloud (≈ 1 week)

The analytic SDF primitive baker of §5(b), a `CloudVolumeComponent`, and an editor panel.

**What it produces.** A specific, art-directed cloud in a specific place: a leaning tower built from
two overlapping ellipsoids, with our existing erosion carving billows and wisps into it. This is the
pp. 137/145/193 class of image — *"a specific sculpted cloud"* — and it is unreachable from any
combination of our current sliders, because the procedural field is a product of a plan-view coverage
and a vertical envelope and cannot make an overhang, a waist, or a tilted axis.

**What it does NOT yet do:**
* no SDF-driven empty-space skipping — the coarse tier tests instance AABBs analytically, which
  handles the space *between* clouds but not the empty space *inside* each box (a cumulus fills
  ~20–30% of its box);
* no LOD atlas — distant hero clouds rely on the existing distance-softening and will alias;
* no fluid simulation, no CubeGrid sculpting — the vocabulary is ellipsoids, capsules and rounded
  boxes with a smooth minimum;
* no animation of the volume itself (the deck is honest about this too: p. 187 gives the voxel method
  "Pseudomotion Only" for evolution). Instance *transforms* can animate (p. 183); the shape cannot.
* no cloud-shadow-map contribution from hero clouds unless `CloudShadowMap.shader` gets the same
  define — which it does for free if both pipelines take it, so this is really "verify it".

### Phase 2 — it gets fast and it holds up at distance (≈ 4–5 days)

* SDF in the `.a` channel at bake time; `CloudConservativeDistance` reads it;
  `CloudMarchAdvance(… , conservativeDistance)` with `stride = max(stride, conservative)` — p. 163's
  `max(sdf, adaptive)`. Property test: the returned distance never exceeds the true distance.
* The far-LOD atlas (0.5 MiB) and the crossfade; per-instance `FadeOutDistance`.
* Instance count to 8 with the 32 MiB atlas; the MiB log line reports tiles used vs free.
* Measure. The deck's own numbers (pp. 166–168) are for adaptive **and** SDF together and our
  schedule is already adaptive, so budget for a smaller win than 2× and be pleased if it is larger.

### Phase 3 — freeform modelling (≈ 3–4 days)

CubeGrid blockout → EDT → smoothed SDF → NVDF. p. 175's sky-writing, p. 187's unique "Freeform
Modeling: Yes".

### Phase 4 — the outside world (≈ 0.5–2 days)

`.dvol` + PNG-slice importer; NanoVDB behind a build flag only if asked for.

---

## 7. Open questions for the teamlead

> **DECIDED (teamlead, 2026-08-16).** The goal set by the owner is a beautiful, realistic and VARIED
> sky — that is the tie-breaker in every answer below.
>
> **Q1 — UNION.** A voxel hero cloud sits inside the procedural deck (`max(procedural, voxel)`).
> Replace would mean a scene has no sky at all until somebody authors a whole 4 km cloudscape, which
> is the opposite of the goal and destroys the fallback we deliberately kept. Union also makes Q7's
> trade natural: a hero cloud is baked (shape), the deck around it stays procedural (evolution).
>
> **Q2 — Fix the ATLAS GEOMETRY, not the voxel size.** 8 tiles of 128x128x64 RGBA8 = 32 MiB (our
> noise set is already 16 MiB, so this is a real but affordable increment). The world extent a tile
> covers is a PER-INSTANCE TRANSFORM, not a global constant — so "8 m per voxel" becomes the default
> bake scale rather than something welded into the tile arithmetic, and a closer fly-by re-bakes at a
> smaller extent without an atlas migration. Variety beat resolution: eight distinct shapes are worth
> more to this goal than four finer ones.
>
> **Q3 — No.** RGBA8 uses all four channels; adding R8/RG8 for a hypothetical far-LOD saving would be
> extending the format enum speculatively. Revisit only if the far atlas measurably needs it.
>
> **Q4 — (a) Clamp in the shader with guard bands.** The atlas needs manual UVW handling regardless,
> so this costs nothing extra and leaves the asserted LINEAR/REPEAT policy — which is correct for the
> tiling noise that is 100% of today's volume usage — untouched.
>
> **Q5 — Its own ticket, scheduled AFTER phase 1.** A `MipMap3DGenerator` also retires the procedural
> path's hand-tuned softening curve (the one whose own comment calls the reference's distance mip a
> dead knob), so it pays twice — but it is not on the voxel critical path.
>
> **Q6 — Houdini is OUT, and consciously so.** We author with analytic primitives and our own tools;
> the licence question then does not exist. The half-day `.dvol`/PNG-slice importer still gets built
> so Houdini can be *a* source later for someone who buys a seat — but nothing we ship depends on it.
>
> **Q7 — Accepted.** Evolution and shape become a per-cloud choice, which is exactly what union buys:
> clouds that must visibly build stay procedural, sculpted heroes are baked.


**Q1 — Union or replace?** Does a voxel hero cloud sit *inside* the procedural deck
(`max(procedural, voxel)`, one extra evaluation in the overlap, matches production pp. 193/194), or
does selecting the voxel model *replace* the procedural field for that scene (faithful to the deck's
architecture, but then somebody must author a whole 4 km cloudscape before the sky has any clouds in
it)? My recommendation is union, but it is a genuine architectural fork and everything in §3.5
depends on it.

**Q2 — Instance budget and atlas size.** 8 tiles of 128×128×64 at 8 m/voxel = **32 MiB**, or 16 tiles
of 96×96×48 = **27 MiB**, or 4 tiles = **16 MiB**? This sets the atlas dimensions, which are baked
into the tile arithmetic and are painful to change later. Related: is 8 m/voxel (deck parity, p. 82)
the right choice, or do we want 4 m for closer fly-bys at 4× the memory?

**Q3 — Do we add single-channel image formats?** Adding `R8`/`RG8` to `Core::Formats::ImageFormat` is
a compile-enforced change (`ImageFormat.hpp:153-169`'s `LookupsAreTotal()` + `static_assert` turns a
missing case into a build error, by design). We do not *need* it — RGBA8 uses all four channels for
profile/type/scale/SDF — but a profile-only far-LOD atlas would be 4× smaller at R8. Low priority;
raising it because the format enum is the kind of thing one wants to extend once, not twice.

**Q4 — The volume sampler is REPEAT, and asserted to be.** `VulkanImage.cpp:66-68` creates every 3D
sampler LINEAR/REPEAT and `:76-86` `DESERT_VERIFY`s it, with the comment *"A volume sampler must be
LINEAR/REPEAT regardless of the texture filter"* — correct for tiling noise, wrong for a bounded
hero cloud. Do we (a) clamp in the shader and keep guard bands (my recommendation: it is needed for
the atlas anyway, so it costs nothing extra), or (b) extend the sampler policy with a
`ClampToBorder` variant and relax the assertion? (a) is smaller; (b) is more honest about what a
volume can be.

**Q5 — Does the 3D mip generator get its own ticket?** §4.4's bake-time far atlas sidesteps it for
hero clouds, but the *procedural* path also wants it: the deck's p. 96 distance mip is the correct
answer to noise aliasing and we currently substitute a hand-tuned softening curve
(`CloudDensityProcedural.glslh:282-289`, which openly calls the reference's mip a "dead knob"). A
`MipMap3DGenerator` matching `MipMap2DGenerator` would serve both. Not on the voxel critical path;
worth scheduling separately.

**Q6 — Houdini licensing, decided now or deferred?** §5(a): Apprentice is free but its output is
non-commercial-only. If any authored cloud might ship, someone needs an Indie/FX seat *before* that
cloud is authored, not after. Deferring is fine — (b) and (c) need no external tool at all — but the
decision should be conscious rather than discovered.

**Q7 — Is "pseudomotion only" acceptable?** p. 187's comparison table gives the voxel method "Evolution:
Pseudomotion Only" — a baked cloud does not grow or dissipate; only the detail noise scrolls (p. 95)
and the instance transform animates (p. 183). Our procedural clouds *do* evolve. So enabling the
voxel path on a hero cloud trades evolution for shape. If a scene wants a cloud that visibly builds,
that cloud stays procedural. Confirm that trade is understood and accepted before phase 1.

---

## Appendix — deck citations used, at a glance

| Page | What it establishes |
|---|---|
| 67–72, 79 | Modelling method chosen: fluid simulation in Houdini via Atlas; sourced from voxels/points/meshes/2.5D; edited by cutouts, erosion, squashing; "Frankencloudscapes" |
| 73, 76, 77, 78 | Desired 4096³ walked back to 2048²×256, shipped at 512×512×64 BC4 ≈ 8.388 MB for the density field; map 4 km × 4 km × 500 m |
| 81 | `density = NVDF data fields × profile remaps × detail noise`; the left/right pair proving all character comes from the noise |
| 82, 86 | Voxel size **8 m**; "Dimensional Profile" vs "Explicit Density"; `if (profile > 0) uprez else 0` |
| 84, 98 | The Dimensional Profile is an interior-depth field: 1 in the core, 0 at the surface, radial in every direction |
| 85, 158 | **Modeling NVDFs = 512×512×64, BC6, 1 B/texel, 16.777 Mb, channels {Dimensional Profile, Detail Type, Density Scale}** |
| 92 | The authoring rule: decreasing density ⇒ curly layered wisps; increasing density ⇒ layered billows |
| 93, 94 | Alligator and Curly-Alligator replace Worley and Perlin-Worley; one 128³ 4-channel noise, 4.194 MB |
| 95, 96 | Wind offset before the noise fetch; `mip = log2(1 + abs(distance * scale))`, position scale 0.01 |
| 99, 104, 108, 113, 114, 118 | The complete up-rez sampler — the code our `CloudDensitySample` already is |
| 119 | 8 m voxels up-rezzed to **0.5 m precision, 10 ms @ 960×540; dense grid sampling 30+ ms** |
| 136, 141, 144 | The three lighting formulas that read the Dimensional Profile — all of which we already implement |
| 157 | Frankencloudscape → SDF → **Field Data NVDF 512×512×64, −256 m to 4096 m, Signed Distance** |
| 159, 160 | SDF compression: BC1 at 0.5 B/texel, decoded with `dot(rgb, float3(1.0, 0.03529415, 0.00069204))`; *"Too low = extra steps, Too High = rendering artifacts"* |
| 161 | Memory per cloudscape: vertical-profile 0.541 Mb, envelope 9.437 Mb, **voxel 25.166 Mb** |
| 163, 164 | The hybrid march: `step = max(sdf, adaptive) + jitter`; `if (sdf < 0) fetch and integrate` |
| 166, 167, 168, 169 | **10 → 6.1 → 2.2 / 12 → 8.2 → 4.0 / 10 → 8.0 → 4.0 ms**; "Cost: 2.2 to 4 Milliseconds" |
| 175, 181, 183, 185 | Production: sky-writing proves freeform authoring; Houdini layout over real terrain; sequencer-animated instance transforms; "Frankenclouding Works" |
| 187 | The comparison table — the voxel method is the only one with "Freeform Modeling: Yes", and the only evolution it offers is "Pseudomotion Only" |
| 193, 194 | One bespoke hero cumulus, authored in isolation, placed in an ordinary sky — the production shape of what this document proposes |
