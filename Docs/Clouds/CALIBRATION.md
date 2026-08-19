# Calibrating the cloud layer against the UE reference — what was measured and what it found

The reference is `UEReference/UE_mid.png`: Unreal's shipped volumetric cloud, default material, default
component, rendered from a ground camera. The file is **git-ignored, not absent** (`.gitignore`:
`Docs/Clouds/UEReference/*.png`) — it is in the working tree of anyone who has it and in nobody's clone.
A checkout without it cannot re-measure the reference; that is a property of the ignore rule, not of the
measurement. The comparison is not by eye. `Tools/ImageStat` (built from the
vendored `stb_image`) reports five numbers over the sky region of a frame, and it was these that found the
defects — three attempts at tuning by eye before it existed produced nothing.

| metric | what it says |
|---|---|
| `p05` | how DARK the darkest twentieth is — the blue gaps and the shadowed undersides |
| `p50` | the overall level |
| `p95` | how bright the highlights are — sunlit cloud tops |
| `contrast` = p95 − p05 | the dynamic range the image actually occupies |
| `sat` | mean saturation — whether the sky is blue or washed |

## The progression

| frame | mean | p05 | p50 | p95 | contrast | sat |
|---|---|---|---|---|---|---|
| **UE reference** | 0.602 | **0.316** | 0.648 | **0.798** | **0.482** | 0.190 |
| ours, before | 0.650 | 0.567 | 0.654 | 0.731 | 0.164 | 0.411 |
| ours, after the shadow ray | 0.728 | 0.677 | 0.729 | 0.783 | **0.106** | 0.209 |
| **ours, calibrated** | 0.565 | 0.389 | 0.585 | 0.722 | **0.333** | 0.235 |
| ours, ACES (T-ACES, 2026-08-19, horizon) | 0.595 | 0.365 | 0.616 | 0.786 | **0.420** | 0.208 |

## The two measured rectangles, both now recorded

Not one of the rows above recorded the region it was measured over, which made every one of them a
number nobody could check. Both have been recovered, and they are **different rectangles** because the
two kinds of image are different kinds of thing.

**The reference — `UE_mid.png 377 169 2035 991`.**

```
ImageStat Docs/Clouds/UEReference/UE_mid.png 377 169 2035 991
  -> mean 0.609  p05 0.321  p50 0.650  p95 0.800  contrast 0.479  sat 0.192
```

against the recorded `0.602 / 0.316 / 0.648 / 0.798 / 0.482 / 0.190` — agreement to a couple of pixels
of frame edge. **The reference row is therefore verified, not testimony.** The target of 0.482 is a
measurement anybody holding the file can repeat.

Why that rectangle and not the whole image: `UE_mid.png` is 2554x1036 and is a screenshot of the whole
Unreal EDITOR, not a rendered frame. The dark panel chrome around the viewport is what pins `p05` at
0.082 over any rectangle that includes it — including the whole image, and including this document's
own frames' rectangle, which over the reference lands in the top-left outliner instead of the sky. Only
the viewport interior is the picture. The rectangle is stable under inset (nudging it to
`450 240 1960 920` moves contrast 0.479 -> 0.477), which is the check that it contains no chrome.

The two other elevations are in the same directory and are indicative rather than canonical — their
windows are a few pixels different (2531x1069 and 2547x1058), so the mid frame's rectangle is close but
not verified against a recorded row for them:

```
UE_zenith.png  377 169 2035 991 -> mean 0.569  p05 0.335  p50 0.496  p95 0.813  contrast 0.479  sat 0.297
UE_horizon.png 377 169 2035 991 -> mean 0.595  p05 0.365  p50 0.622  p95 0.803  contrast 0.438  sat 0.286
```

**Our frames — the full width by the top 71.9%.** Recovered by search:
`ImageStat E11_calibrated_vs_UE.png 0 0 1103 480` returns the "ours, calibrated" row exactly, so the
historical region is sky only, ground excluded. Every row from here down uses that same fraction and
states it: at 1280x766 it is `0 0 1280 552`.

