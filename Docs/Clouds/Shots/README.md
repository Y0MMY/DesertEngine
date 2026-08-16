# Reference shots

Rendered by the editor itself, not captured off a screen:

```
scripts/MacOS/RunEditor.sh is the interactive path; these came from the shot mode —
Editor --project Desert.deproj --scene <s>.desce --shot out.png \
       --camera 0,200,0 --look 0,0.35,-1 --shot-frames 90
```

| file | what it shows |
|---|---|
| `before-zenith-empty.png` | Looking straight up at Coverage 0.50 — **empty sky**. The whole visible dome fell inside one feature of a weather field whose dominant scale was 30 km. |
| `before-horizon-moire.png` | The first attempt at a fix — shrinking the noise tiles — which fills the zenith and turns the horizon into a radial moire fan, because a 3 km field repeats forty times over 138 km. |
| `after-fairweather.png` | Fair Weather: scattered cumulus, flat bases, lit tops, blue between. |
| `after-partlycloudy.png` | Partly Cloudy. |
| `after-horizon-aerial.png` | The horizon with the fades measured at the cloud rather than at the shell entry, and their range derived from the layer geometry — the far field recedes into haze instead of ending on a white wall. |
| `after-storm.png` | Storm, for the other end of the range. |

The frame count matters: the clouds accumulate over about ten frames, so a shot
taken on frame one is a picture of the dither rather than of the sky.

## v3 — the Nubis-Cubed parity pass (REQUIREMENTS_CLOUDS.md §10)

| file | what it shows |
|---|---|
| `v3-partlycloudy-mid.png` | Same camera as `after-partlycloudy.png`: the hemispheric ambient (CLD-100/101) — lit faces white, bases luminous grey, no slate-blue cast. Lit:shadow ≈ 2.3:1 in linear (CLD-112). |
| `v3-partlycloudy-zenith.png` | Zenith: alligator detail erosion (CLD-110) — wispy fringes, no empty sky. |
| `v3-partlycloudy-horizon.png` | Horizon: aerial perspective intact, no moire, no shadow-map seam (CLD-103). |
| `v3-storm.png` | Storm: deck interiors stay readable (CLD-104 depth-modulated ambient extinction, 3 MS octaves). |
| `v3-sunset.png` | Sunset: warm direct light survives to the horizon (CLD-102), mauve-grey ambient tracks the sky. |
| `v3-default-scene.png` | The default sandbox scene after the field migration. |

## v4 — CLD-114, the profile normalisation (REQUIREMENTS_CLOUDS.md §10.2)

Before/after pairs, 90 frames each, same camera, same scene. `before` is the dev tip at
`1b9e7677`; `after` is CLD-114 plus the Ambient Occlusion retune. `lit:shadow` below is the ratio
of the 95th to the 5th percentile of LINEAR luminance over the cloud pixels of the frame, the same
measure the 2.3:1 anchor of `v3-partlycloudy-mid.png` was quoted in.

| pair | camera | lit:shadow before → after |
|---|---|---|
| `v4-*-ueshowcase-mid.png` | `--camera 0,200,0 --look 0,0.35,-1` | 1.89 → **2.74** |
| `v4-*-partlycloudy-mid.png` | the anchor camera above | 1.48 → **4.41** |
| `v4-*-ueshowcase-horiz.png` | front-lit horizon, `--look -0.307,0.12,-0.563` | 1.47 → **1.58** |
| `v4-*-sunset-backlit.png` | along the scene's own sun, `--look 0.952,0.12,0.281` | 3.19 → **3.22** (rim p95 0.921 → 0.927) |
| `v4-*-storm-backlit.png` | along the scene's own sun, `--look 0.351,0.902,0.251` | 2.17 → **3.87** |
| `v4-*-overcast.png` | under the blanket | frame mean 0.104 → 0.087, structure visible |
| `v4-*-stratus.png` | under the blanket | frame mean 0.224 → 0.140, structure visible |

The horizon pair moves least, and that is the honest reading: at 10-100 km the aerial-perspective
and horizon fades have already blended the cloud most of the way into the sky, so there is little
interior left to occlude. The mid-elevation pairs are where the change lives.

Two instrumented frames, kept because they are the measurement the change rests on and not a look:

