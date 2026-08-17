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

## v6 — the temporal stage, in motion

Everything above was shot from a FIXED camera, and a fixed camera is the one condition under which
the cloud temporal resolve's reprojection is exact by construction: every pixel finds its own history
at its own texel. So a still can never exercise the parts of that stage that exist for a moving one,
and `CloudTemporalResolve.shader` says so in its own header — it names six artefacts in advance
precisely because none of them could be looked at. This section is the first time anyone looked.

### Driving it

```
Editor --project Desert.deproj --scene <s>.desce \
       --camera 0,200,0 --look 0,0.35,-1 \       # where the path starts (unchanged)
       --camera-to 200000,200,0 \                # where the eye ends   (optional)
       --look-to 0.8660,0.35,0.5 \               # where the aim ends   (optional, slerped)
       --shot-frames 90 \
       --shot-sequence <dir> --shot-every 1 \    # every Nth frame, not only the last
       --shot out.png                            # the last frame, as before
```

`--camera-to` / `--look-to` interpolate from `--camera` / `--look` across exactly the `--shot-frames`
warm-up frames: position linearly, aim by shortest-arc slerp so equal frames are equal ANGLES (a
component-wise lerp would put a hump in the middle of every measurement that belonged to the
interpolator rather than to the renderer). Give one and the other holds still, so a pure translation
stays a pure translation. `--shot-sequence` writes `frame_00001.png` .. `frame_000NN.png`, frame N
rendered from path parameter N; with `--shot-every` dividing `--shot-frames`, the last file and the
`--shot` PNG are the same image.

**A still command renders one more frame than it used to.** The camera is now placed BEFORE the frame
it affects rather than after, so `--shot-frames N` renders N frames from the requested pose instead of
N-1 — which on a moving path is not bookkeeping: under the old ordering the last captured frame of a
120-degree pan sat 1.35 degrees, about 28 pixels, short of the endpoint `--look-to` named, and every
"same pose, arrived differently" comparison was measuring that shift instead of the renderer. The
effect on an existing still is exactly that one frame and nothing else, and it was measured rather
than argued: `after-placement --shot-frames 91` against `before-placement --shot-frames 90` agrees to
0.040-0.061 mean grey levels, INSIDE the 0.022-0.084 spread of the same binary run twice. So an old
command at `--shot-frames N` reproduces its old picture at `--shot-frames N+1`; left at N it differs
by 0.13-0.23 mean, p99 1.1-2.1, max 13 grey levels — inside the +/-14-24 level repeat noise this file
already records above, but named here rather than left to be discovered.

A shot has never been bit-reproducible in any case: the timestep is `glfwGetTime()`, so the wind has
advanced by a different amount by frame 90 on every run. That is the noise the tables above are quoted
against, and it is why every claim here is a spread and not an equality.

Also new, and for the same reason the rest of this exists: in capture mode a `--scene` that does not
exist is now fatal. The loader logged it and carried on, which is right for an editor and wrong for a
capture — the run went on to write PNGs named after the scene that was asked for, holding the picture
of a different one.

### The metric

Frame-to-frame difference of Rec.709 luma, in 8-bit grey levels, over a converged tail (the first 30
frames of each sequence are dropped — convergence from cold is a different question from behaviour in
motion). Reported as mean and p99 over the whole frame and over the leading and trailing thirds of the
direction of travel, because disocclusion is a bright band on the leading edge and ghosting a smear on
the trailing one, and a mean over the whole frame hides both.

Two controls are what make the numbers mean anything, and both turned out to be necessary:

* **A still of the same scene, measured with the same thirds.** The right third of this sky already
  differs 3.8x more than the left in a STILL, because that is where the cloud is. Read a motion ratio
  against 1.0 instead of against that and every pan appears to have a disocclusion band.
* **`Animation Speed = 0`.** A still sequence measures 0.357 mean; with the wind stopped it measures
  **0.030**. So 92% of the "floor" is the clouds genuinely moving, and the resolve's own residual on a
  still is three hundredths of a grey level. (A sequence writes a PNG per frame behind a device-idle
  wait, so a frame costs ~1 s of wall clock and the wall-clock wind advances far more per frame than
  it would at 60 fps. Camera steps are unaffected; the wind-driven rows are an upper bound.)

### What the six predicted artefacts actually measure

`Clouds_UEShowcase`, High unless noted, 90 frames, `--camera 0,200,0`.