## T-ACES: changing the operator, 2026-08-19

Decision D-10. Until this landed, the reference and our frames went through different tonemappers
(UE's ACES-derived film curve against our extended Reinhard), so the table above was reporting the gap
between two OPERATORS as though it were a gap between two skies. The remaining discrepancy after
calibration was a contrast of 0.333 against 0.482 — and contrast is precisely the axis two tonemappers
differ on.

Frames: `Shots/TACES_{before,after}_*.png`, three elevations of `Clouds_Demo` plus two scenes with no
cloud in them, all from the same cameras, all measured over the same rectangle.

| frame (Clouds_Demo, camera 0,200,0) | mean | p05 | p50 | p95 | contrast | sat |
|---|---|---|---|---|---|---|
| **UE reference** (recorded above) | 0.602 | **0.316** | 0.648 | **0.798** | **0.482** | 0.190 |
| zenith `0,0.9,-1` — Reinhard | 0.497 | 0.338 | 0.550 | 0.669 | 0.331 | 0.186 |
| zenith — **ACES** | 0.472 | 0.240 | 0.550 | 0.723 | **0.483** | 0.234 |
| mid `0,0.45,-1` — Reinhard | 0.445 | 0.361 | 0.412 | 0.633 | 0.273 | 0.326 |
| mid — **ACES** | 0.391 | 0.267 | 0.343 | 0.676 | **0.408** | 0.404 |
| horizon `0,0.12,-1` — Reinhard | 0.583 | 0.430 | 0.593 | 0.719 | 0.289 | 0.204 |
| horizon — **ACES** | 0.595 | 0.365 | 0.616 | 0.786 | **0.420** | 0.208 |

**What it found: the residual gap was the operator, not the clouds.** At the zenith the contrast goes
from 0.331 to 0.483 against the reference's 0.482, and it gets there from both ends at once — the blue
gaps drop (p05 0.338 → 0.240, the reference is 0.316) while the sunlit tops rise (p95 0.669 → 0.723,
the reference is 0.798). Nothing about the cloud field changed between those two rows; only the curve
did. Three attempts to close that gap by tuning the clouds would have chased a defect that was not in
them, which is the same trap §1 of this document records for the shadow ray.

### The Reinhard branch is the same operator it was, to five digits

Moving the operator out of `main()` and into a function is the kind of refactor that changes a picture
by accident. `Shots/TACES_guard_reinhard_after_refactor.png` is `Clouds_Demo` at the horizon, on the
NEW build, with the scene set back to `Tonemapper: Reinhard`:

| frame | mean | p05 | p50 | p95 | contrast | sat |
|---|---|---|---|---|---|---|
| horizon, OLD build (no operator field, always Reinhard) | 0.583 | 0.430 | 0.593 | 0.719 | 0.289 | 0.204 |
| horizon, NEW build, scene on Reinhard | 0.583 | 0.430 | 0.593 | 0.719 | 0.289 | 0.204 |

Identical in every figure ImageStat reports. Reinhard is a live alternative, not a path that merely
still compiles.

### The change is global, and one non-cloud scene says so out loud

| frame | mean | p05 | p50 | p95 | contrast | sat |
|---|---|---|---|---|---|---|
| `CornellDemo` (interior, no sky) — Reinhard | 0.487 | 0.148 | 0.527 | 0.705 | 0.556 | 0.310 |
| `CornellDemo` — **ACES** | 0.474 | 0.062 | 0.515 | 0.769 | 0.708 | 0.330 |
| `Fog_Showcase` — Reinhard | 0.730 | 0.650 | 0.729 | 0.811 | 0.161 | 0.237 |
| `Fog_Showcase` — **ACES** | 0.795 | 0.699 | 0.798 | **0.880** | 0.181 | **0.120** |
| `Fog_Showcase` — ACES at Exposure 0.22 (probe only, NOT committed) | 0.411 | 0.299 | 0.403 | 0.541 | 0.242 | 0.287 |

Cornell behaves the way the cloud frames do: more range at both ends, saturation held.

`Fog_Showcase` does not, and the numbers say why it is not the operator's fault. Under Reinhard the
DARKEST twentieth of its sky already measured 0.650 — that scene is exposed one to two stops hot, and
Reinhard's gentle compression was hiding it. ACES has a shoulder, so the same radiance lands on it and
desaturates toward white (sat 0.237 → 0.120): the sky stops being blue and becomes an even wash. The
probe row is the proof that the exposure is the cause: at the exposure the cloud demo already uses for a
sun of 22 (0.22 against `SunIntensity` 22), the same scene through the same ACES comes back with MORE
saturation than it ever had on Reinhard (0.287) and half again the contrast.

**`Fog_Showcase` therefore ships on `Tonemapper: Reinhard`, and that is what the operator being a scene
property is FOR.** A scene whose exposure was authored for a curve keeps that curve until somebody
re-exposes it; it does not get a re-grade nobody has looked at. Re-exposing it was not folded into this
change, because scene authoring needs its own reference and mixing it in would have made the operator's
own before/after unreadable.

### Named follow-up: re-expose `Fog_Showcase` and move it to ACES

Everything the next person needs is already measured:

* **State today.** `Editor/Resources/Assets/Scenes/Fog_Showcase.desce`, `Tonemapper: Reinhard`,
  `Exposure: 1.0`, sky `SunIntensity: 22`, physical atmosphere (`Model: 1`).
* **The defect.** `p05` 0.650 under Reinhard — the scene is 1–2 stops hot, independently of any
  tonemapper. This is the same "one sun described by two numbers" trap §2 of this document records.
* **The starting point.** Exposure 0.22 gives mean 0.411 / p05 0.299 / contrast 0.242 / sat 0.287.
  Saturation and contrast are then better than the scene ever had; the mean is low, so the answer is
  probably between 0.22 and 1.0, not at either end.
* **Done when.** The scene is on ACES with a frame attached and a row in this table.
* **The guard.** `SceneTonemapMigration.EveryRepositorySceneIsOnACESExceptTheOneNotYetExposedForIt`
  fails if the scene is moved to ACES while `Exposure` is still 1.0, and ALSO fails once `Exposure`
  changes — telling whoever re-exposed it to finish the job and delete the exception. The exception
  cannot rot into folklore.

Frame of the shipped state, taken on the NEW build with the scene on Reinhard:
`Shots/TACES_guard_fog_reinhard.png` — `0.730 / 0.650 / 0.729 / 0.811 / 0.161 / 0.237`, identical in
every figure to the old build's frame. The scene ships as it was.

### The other ten scenes were checked, and none is a second `Fog_Showcase`

Every repository scene was shot twice on the new build — once on each operator, same camera
(`0,200,0`, look `0,0.35,-1`; the two interiors from `0,150,600`), same band.

| scene | p05 Reinhard | sat Reinhard → ACES | contrast Reinhard → ACES |
|---|---|---|---|
| `Fog_Showcase` | **0.650** | 0.237 → **0.120** | 0.161 → 0.181 |
| `Clouds_Showcase` | 0.575 | 0.391 → 0.227 | 0.191 → 0.244 |
| `Sky_PhysicalShowcase` | 0.571 | 0.495 → 0.348 | 0.146 → 0.202 |
| `Desert_Sandbox`, `Starter`, `Terrain_Grass`, `DepthPrecisionProbe` | 0.543 | 0.359 → 0.306 | 0.091 → 0.138 |
| `Clouds_Sunset` | 0.492 | 0.381 → 0.334 | 0.154 → 0.233 |
| `Clouds_Demo` | 0.369 | 0.319 → 0.386 | 0.300 → 0.442 |
| `MainMenu` (UI, no sky) | 0.362 | 0.043 → 0.047 | 0.301 → 0.445 |
| `CornellDemo` (interior, no sky) | 0.148 | 0.336 → 0.370 | 0.501 → 0.646 |

`Fog_Showcase` is the only scene above the 0.6 threshold — but the threshold is a proxy, so the frames
were looked at as well, and two of them are worth writing down because the numbers alone would have
misled:

* **`Clouds_Showcase` loses more saturation than `Fog_Showcase` (−0.164) and is an IMPROVEMENT.** Under
  Reinhard the cloud and the sky behind it sat at nearly the same value and the whole frame was one blue
  wash; under ACES the clouds come out white and separate from the sky. The saturation that left the
  measurement is the blue that left the CLOUDS, which is where it did not belong. Contrast rises.
* **`Sky_PhysicalShowcase` (−0.147) keeps its gradient** — paler toward the horizon, still plainly a
  blue sky, not a wash.

The lesson for the next survey: a saturation drop is not by itself a regression. It is a regression when
the drop is the SUBJECT losing its colour, and that distinction needs the frame, not the number.

## What the measurements found

### 1. The shadow ray was thirty times too short

`LightMarchDistance` was 500 m against Unreal's `ShadowTracingDistance` of 15 km. A shadow ray started
inside a two-kilometre cloud and only 500 m long never leaves it, so every sample in the body sees roughly
the same optical depth and the body is shaded uniformly. That is why the clouds were flat.

Fixed: 15 km, six samples. The samples are on a squared distribution, so this is not thirty times the
cost — the first few still land in the metres nearest the sample.

> **That last sentence is wrong, and it is the whole of task OE.** On a squared distribution over a
> march of length `M` with `N` samples the FIRST segment is `M / N²` long. At 500 m and six samples it
> was 13.9 m. At 15 km and six samples it is **417 m** — the lengthening of the ray coarsened its near
> field by exactly the factor it lengthened the ray, thirty times, and the sample count was not moved
> with it. Measurements in "OE" below.

### 2. The clouds were DARKER than the sky they sat on

The decisive measurement was a frame with the clouds switched OFF: the clear sky alone measured mean 0.740,
p05 0.646, p95 **0.869** — brighter at every point than the sky WITH clouds (p95 0.783). A sunlit cloud
was coming out darker than the blue behind it, which is backwards and which no amount of cloud tuning
could fix.

**The cause is that this engine describes one sun with two numbers.** `SkyAtmosphereData::SunIntensity`
says how bright the sky's own sun is — 22 in these scenes. `DirectionalLightData::Intensity` says how
brightly the sun lights things — 1. `AtmosphereEnv` documents the split deliberately ("they differ by more
than an order of magnitude and that is the point, not a defect"), and the height fog already navigates it.

The clouds are lit, correctly, by what actually falls on things. So the sky was being drawn with a sun
twenty-two times brighter than the one lighting the clouds in front of it.

**This was NOT fixed in code**, and deliberately. Lighting the clouds with the sky's sun was tried and
measured: p95 0.966, everything clipped to white. The inconsistency is in the SCENE, not the renderer, and
the fix is to make one sun out of two — the demo scene now carries a directional light of 22 with an
exposure of 0.22, so the sun that lights the clouds is the sun that draws the sky.

Worth knowing for every future scene: **a scene whose sky SunIntensity and light Intensity disagree will
have clouds that cannot be brighter than the sky.** That is a scene-authoring trap the engine currently
does nothing to catch.

### 3. Coverage was near-overcast where the reference is half-clear

At the settings that filled the frame, 92% of the sky was touched by cloud. The reference is about half
open, and the deep blue between clouds is where its `p05` of 0.316 comes from. Ours could not go dark
because there was nothing dark left in the frame.

### 4. Radial moire at the horizon — the far field repeating

Raising `MaxViewDistance` to 150 km to fill the horizon band made the weather tile repeat about twenty
times toward the vanishing point, and the repetition shows as streaks radiating from it. Pulled back to
60 km with a 12 km tile; the aerial-perspective ramp keeps the band without needing the extra distance.

## Where it stands

Contrast 0.420 at the horizon and 0.483 at the zenith against the reference's 0.482, with mean, median
and saturation all in the same family. The two frames are still not the same sun angle or the same
camera elevation, so an exact match is not the target and never was.

*(The paragraph this replaced read "contrast 0.333 against 0.482, the remaining gap is in the dark end".
That gap was the tonemapper, and T-ACES closed it without touching a single line of cloud code — see the
T-ACES section above. It is left recorded because being wrong about WHERE a discrepancy lives is the
expensive mistake this document exists to prevent, and this is the second time it happened.)*

## OE: looking INTO the sun, 2026-08-20 — the shadow ray, and why ACES hid how big it was

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, 1280x766, band `0 0 1280 552`. The sun of this
scene is at elevation 51 degrees and azimuth `+Z`, so `--look 0,Y,1` is INTO it and `--look 0,Y,-1` is
away from it. Two independent runs of the same command produced **0 differing bytes** of the PNG, so
every difference below is the change and nothing else.

| frame | mean | p05 | p50 | p95 | contrast | sat |
|---|---|---|---|---|---|---|
| **UE reference** `UE_mid.png 377 169 2035 991` | 0.609 | 0.321 | 0.650 | **0.800** | 0.479 | 0.192 |
| zenith `0,0.9,1` — INTO the sun | 0.740 | 0.499 | 0.722 | **0.996** | 0.497 | **0.022** |
| zenith `0,0.9,-1` — away | 0.516 | 0.284 | 0.499 | 0.767 | 0.483 | 0.121 |
| mid `0,0.45,1` | 0.578 | 0.456 | 0.547 | 0.841 | 0.385 | 0.056 |
| mid `0,0.45,-1` | 0.568 | 0.295 | 0.574 | 0.786 | 0.491 | 0.126 |
| horizon `0,0.12,1` | 0.674 | 0.488 | 0.672 | 0.855 | 0.367 | 0.047 |
| horizon `0,0.12,-1` | 0.631 | 0.467 | 0.632 | 0.775 | 0.307 | 0.064 |

Frames: `Shots/OE_base_{zenith,mid,horizon}_{sun,away}.png`. **Only the sunward half of the sky is
broken**, and it is worst where the elevation is highest — the away column is within a few hundredths
of the reference at every elevation.

### The second scale this document needed: what a display number means in radiance

Every figure above is 8-bit output, and above ~0.95 that scale is nearly flat: the shipped ACES fit
plus gamma 2.2, evaluated on its own constants, maps

| linear luminance after exposure | 0.42 | 0.84 | **0.97** | 1.0 | 2.5 | 4.0 | 10.0 | **16.8** | 24 |
|---|---|---|---|---|---|---|---|---|---|
| displayed | 0.594 | 0.767 | **0.798** | 0.804 | 0.926 | 0.958 | 0.988 | **0.996** | 0.9995 |

So the reference's `p95 = 0.798` IS "linear 0.97 after exposure", and our `0.996` IS "linear 16.8" —
**17 times the reference, 4.1 stops**, not the 25% the display numbers suggest. This is why the earlier
finding that removing the lens flare moves `p95` by 0.008 was misleading rather than wrong: it was
measured at a point where the curve compresses a factor of three into a third of a display level.
Everything below is therefore reported in LINEAR scene radiance, read out of an instrumented composite
that writes `clamp((log2(L) + 10) / 20, 0, 1)` in place of the tonemap, so ImageStat's percentiles
decode straight back to radiance.

### The knock-out table — linear scene radiance, before exposure, zenith, INTO the sun

| variant | p05 | p50 | **p95** |
|---|---|---|---|
| **shipped, INTO the sun** | 1.283 | 1.866 | **20.53** |
| **shipped, away** | 0.536 | 1.357 | **2.87** |
| *target implied by the reference's 0.798* | | | *4.4* |
| clouds off (`VolumetricCloud.Enabled=false`) | 0.633 | 0.824 | 1.15 |
| directional light `Intensity = 0` | 0.633 | 0.737 | 0.82 |
| phase forced isotropic (`PhaseG=0, PhaseGBackward=0, MsEccentricity=0`) | 1.149 | 1.580 | 3.20 |
| `PhaseBlend 0.575 -> 1.0` (second lobe only) | 1.214 | 1.670 | 4.00 |
| multi-scatter octaves 3 -> 1 | 0.669 | 0.824 | 16.45 |
| cloud ambient scale -> 0 | 0.566 | 1.087 | 19.43 |
| scattering albedo 0.98 -> 0.5 | 0.669 | 0.920 | 10.13 |
| extinction scale 8 -> 16 | 1.214 | 1.580 | 22.63 |
| **light march 6 -> 16 samples** | 1.283 | 1.495 | **6.15** |
| light march 16 samples spread over 50 km instead of 15 | 1.283 | 1.580 | 10.13 |
| **light march 64 samples (converged)** | 1.283 | 1.495 | **4.72** |
| light march 64 samples, AWAY from the sun | 0.536 | 1.414 | 2.46 |
| cloud aerial perspective off | *bit-identical to the shipped frame in all six figures* |

Read it in one line: **converging the sun-visibility shadow ray takes the sunward highlight from 20.53
to 4.72 — a factor of 4.35 — and lands within 7% of the 4.4 the reference implies. The same change
away from the sun is 2.87 to 2.46, a factor of 1.17.** The error is 3.7 times more damaging into the
sun than away from it, and nothing else in the table has that shape.

### The instrument that names it: the optical depth itself, rendered

The march's `opticalDepth` toward the sun at each pixel's FIRST cloud sample, written out in place of
the cloud's radiance (`Shots/OE_instr_opticaldepth_n{6,64}_zenith_sun.png` — the six-sample frame has
large BLACK regions along every cloud edge where the layer reports itself transparent to the sun; at 64
they are gone):

| shadow samples | first segment | tau p05 | tau p50 | tau p95 | sun let through at p05, `exp(-tau)` |
|---|---|---|---|---|---|
| **6 — shipped** | **417 m** | **0.384** | 3.39 | 13.18 | **0.681** |
| 10 — UE's `BaseShadowRaySampleCount` | 150 m | 3.387 | 10.13 | 18.38 | 0.034 |
| 16 — our clamp ceiling | 58.6 m | 3.784 | 7.67 | 16.45 | 0.023 |
| 32 | 14.6 m | 3.580 | 8.11 | 14.72 | 0.028 |
| 64 | 3.7 m | 3.784 | 8.11 | 13.93 | 0.023 |

**At six samples the fifth percentile of the optical depth toward the sun is 0.384 where the converged
answer is 3.784 — the shipped shadow ray lets THIRTY TIMES too much sunlight into the pixels that end
up brightest**, and its median reports 3.39 against 8.11, 42% of the material that is there. Everything
from ten samples up is on the converged plateau; six is the only value below the knee, and 32 is where
the first segment returns to the 13.9 m it had before the ray was lengthened.

Why it shows up only into the sun: the error is a multiplier on `sunVisibility`, and `sunVisibility` is
multiplied by the phase function. At the bright pixels of the sunward frame the dual lobe is ~0.51 sr^-1
against ~0.031 sr^-1 at the same pixels of the away frame — a factor of 16. An error the away azimuth
divides by 16 the sunward azimuth does not.

### Two mechanisms, and the second is invisible until the first is fixed

Displayed `p95`, zenith, INTO the sun:

| shadow samples | bloom + lens flare | p95 |
|---|---|---|
| 6 (shipped) | on | **0.996** |
| 6 | off | 0.961 |
| 64 | on | 0.926 |
| **64** | **off** | **0.806** |
| *UE reference* | | *0.798* |

Away, for the same rows: 0.767 / — / 0.676 / 0.664.

Bloom and the lens flare are worth **0.035** on the shipped frame and **0.120** once the shadow ray is
converged, because on the shipped frame they are being added on top of a value ACES has already
saturated. Their own azimuth asymmetry is large — they add ~6.7 units of linear radiance at the
near-sun p95 pixels against ~0.93 away — and their cause is a relation of the kind section 2.3.1 of the
contract lists: **`BloomThreshold` is compared against PRE-exposure radiance** (`BloomDownsample.shader`
takes `max(brightness - u_Threshold, 0)` on the raw HDR) while the scene's `Exposure` is 0.22, so a
threshold authored as 2.5 is 0.55 in exposure-normalised units and ordinary daylight sky is a bloom
source. It is left as measured and NOT tuned: raising the number in one scene would hide a units
disagreement that belongs to the bloom pass.

### What was disproved

* **The sun of 22 at an exposure of 0.22 is not itself the overexposure.** No single exposure can fix
  both azimuths: bringing the shipped sunward p95 to 0.798 needs 0.047, which puts the away frame at a
  displayed 0.27. And with the shadow ray converged the SAME 0.22 lands the sunward frame on 0.806
  against the reference's 0.798. The exposure was right; the radiance was wrong.
* **The sun disc and its glow are not it.** Clouds off, zenith, into the sun: displayed p95 0.594,
  linear 1.15, saturation 0.384 — a correctly exposed blue sky with the disc and its ghosts in frame
  (`Shots/OE_ko_cloudsoff_zenith_sun.png`).
* **`PhaseG` 0.8 is not a defect and turning it down is the wrong repair.** The implementation is
  equivalent to `VolumetricCloud.usf:327-335` including the sign convention (UE evaluates HG with
  `+2g·cos` and passes `-cosTheta`; we use `-2g·cos` and `+cosTheta`), and the constants 0.8 / 0.1667 /
  0.575 are UE's shipped instance. Forcing it isotropic gives 3.20 — BELOW the 4.4 the reference
  implies, so a frame tuned that way would be dark for a second wrong reason
  (`Shots/OE_ko_phase_isotropic_zenith_sun.png`).
* **Aerial perspective contributes exactly nothing here.** `AerialPerspectiveStartDistance` is 30 km and
  every cloud in frame is nearer, so `aerialAmount` is 0; switching it off reproduces the shipped frame
  in all six figures.
* **The ACES shoulder is where it should be.** Its own constants put linear 1.0 at 0.804 and the
  reference's 0.798 at 0.97. What it did do is hide the SIZE of the defect (see the second scale above).
* **Ambient and the multi-scatter octaves are secondary**: -5% and -20% of the sunward p95 against the
  shadow ray's -77%.
* **The defect is cloud-specific.** `Sky_PhysicalShowcase`, no cloud, sun in frame, same cameras:
  zenith into the sun `p95 0.888 / sat 0.296`, zenith away `0.583 / 0.495`, mid `0.785 / 0.733`,
  horizon `0.941 / 0.930`. Bright toward the sun, as it should be, and it keeps its colour — against
  the cloud scene's `0.996 / sat 0.022` (`Shots/OE_skyshowcase_zenith_sun.png`).

### What the repair costs, and the number that must move with it

`LightMarchSamples` defaults to 6 in `Engine/ECS/VolumetricCloudComponent.hpp` and is clamped to 1..16
in `CloudPayload.hpp` and again in `CloudRaymarch.shader`; the quadrature is
`CloudRaymarch.shader::CloudLightOpticalDepth`. UE's own numbers are 10 by default
(`VolumetricCloudComponent.h:236`) with a ceiling of 80
(`r.VolumetricCloud.Shadow.ViewRaySampleMaxCount`) — ours is six with a ceiling of sixteen.

**Whoever raises it must re-expose the demo scene in the same change.** The away azimuth was calibrated
on top of the error: at 64 samples it falls from 0.767 to 0.676, and the exposure that puts it back is
0.22 x (2.87 / 2.46) = 0.257. Measured at 64 samples and 0.257: into the sun 0.938, away 0.716. These
are two numbers obliged to agree and today they do not.

## The instrument

`Tools/ImageStat` — a standalone CLI over the vendored `stb_image`, linked against nothing else.

```
./build/Bin/Debug/ImageStat <png> <x0> <y0> <x1> <y1> [<png> <x0> <y0> <x1> <y1> ...]
```

Several images per invocation, because the numbers only mean something side by side: a contrast of 0.333
says nothing until the reference's 0.482 is on the line below it. Every visual claim in this programme
that turned out to be wrong was wrong in a way this would have caught in one run.