| file | what it shows |
|---|---|
| `v4-diagnostic-visible-profile.png` | The contribution-weighted mean `dimensional_profile` of every ray, thresholded: red > 0.06, yellow > 0.12, white > 0.20. The samples the eye integrates sit at 0.06-0.20 — the bottom fifth of the domain the deck's p.136/p.141 formulas are written against. |
| `v4-diagnostic-ambient-occlusion.png` | `CloudAmbientOcclusion` at full strength before the fix: red 0.6-0.9, yellow 0.3-0.6, white ≤ 0.3. Most cloud volume was between 0.6 and 0.9, i.e. the strength knob was scaling a factor that was already almost 1. |

## v5 — the clouds join the physical atmosphere

The three follow-ups Sky Phases 3 and 4 deferred, landing together because they are one
recalibration: the cloud march samples the aerial-perspective froxel volume instead of its own
approximation, the cloud ambient's sky term reads the marched average sky, and the fog's directional
lobe reads the sun's post-transmittance illuminance. All three are scoped to
`SkyModel::PhysicalAtmosphere`; `ArtisticGradient` is frozen and is unchanged to the bit.

### The estimator, stated

`lit:shadow` below is the same quantity the v4 table quotes — the 95th over the 5th percentile of
LINEAR luminance (sRGB-decoded, Rec.709) over the cloud pixels of the frame — but the MASK is now
defined instead of implied, and the absolute numbers therefore differ from v4's:

> **a cloud pixel is a pixel that differs from a clouds-off render of the same scene, same camera,
> same sky model, by more than 3 levels in any 8-bit channel.**

That is not a heuristic about colour: the cloud layer is exactly what the two frames differ by. Every
row below is measured with the same mask against the same clouds-off reference, so before/after and
gradient/physical are comparable within the table; they are NOT comparable with v4's numbers.

### The calibration table

90 frames each, `--camera 0,200,0`. `before` is dev at `a0f6c820`; `after` is this change.

| camera | model | before | after |
|---|---|---|---|
| `Clouds_UEShowcase` mid, `--look 0,0.35,-1` | ArtisticGradient | 4.38 | **4.38** |
| `Clouds_UEShowcase` mid | PhysicalAtmosphere | 4.24 | **2.20** |
| `Clouds_UEShowcase` horizon, `--look -0.307,0.12,-0.563` | ArtisticGradient | 2.06 | **2.06** |
| `Clouds_UEShowcase` horizon | PhysicalAtmosphere | 2.12 | **1.58** |
| `Clouds_PartlyCloudy` anchor, `--look 0,0.35,-1` | ArtisticGradient | 6.19 | **6.19** |
| `Clouds_PartlyCloudy` anchor | PhysicalAtmosphere | 7.41 | **3.62** |
| `Clouds_Sunset` backlit, `--look 0.952,0.12,0.281` | ArtisticGradient | 2.75 | **2.75** |
| `Clouds_Sunset` backlit | PhysicalAtmosphere | 5.03 | **4.89** |

The gradient rows are identical to two decimals. They are NOT bit-identical frames and nothing here
claims they are: the march dithers its entry point by a per-frame hash, so a 90-frame accumulation is
a stochastic estimate, and the SAME binary rendering the SAME gradient scene twice differs by up to
14, 18 and 21 8-bit levels on 1.4% to 6.8% of channels at these three cameras. The before/after
gradient pairs differ by 11, 18 and 24 levels on 0.9% to 6.4% — the same order as that repeat, i.e.
the noise floor. What IS stable through the noise is the estimator itself: the same-binary repeats
measure 4.39 / 2.06 / 6.19 against the table's 4.38 / 2.06 / 6.19, which is why it is the anchor and
a pixel diff is not.

### Where the physical rows went, and why the ambient is not what moved them

The two ambient contributions are the only knobs this change was allowed to retune, so they were
measured on their own — the same three cameras with `Atmospheric Perspective` forced to 0, which
leaves the ambient as the only term that differs between the models:

| camera (AP dial at 0) | gradient | physical, contributions x1.0 | physical, x2.2 |
|---|---|---|---|
| `Clouds_UEShowcase` mid | 5.01 | **4.88** | 3.59 |
| `Clouds_UEShowcase` horizon | 2.19 | **2.28** | 1.75 |
| `Clouds_PartlyCloudy` anchor | 6.72 | **8.08** | 5.73 |

