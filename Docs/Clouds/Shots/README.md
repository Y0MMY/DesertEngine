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

## UE-parity pass (light shafts + showcase scene)

| file | what it shows |
|---|---|
| `ue-showcase-sun.png` | `Clouds_UEShowcase.desce`: sun disc + bloom glow, Ultra (full-res) clouds, deeper zenith. |
| `ue-showcase-sunset-shafts.png` | The same scene with the sun on the horizon: the new Light Shaft Bloom (DirectionalLight, UE params) — crepuscular rays through the deck, clouds lit from below. |
