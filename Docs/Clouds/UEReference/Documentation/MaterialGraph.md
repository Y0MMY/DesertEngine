# `m_SimpleVolumetricCloud` — what the graph actually does

Extracted from `../m_SimpleVolumetricCloud.graph.txt` (1.19 MB, ~250 nodes), copied out of the UE material
editor. Analysed programmatically rather than read: the node-type histogram, the artist's own comment
groups, the named reroutes and the parameter defaults carry the whole architecture.

## The graph in the author's own words

The comment boxes are the section headings of the material, in order:

```
Reads + Reroutes
Sample Cloud Profiles by Layout Texture
Apply Mask Texture
Global Coverage Control
Noise 1  ->  Noise Bias  ->  Noise Strength  ->  Noise Movement
Noise2 (Distortion)
Noise3
Global Density Control
Fade Near Camera
Prevent Shadowing from Above Clouds
Material Look Parameters
Volumentric Advanced Output      [sic]
Lightning
Bypass on mobile
```

That is the pipeline: read parameters, look up the per-type vertical profile through the layout texture,
apply the regional mask, apply coverage, then three noise stages, then density, then two corrections
(near-camera fade, and a fix for shadowing from clouds above), then the look parameters and the advanced
output.

## Node census

| Count | Node |
|---|---|
| 56 | Multiply |
| 31 | Reroute |
| 19 | VectorParameter, 19 Add |
| 18 | NamedRerouteUsage, 11 NamedRerouteDeclaration |
| 14 | **TextureSample**, 4 TextureObjectParameter |
| 13 | ComponentMask, 12 Constant |
| 10 | ScalarParameter, 10 LinearInterpolate |
| 8 | Subtract, 8 MaterialFunctionCall |
| 4 | ShadingPathSwitch (a mobile branch on every root pin) |
| 3 | **CloudSampleAttribute**, 2 VolumetricAdvancedMaterialInput, 1 VolumetricAdvancedMaterialOutput |

Named reroutes — the semantic waypoints the author gave things:
`ProfileObject`, `LayoutObject`, `MaskObject`, `NoiseObject`, `SkyPlacement`, `SkyTexScale`, `CloudWind`,
and **`ConservativeDensity.r`, `.g`, `.b`, `.a`**.

`ConservativeDensity` being four separate channels settles a question: the cheap early-out is computed
**per cloud type**, not once for the layer.

`Km-to-Cm` is called as a material function — the layout is authored in kilometres and converted once, the
same discipline this engine uses.

## The root pins

| Root input | Driven by |
|---|---|
| Albedo | `lerp(VectorParameter, VectorParameter, alpha)` — the cloud colour is a BLEND OF TWO authored tints, not one |
| Emissive | a Multiply chain |
| Extinction ("Исчезновение") | `saturate(...)` — the extinction is clamped to [0,1] before the global density scales it |
| Ambient Occlusion | `lerp(...)` — authored, not a constant |

Each of the four goes through a `ShadingPathSwitch` whose Mobile branch is a constant. Irrelevant to us.

## The numbers that matter, and what ours are

### Phase — `Phase_Controls` = (0.800, 0.1667, 0.575, 1.0)

| | UE | Ours |
|---|---|---|
| PhaseG (forward lobe) | **0.800** | 0.6 |
| PhaseG2 (second lobe) | **0.1667** | absent — single lobe only |
| PhaseBlend | **0.575** | absent |

The second lobe is nearly isotropic (0.167) and it is blended in at 0.575 — more than half. That is the
silver lining: a strong forward lobe for the rim, a near-isotropic one for the body, weighted toward the
body. A single lobe cannot produce both.

### Multiple scattering — `Multiscatter_Controls` = (0.6667, 0.250, 0.180, 1.0), octave count **2**

| | UE | Ours (before this reading) |
|---|---|---|
| Contribution | **0.667** | 0.5 |
| Occlusion | **0.250** | 0.5 |
| Eccentricity | **0.180** | 0.5 |
| Octave count | 2 (so MSCOUNT = 3 orders) | 3 orders — same |