At the authored contributions the physical ambient already lands within 3% and 4% of the gradient on
two of the three cameras and 20% on the third; no single rescale improves the set, and every scale
tried made two of the three worse. **The retune's outcome is therefore that
`AmbientSkyContribution` / `AmbientGroundContribution` keep the numbers CLD-100/101/102 measured** —
the recalibration happened at the SOURCE, not at the multipliers.

That source is the part that had to be got right. The published Distant Sky Light is a FULL-SPHERE
mean, and half of what it averages is the lit ground: at a 50-degree sun over the shipped 0.3-albedo
surface the sky half of the march is 0.205 in luminance with an R/B of 0.27, the ground half is
1.613, and the sphere mean is 0.909 with an R/B of 0.98. Feeding a sphere mean to `CloudAmbient` —
which already blends a sky radiance against a separate ground-bounce radiance — counts the ground
twice and paints shadowed cloud faces the colour of dirt. The fill now reduces the same 64 marched
radiances twice and stores both: texel 0 the sphere mean (the height fog's, UE's own quantity),
texel 1 the sky half (the cloud ambient's). Measured on the mid camera, the darkest decile of cloud
pixels comes out at an R/B of 0.391 with the sphere mean and 0.330 with the sky half — the sky half
is the bluer shadow, and the ratio it produces (2.20) is the better of the two anyway (2.16). For
scale, CLD-100 recorded 0.11 as the navy the zenith texel alone produced, and the artistic gradient's
own shadowed decile sits at 0.653 in these frames: the physical shadow is a blue-grey between the
two, which is the deck's luminous blue-grey and neither of the two failures.

What actually moved the physical rows is the **aerial perspective**, and it moved them by being
right. Along the horizon camera's ray the volume holds a transmittance of 0.741 and an in-scatter of
0.253 at 20 km; a cloud whose lit and shadowed faces sit at 0.9 and 0.3 comes back at 0.92 and 0.47,
i.e. 3.0:1 becomes 1.96:1 by arithmetic. The gradient's approximation reached a blend of about 0.10
at the same distance, because its ramp runs from 3 km to 138 km linearly. Twenty kilometres of real
air flattens distant contrast; that is what aerial perspective IS, and no setting of the ambient
contributions can restore contrast the air removed — with the ambient at ZERO the physical rows read
2.54 / 1.84 / 4.07, still short of the gradient's 4.38 / 2.06 / 6.19.

### The frames

| file | what it shows |
|---|---|
| `v5-before-ueshowcase-horizon-physical.png` | The seam this change exists to remove: the terrain recedes into haze while the cloud deck above it stays white and crisp to the horizon line. |
| `v5-after-ueshowcase-horizon-physical.png` | The same frame with the deck sampling the same froxels the terrain haze comes out of — the layer and the air under it dissolve together and the horizon has no colour step across it. |
| `v5-before-ueshowcase-mid-physical.png` | Mid elevation before: cloud bases lit by the hand-tuned dome, distance fade by the analytic approximation. |
| `v5-after-ueshowcase-mid-physical.png` | After: shadowed faces keep a blue-grey character (the sky half, R/B 0.27) rather than going grey or navy, and the far band recedes physically. |
| `v5-before-sunset-backlit-physical.png` | A 7-degree sun, looking along it: the far deck holds a cool cast that the golden haze band under it does not share. |
| `v5-after-sunset-backlit-physical.png` | The far deck takes the same gold; the near rim and the silver lining are untouched (lit:shadow 5.03 -> 4.89). |
| `v5-after-ueshowcase-mid-gradient.png` | The parity control: the artistic gradient after the change, identical to its `before` within one 8-bit level. |
| `v5-before-fog-showcase.png` | `Fog_Showcase` as it shipped — artistic gradient, sun lobe fed by the SKY's sun. |
| `v5-after-fog-showcase.png` | The same scene on the physical atmosphere with the lobe fed by the LIGHT's post-transmittance illuminance and `Directional Inscattering Luminance` authored to (10, 8, 5.5) to replace what the switch removed. |

## UE-parity pass (light shafts + showcase scene)

| file | what it shows |
|---|---|
| `ue-showcase-sun.png` | `Clouds_UEShowcase.desce`: sun disc + bloom glow, Ultra (full-res) clouds, deeper zenith. |
| `ue-showcase-sunset-shafts.png` | The same scene with the sun on the horizon: the new Light Shaft Bloom (DirectionalLight, UE params) — crepuscular rays through the deck, clouds lit from below. |