| the header's prediction | measured |
|---|---|
| Disocclusion trails, leading edge | PRESENT under rotation, and only under rotation. Excess over the still baseline: leading/trailing 1.08 (10-degree pan), **1.19** (120-degree pan) at High, 1.09 / **1.33** at Ultra. Under a 2 km translation it is 0.96 — clouds 5-100 km away have no parallax to disocclude at the screen edge, so that case shows up in p99 (14.4 against a 4.3 still) rather than in the thirds. |
| Inertia on fast rotation | PRESENT and the largest effect found. The frame you land on after a 120-degree pan differs from a still at the same pose by **10.4** mean / 58.9 p99 (High), **10.7** / 59.7 (Ultra). The header says "the inertia is in the pixels that stayed" — measured, the trailing third differs **40%** more than the leading third (11.9 vs 8.5 High; 12.2 vs 8.7 Ultra). Decays as (1 - blend)^n, so ~0.37 s at 60 fps. |
| Softness in the uncovered band | PRESENT. Laplacian detail in the same frame, moving vs settled: **-14%** at High (1.295 vs 1.502, uniform across the frame) and **-26%** at Ultra (1.480 vs 1.963), where it is edge-weighted — the leading third loses most. Full resolution keeps the interior sharp and pays at the edge; half resolution pays everywhere. |
| Shell-parallax error (second layer) | NOT VISIBLE above the single-layer case. `Clouds_TwoLayerShowcase` under the same 2 km translation measures 1.214 / 10.556 against the one-layer scene's 1.471 / 14.442 — the two-layer scene is QUIETER, not noisier. Predicted magnitude for the cirrus sheet reprojected through the deck's mid-surface is ~1.7 px/frame, which the neighbourhood clamp absorbs, exactly as the header claims. |
| Ghosting on wind-driven silhouette change | The clamp has large authority on the scene the header names. `Clouds_Storm` (Animation Speed 2.2), still camera, so the only thing moving is the cloud: Temporal Off **2.287**, clamp 0.75 **1.464**, 1.50 (authored) **1.109**, 4.00 **0.644**. A 5.3x sweep of the knob moves the metric 2.3x — this is not a dead setting. |
| Sun-glint flicker | PRESENT and monotone in the direction the header predicts. Still, looking along the sun: clamp 0.75 **0.172** / p99 3.088, 1.50 **0.157** / 2.556, 4.00 **0.143** / 2.365. Raising the clamp buys the flicker away, exactly as written. |

### Does the stage earn its place?

| | still | 120-degree whip pan |
|---|---|---|
| Temporal Off | 0.819 / 7.34 | 8.644 / 66.75 |
| Reprojection (High) | **0.357 / 4.32** | **8.403 / 65.00** |

On a still the stage removes 56% of the frame-to-frame change — that is the boiling it exists for.
Under a fast pan it removes 2.8%, because most reprojected UVs leave the screen and carry no history;
the header predicted exactly this. It does not make motion WORSE, which was the open question.

### The Ultra checkerboard, and one hypothesis that was wrong

Ultra is the only tier that checkerboards (full resolution + history), and half its pixels each frame
are pure clamped history at blend weight 0. Its clamp knob behaves differently from every other tier's:

| Temporal Clamp Scale | Ultra still | Ultra whip pan |
|---|---|---|
| 0.75 | 0.459 / 5.51 | **7.249 / 53.31** |
| 1.75 (authored) | 0.367 / 5.63 | 7.711 / 61.75 |
| 4.00 | **0.287 / 4.94** | 7.795 / 61.52 |

Tightening buys 6.0% mean and 13.7% p99 under motion and costs 25% on a still; loosening does the
reverse. Ultra is documented as the stills-and-captures tier and 1.75 is authored toward that side, so
this is a live trade behaving as specified, **not** a defect — but it is a trade nobody could see
before, and if Ultra is ever used in motion the number to move is this one.

The obvious mechanism-level suspicion was that the 3x3 clamp box is built from all nine taps while the
fallback mean deliberately counts fresh ones only, so under the checkerboard 4-5 of the 9 taps that
widen the box were marched from the PREVIOUS camera. Restricting the box to fresh taps was tried and
**made Ultra strictly worse** — +11% on the whip pan, +25% on a still and on the translation — while
leaving High identical to 0.02% (the correct control: with no checkerboard every tap is fresh and the
code reduces to what it was). The reason is that for a stale pixel its own stale texel is not
pollution: it is the only same-texel evidence in the frame, one frame old, and removing it clamps the
history away from where it legitimately sits. The shader is right as written; the change was reverted.