Occlusion 0.25 against our 0.5 is the largest single discrepancy and it points the right way: each
successive order is absorbed FAR less, so light reaches the core and the cloud stops being grey.
Eccentricity 0.18 sends the higher orders almost straight to isotropic.

### Density and colour

| Parameter | Value |
|---|---|
| `Cloud_GlobalDensity` | **0.008** per metre = **8 per km** — the value this programme had already arrived at by measuring against the frame |
| `Cloud_AlbedoColor` | 0.98, 0.98, 0.98 (alpha 0.5 carries the ambient-occlusion amount) |
| `Cloud_GlobalCoverage` | **-0.2** — a BIAS, not a 0..1 slider |
| `Layout_CloudGlobalScale` | 256 km |

### Noise stack

| Parameter | Value | Reading |
|---|---|---|
| ~~`Noise_Bias`~~ **`Noise_Strength`** | (0.800, 0.080, 0.030, 2.5) | base 0.8, detail 0.08, third 0.03; alpha is StormyDistortFactor |
| `Noise_Bias` (the actual one) | **(0.500, 0.800, 0.500, 0.0)** | what is SUBTRACTED from each octave before its strength is applied |
| `Noise1_Coordinates` | (4.167, 4.444, **9.091**, 7.0) | vertical frequency ~2.2x the horizontal; alpha is the wind multiplier |
| `Noise2_Coordinates` | (60, 60, **75**, **-6.0**) | ~14x the base frequency, and the wind runs the OTHER WAY |
| `Noise3_Coordinates` | (30, 40, 25, 8.0) | third octave, unused by default (`UseNoise3` off) |
| Texture | `VT_PerlinWorley_Balanced` | Perlin-Worley, as we now bake |

The opposed wind multipliers (+7 and -6) are worth copying: two noise layers drifting against each other
is what stops the whole sky reading as one rigid object sliding past.

## Три поправки к этому документу (2026-08-19)

Внесены после полного разбора графа — `ShapeModel.md`. Проверены тимлидом по исходному файлу.

1. **Вектор `(0.8, 0.08, 0.03, 2.5)` — это `Noise_Strength`, а не `Noise_Bias`** (L5519–L5521).
   Настоящий `Noise_Bias` = `(0.5, 0.8, 0.5, 0.0)` (L5482–L5484). Разница не косметическая: одно
   умножает октаву, другое вычитается из неё до умножения. Таблица выше исправлена.
2. **Четыре канала `ConservativeDensity` — не четыре вида облаков.** Вывод «дешёвый ранний выход
   считается на вид» из этого не следует; см. `ShapeModel.md`.
3. **`VT_PerlinWorley_Balanced` в графе отсутствует** — там `DefaultVolumeTexture` (L2262). Имя взято
   из инстанса, то есть из скриншота, и должно так и цитироваться.

## Two techniques we do not have

**"Fade Near Camera".** UE fades the clouds out close to the camera. Without it a camera that enters the
layer fills the screen with a wall of density; with it the near metres thin out. Ours has nothing.

**"Prevent Shadowing from Above Clouds".** A dedicated correction, sitting between the noise stack and the
look parameters. The name is self-explanatory and the defect it fixes is real: a cloud that is shadowed by
the whole layer above it goes black, and the sky loses its ceiling.

## What this changed on our side

Applied immediately (the fields already existed):
`MultiScatterContribution` 0.5 -> 0.667, `MultiScatterOcclusion` 0.5 -> 0.25,
`MultiScatterEccentricity` 0.5 -> 0.18.

Queued, needing the phase function and two new parameters:
the second Henyey-Greenstein lobe (G2 = 0.1667) and its blend (0.575).

Confirmed, no change needed: extinction 8/km, three scattering orders, Perlin-Worley for the base shape,
vertical anisotropy in the base noise, the premultiplied composite, the layer-entry-relative tracing
distance.
