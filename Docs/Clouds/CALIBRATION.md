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

*(The state described in this subsection is the state BEFORE the repair. It defaults to 32 with a
ceiling of 64 today, and the three ceilings are now one constant — see OE-FIX below.)*

`LightMarchSamples` defaults to 6 in `Engine/ECS/VolumetricCloudComponent.hpp` and is clamped to 1..16
in `CloudPayload.hpp` and again in `CloudRaymarch.shader`; the quadrature is
`CloudRaymarch.shader::CloudLightOpticalDepth`. UE's own numbers are 10 by default
(`VolumetricCloudComponent.h:236`) with a ceiling of 80
(`r.VolumetricCloud.Shadow.ViewRaySampleMaxCount`) — ours is six with a ceiling of sixteen.

**Whoever raises it must re-expose the demo scene in the same change.** The away azimuth was calibrated
on top of the error: at 64 samples it falls from 0.767 to 0.676, and the exposure that puts it back is
0.22 x (2.87 / 2.46) = 0.257. Measured at 64 samples and 0.257: into the sun 0.938, away 0.716. These
are two numbers obliged to agree and today they do not.

## OE-FIX: the ray converged, the scenes re-exposed, the bright pass moved into the right space, 2026-08-20

The section above found the mechanism and stopped. This one closes it. Everything below is
`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, **1280x766, band `0 0 1280 552`** — and the
LineJump rows use that band inset by two, `2 2 1278 552`. Two runs of the same command produced **0
differing bytes** of 1 240 045 on this tree as well, so every difference reported here is the change.

### The rendered convergence, which disagrees with the tau instrument above

The knock-out table above measured `opticalDepth` at each pixel's FIRST cloud sample and concluded that
"everything from ten samples up is on the converged plateau". **On the rendered frame it is not.** The
p95 pixel of the sunward zenith is not anybody's first sample, and measured through the frame — bloom
and lens flare switched off so the ACES shoulder does not flatten the scale — the plateau starts around
thirty:

| shadow samples | first segment | displayed p95 | **linear after exposure** | x converged |
|---|---|---|---|---|
| **6 — was shipped** | 417 m | 0.961 | **4.286** | 4.42x |
| 10 — UE's `BaseShadowRaySampleCount` | 150 m | 0.918 | 2.274 | 2.35x |
| 16 — the old clamp ceiling | 58.6 m | 0.849 | 1.294 | 1.34x |
| 24 | 26.0 m | 0.813 | 1.048 | 1.08x |
| **32 — now the default** | 14.6 m | 0.802 | **0.989** | 1.02x |
| 48 | 6.5 m | 0.798 | 0.969 | 1.00x |
| 64 — now the clamp ceiling | 3.7 m | 0.798 | 0.969 | 1.00x |
| *implied by the UE reference's p95 0.800* | | *0.800* | *0.979* | *1.01x* |

Read it in one line: **the converged sunward zenith lands on the UE reference's own p95 to one per
cent, and sixteen — the ceiling the component used to carry — could not have got there, because it is
still 34% too bright.** Ten, which is Unreal's default, is 2.35x on OUR quadrature; that is not a
contradiction of Unreal, it is what the same count buys on a different sample placement, and it is why
the number here was measured rather than copied.

The AWAY azimuth converges by ten (0.657 against 0.653) and is what hid the whole defect: the error is
a multiplier on `sunVisibility`, and the sunward dual lobe is ~16x the away one at the same pixels.

The display scale is why this needed the linear column. Between six samples and thirty-two the frame
moves 0.961 -> 0.802, which reads as 16% — in radiance it is 4.29 -> 0.99, a factor of **4.3**.

### What it costs, measured, on a machine that was shared

Frame-count slope, `(t900 - t300) / 600`, `--look 0,0.45,1` (cloud across the whole frame), **four
passes with all four sample counts interleaved in one session, minimum of four, never the mean**:

| samples | slope | vs 6 | delta |
|---|---|---|---|
| 6 | 6.95 ms/frame | 1.00x | — |
| 16 | 9.20 ms/frame | 1.32x | +2.25 ms |
| **32** | **12.98 ms/frame** | **1.87x** | **+6.03 ms** |
| 64 | 20.28 ms/frame | 2.92x | +13.33 ms |

**The spread: 0.3–0.8% run to run** on all eight (count, frame-count) pairs across the four passes, and
taking the slope from the maxima instead of the minima moves it 1.5%. The machine is shared with other
agents and one earlier programme's series drifted 40% with no code change; this window happened to be
quiet, and the interleaving is what makes that checkable rather than hoped for.

The cost is **linear in the sample count at 0.230 ms per sample** (0.225 / 0.232 / 0.230 measured over
the three intervals), which is the shape to expect: the shadow ray is traced once at every view sample
that finds material, so nothing amortises.

**This is a debug build, and the ratio is a lower bound on the shipping one** — the 6.95 ms baseline
carries CPU work that a release build shrinks and the cloud dispatch does not, so the cloud pass is a
LARGER share of a release frame than of this one.

**Read as the statement of the quality-tier task, which does not exist yet:** the table above is a price
list. `LightMarchSamples` is the first knob a tier should reach for, it is linear, and the quality it
buys is known to the per cent — 16 saves 3.8 ms and costs 34% of highlight radiance, 24 saves 1.9 ms and
costs 8%. Nothing was hidden behind a tier here because there is no tier to hide it behind; the default
is the value that makes the frame correct.

### The bloom threshold: fixed, not tuned, and one authored number re-authored because of it

`BloomDownsample.shader` and `LensFlareBrightPass.shader` both threshold the RAW HDR image, while
`SceneComposite` applies `Exposure` afterwards to the sum of scene and both effects. So an authored
threshold of 2.5 meant 2.5 in a scene at `Exposure 1.0` and **0.55** in one at 0.22 — one knob with two
meanings, decided by an unrelated field, which is §2.3.1's disagreement exactly. At 0.55 of a normalised
unit ordinary daylight sky is a bloom source, and that is what the previous section measured as worth
0.035 of displayed p95 on the shipped frame and **0.120** once the shadow ray converged.

Fixed in `SceneRenderer.cpp` by dividing both thresholds by the scene's exposure, so the comparison
happens in the space the number was written in. **Neither authored number was retuned to make a frame
look right** — but the fix then showed that 2.5 in the corrected space blooms *nothing at all* in
`Clouds_Demo`, and a knob that moves nothing is what §2.3.1 forbids in the other direction. Measured, at
the converged ray, sunward zenith:

| `BloomThreshold`, normalised | displayed p95, sunward | displayed p95, away |
|---|---|---|
| 2.5 (as authored) — bloom entirely inert | 0.802 | 0.653 |
| 2.0 | 0.805 | 0.653 |
| 1.5 | 0.809 | 0.653 |
| **1.0 — now the authored value** | **0.837** | **0.653** |

**One is the principled value, not a tuned one:** in exposure-normalised units 1.0 is where ACES puts
0.804 on the display, so "above one unit" is "brighter than white", which is what a bright pass is for.
It restores the effect at *the same strength it used to have* — 0.035 of displayed p95, the number the
previous section measured on the shipped frame — while removing the reason it was firing: the away
column does not move at all, because ordinary blue sky no longer reaches the cutoff.

**Auto-exposure is NOT fixed by this and is not pretended to be.** There the exposure is
`key / adaptedLuminance`, evaluated in the composite from a 1x1 image the CPU never reads, so
`SceneRenderer` has nothing to divide by and leaves the threshold in raw radiance. No repository scene
enables auto-exposure. Closing it means giving the bloom pass the adapted-luminance image and doing the
comparison on the GPU — one new descriptor on `BloomDownsample`, which `ShaderCacheKey` pins — and it is
written here rather than left to be rediscovered.

### The exposure, checked independently and agreeing with the previous measurement

The section above derived 0.22 x (2.87 / 2.46) = **0.2567** from an instrumented linear composite on the
old tree. Measured again here by a different route — displayed p95 of the away zenith with bloom and
flare off, inverted through the shipped ACES fit evaluated on its own constants — the shadow ray takes
the away axis from linear 0.628 to 0.525, a ratio of 1.1963, giving **0.2632**. The two disagree by
2.5%, which is smaller than either claims, so the scenes ship at **0.26** between them.

`Clouds_Demo` and `Clouds_TwoSpecies` are the two scenes that carry it. **Which scenes needed it was
measured, not assumed:** every other repository scene was shot before and after, and
`Sky_PhysicalShowcase`, `Fog_Showcase` and `CornellDemo` come back **byte-identical** — the threshold
change is exactly inert at `Exposure 1.0` and the ray change cannot touch a scene with no cloud in it.
`Clouds_Showcase` and `Clouds_Sunset` move by 0.001–0.002 of mean and were left alone: their
directional light is 1 against a sky sun of 22, so they are still in the two-suns state section 2 above
describes, and re-exposing them is scene authoring with no reference behind it.

### The six points, before and after

`LightMarchSamples` 6 -> 32, `Exposure` 0.22 -> 0.26, `BloomThreshold` 2.5 -> 1.0, all in one change.

| frame | mean | p05 | p50 | p95 | contrast | sat |
|---|---|---|---|---|---|---|
| **UE reference** `UE_mid.png 377 169 2035 991` | 0.609 | 0.321 | 0.650 | 0.800 | 0.479 | 0.192 |
| zenith `0,0.9,1` INTO the sun — before | 0.731 | 0.483 | 0.713 | **0.996** | 0.513 | **0.025** |
| zenith into the sun — **after** | 0.601 | 0.503 | 0.550 | **0.887** | 0.384 | 0.050 |
| zenith `0,0.9,-1` away — before | 0.503 | 0.281 | 0.490 | 0.747 | 0.467 | 0.128 |
| zenith away — **after** | 0.528 | 0.297 | 0.534 | 0.696 | 0.400 | 0.128 |
| mid `0,0.45,1` — before | 0.569 | 0.444 | 0.538 | 0.837 | 0.393 | 0.061 |
| mid into the sun — **after** | 0.569 | 0.483 | 0.543 | 0.747 | 0.264 | 0.070 |
| mid `0,0.45,-1` — before | 0.558 | 0.293 | 0.562 | 0.770 | 0.477 | 0.130 |
| mid away — **after** | 0.556 | 0.313 | 0.574 | 0.723 | 0.410 | 0.136 |
| horizon `0,0.12,1` — before | 0.672 | 0.483 | 0.672 | 0.852 | 0.369 | 0.047 |
| horizon into the sun — **after** | 0.637 | 0.506 | 0.633 | 0.778 | 0.272 | 0.062 |
| horizon `0,0.12,-1` — before | 0.629 | 0.463 | 0.629 | 0.775 | 0.311 | 0.064 |
| horizon away — **after** | 0.621 | 0.506 | 0.621 | 0.739 | 0.233 | 0.072 |

In linear scene radiance the sunward zenith p95 goes **16.81 -> 1.70, a factor of ten**, while the three
away frames move only x0.81–0.85 — the asymmetry the defect had, removed along the axis it lived on.

**And the frames say what the numbers cannot.** Before, the sunward zenith is a white wash across the
whole upper half with no cloud structure left in it; after, the clouds have lit tops and shaded
undersides, the sun is a small hard disc with a tight glow instead of a smear that ate a third of the
screen, and blue sky is visible between the clouds again. The mid frame gains the same blue back
(saturation 0.061 -> 0.070 sunward) because the veil over it was bloom firing on the sky, not haze.

**One number moved the wrong way and is not hidden:** contrast on the away axis falls (zenith
0.467 -> 0.400), and the reference's is 0.479, so the BEFORE frame was closer on that one figure. It was
closer for the wrong reason — the shadow ray was inflating the cloud tops, and p95 fell further than p05
when that stopped. Chasing the contrast back would mean restoring the error under another name, which is
the mistake the T-ACES section above records this document existing to prevent.

Frames: `Shots/OEFIX_after_{zenith,mid,horizon}_{sun,away}.png`, the two that carry the visual argument
as `Shots/OEFIX_before_{zenith,mid}_sun.png`, and the no-cloud guard as
`Shots/OEFIX_guard_nocloud_sky_identical.png` — which is byte-identical to the same frame taken before
the change and is in the repository so that the claim can be re-checked rather than believed.

LineJump over `2 2 1278 552`, after: rows max 0.0016–0.0072 on the four sky frames, against the 0.006
norm and the 0.010 threshold — no banding introduced. The two horizon frames read 0.046–0.059 at row
539 both before and after; that row is the ground/sky edge, which the band's bottom includes at this
elevation, and it is geometry rather than a step in the sky.

## CS: the cloud shadow map on the world — what it costs and what proves it works, 2026-08-20

The clouds now shade the ground. The pass is an orthographic march down the sun's own direction,
`512 x 512 RGBA32F`, one texel per ray, storing `(frontDepthKm, meanExtinctionPerKm, maxOpticalDepth)` —
Unreal's encoding (`VolumetricCloud.usf:2168-2215` writes it, `VolumetricCloudCommon.ush:54-68` reads it),
transplanted rather than re-derived, with our own units and our own read-time strength. The shared text is
`Common/CloudShadowMap.glslh`; the projection is `Graphic::CloudBuildShadowMapView`.

### The price, measured, on a machine that was shared

Frame-count slope, `(t900 - t300) / 600`, `Clouds_Demo`, camera `0,200,0`, `--look 0,0.45,1` — the same
framing the shadow-ray price list above uses, so the two are directly comparable. **A and B interleaved in
one session, four passes, minimum of four**, where B is the identical binary with `CastShadows` off in the
scene — one field, no rebuild, no second set of shaders:

| | slope | vs no map | delta |
|---|---|---|---|
| **cloud pass without the map** (`CastShadows` false) | **12.92 ms/frame** | 1.00x | — |
| **cloud pass with the map, 32 base samples** | **17.84 ms/frame** | **1.38x** | **+4.92 ms** |

*(The 12.92 ms baseline is the same quantity the OE-FIX table calls 12.98 ms at 32 shadow-ray samples,
measured two hours later on the same tree. The half per cent between them is the machine.)*

**The spread: 0.6–1.2 % run to run** on three of the four (flag, frame-count) pairs; the fourth, `A300`,
carried one 5.2 % outlier in pass 2, which is why the slope is taken from the minima. Taking it from the
maxima instead gives +3.31 ms and 1.26x — the outlier is in the 300-frame leg, so it flatters the maxima
slope rather than the minima one. The machine is shared with other agents.

**A second price point, so the tier knob is a curve and not an assertion.** Two binaries differing by one
constant — `kCloudShadowBaseSamples` 32 against 16 — interleaved in one session, three passes, minimum of
three:

| base samples | effective at this sun (49°) | slope | map costs | per effective sample |
|---|---|---|---|---|
| 16 | 19.2 | 15.26 ms/frame | **+2.34 ms** | 122 µs |
| **32 (shipped)** | **38.4** | **18.01 ms/frame** | **+5.09 ms** | 133 µs |

The cost is **linear in the sample count** and quadratic in the resolution by construction — the pass
marches `resolution² × count` field samples and nothing amortises. The eleven per cent between the two
per-sample figures is the fixed per-texel work (ray reconstruction and the shell intersection), about
0.25 ms of the total.

**"Effective" is not the base count**, and the difference is twenty per cent of this pass's whole price.
Unreal's horizon factor is `clamp(0.2 / sin(elevation), 0, 1)`, which is **0.2 and not 0** with the sun
straight overhead, so the count never falls below 1.2x the base and reaches 2x below 11.5° of elevation.
`Desert/Tests/Engine/CloudShadow` asserts both ends, because a reading of the constant alone predicts 32
where the pass actually takes 38.4.

**What 16 samples costs in the picture, since it saves 2.75 ms.** On the ground frame the two are
indistinguishable to ImageStat — `mean 0.377 / p95 0.615 / contrast 0.398` against
`0.378 / 0.615 / 0.396` — and that is exactly the reading percentiles are bad at. The pixel diff says
**38.6 % of pixels changed, max delta 44/255**: the shadow EDGES move, and the whole-frame distribution
does not notice because a shadow that moves takes as many pixels dark as it gives back. Sixteen is a tier,
not a free saving, and the principled reason is beside the measurement: 16 samples over the shipped
congestus envelope is 225 m of spacing against the 125 m chord the view march can resolve, so the map's
two axes would stop agreeing about what a cloud is.

**This is a big number and it is the entry to the quality-tier decision, said as one.** The map adds
39 % to a cloud pass that already costs 12.9 ms in this build; it is the largest single addition since the
shadow ray's own 1.87x. The whole curve is above: cost `∝ resolution² × samples`, 133 µs per effective
sample at 512², so a 256² / 16-sample tier is `1/8` of it — about 0.6 ms — at 234 m per texel.

**Debug build, and the ratio is a lower bound on the shipping one** for the reason the OE-FIX table gives:
the baseline carries CPU work a release build shrinks and this dispatch does not.

### The sky did not move, and six points say so with a zero

Same six points as the azimuth protocol — three elevations x sunward and away — `Clouds_Demo`, camera
`0,200,0`, `--shot-frames 90`. The comparison is `CastShadows` on against off, one binary:

| point | changed pixels | max delta |
|---|---|---|
| zenith `0,0.9,-1` / `0,0.9,1` | **0 / 980 480** | 0 |
| mid `0,0.45,-1` / `0,0.45,1` | **0 / 980 480** | 0 |
| horizon `0,0.12,-1` / `0,0.12,1` | 29.5 % | 175 |

The four upper points are **byte for byte identical**, which is the strongest statement available that this
change touched the ground and nothing else: the deferred pass discards every texel with no geometry in it,
so the sky is drawn by a path this work never entered. The horizon points are 30 % changed because 30 % of
those frames is the checker FLOOR, and splitting the rectangle says so:

| band | changed | max delta |
|---|---|---|
| sky, rows 0–519 | 0.17 % | 37 |
| ground, rows 520–765 | 91.6 % | 176 |

The 0.17 % of sky pixels that do move are the bloom halo immediately above the horizon line picking up a
darker ground — a post-process consequence, not a sky change. `ImageStat` over the sky band reads
`mean 0.635 / p05 0.503 / p95 0.780 / contrast 0.276` before and after, identical to three decimals.

LineJump over `2 2 1278 552` on the zenith and mid frames reads `rows max 0.00160 @y 285` and
`0.00551 @y 519` — the same numbers before and after, and both under the 0.006 norm this document records.
No band was introduced.

### The snap, which no still frame can show, measured on a flyover

The map is rebuilt every frame from a world-space function, so if its projection drifts with the camera the
same piece of ground lands in a different texel every frame and the shadow **boils in place**. That is
invisible in a still frame by construction — a still frame has exactly one projection — so it is measured
on a sequence.

**The instrument.** Camera looking straight DOWN at 1.5 km, translating 600 m over 300 frames, ten shots
(`--shot-sequence`, `--shot-every 30`). The ground plane is parallel to the image plane, so consecutive
frames are an EXACT translation of one another unless the shading of a given piece of ground changes. Find
the integer shift that best aligns each frame onto the one before it and report the mean absolute luminance
difference that remains. Wind, SSAO, screen-space GI, bloom and lens flare are switched off in the probe
scene: all five are screen-locked or time-varying and would be measured as boiling.

| build | best shift | mean residual | max |
|---|---|---|---|
| **shipped (both snaps)** | +52 px, every pair | **0.545 / 255** | 0.569 |
| both snaps knocked out | +52 px, every pair | **2.291 / 255** | 2.556 |

**4.2x the floor**, and the floor is what it should be: 0.545 is the sub-pixel resampling residual of a
high-contrast checkerboard shifted by a non-integer number of pixels, which no snap can remove. The
knock-out build differs from the shipped one by two functions returning their argument.

Frames: `Shots/CS_flyover_snap_on_f150.png` and `Shots/CS_flyover_snap_off_f150.png`.

### The sweep

After `rm -rf build/Tests/Intermediates/Debug` **and** with every binary deleted before its own build:

**62 test makefiles, 62 binaries built, 62 binaries run, zero failures.**

The count was 61 before this phase and is 62 because `CloudProceduralField` is new. The audit of the skip
list — walking every binary in `build/Bin/Tests/Debug` and asking whether the list would have hidden it —
comes back empty: nothing in the list is a test. The list has hidden real suites once before
(`DShaderParser`, `FontBaker`, `MeshSimplifier`, 35 tests that never executed for a whole programme), which
is why the audit is run rather than the list trusted.

### The frames

| file | what it shows |
|---|---|
| `Shots/CS_before_ground_plain.png` | 40 km of ground under a cumulus deck, uniformly lit — the state this task found |
| `Shots/CS_after_ground_plain.png` | the same frame with cloud shadows crawling across it |
| `Shots/CS_after_ground_steep.png` | the same, looking further down, shadows over the whole plain |
| `Shots/CS_after_ground_plain_16samples.png` | the tier point: 2.75 ms cheaper, 38.6 % of pixels moved |
| `Shots/CS_after_sky_{zenith,mid,horizon}_{sun,away}.png` | the six points; the four upper ones are byte-identical to their "before" |

The scene is `Clouds_ShadowsOnGround.desce`, authored for this: `Clouds_Demo`'s checker floor scaled to
40 km so that several cloud cells fit in one frame. `Clouds_Demo`'s own floor is about 4 km across, which
is one cloud shadow and cannot show the effect at all.

## QT: the quality tiers — what each one costs, what each one gives up, and the four knobs that were refused, 2026-08-20

The two tasks before this one each doubled a price and each said, in the same words, that its price list
was the ENTRY to a tier decision rather than a tier: the shadow ray at 0.230 ms per sample (OE-FIX above)
and the shadow map on the world at 133 µs per effective sample (CS above). This section spends those two
price lists — and records that one of the two things they proposed spending them on **cannot be built**.

**Everything below is `Clouds_Demo` at 1280x766, camera `0,200,0`, debug build, on a machine shared with
other agents.** Slopes are `(t900 - t300) / 600` with every configuration interleaved in ONE session and
the minimum taken over the passes. The ImageStat band is `0 0 1280 552` — the OE-FIX band — and pixel
diffs are over the whole 1280x766 frame. The noise floor of the headless `--shot` path was
**re-measured, not quoted**: the same command run twice at the reference tier gives **0 differing pixels
of 980 480**, so every diff below is the change and nothing else.

*(Sanity check, because it is cheap and it was worth having: all six azimuth/elevation points at the
reference tier reproduce the OE-FIX table above to three decimals — sunward zenith p95 0.887, away 0.696,
mid 0.747 / 0.723, horizon 0.778 / 0.739 — and LineJump reads `0.00160 @y 285`, the same figure the CS
section recorded. This tree is the tree those numbers were taken on.)*

### The finding that decided the shape of the tier: three of the brief's six candidates cannot move

The brief listed six candidate knobs. Three are pinned, and the pinning is ONE relation seen from three
sides. Each link was measured rather than assumed:

1. **The march's SEARCH step is `32 km / MaxSteps`** — `CloudFinestResolvableChordKm` in
   `Common/CloudGeometry.glslh` — which is **125 m** at the component's 256.
2. **The shipped library clears that by 1.10x and no more.** Over all nine `.decloudtype` files,
   Altocumulus and Cirrus both place a median chord of **137 m**. `Desert/Tests/Engine/CloudType` now
   computes and prints the bound rather than leaving it to be rediscovered: **the library tolerates Max
   Steps down to 233**, against the component's 256 — a margin of **9 %**.
3. **So Max Steps cannot be a tier knob.** Not "should not": at 192 four of the nine shipped types fall
   past Nyquist, at 128 five do, the worst (Altocumulus, Cirrus) at **0.55x**. Those types do not get
   softer — they dither in and out with the ray's jitter, which is the definition of speckle. The knob
   that cannot be used is worth **4.48 ms**, the second-largest saving in the subsystem. The frame agrees
   with the arithmetic: Max Steps 128 moves **97.5 % of pixels at max 62/255**, a larger amplitude than
   any knob that IS in the tier.
4. **So the shadow map's texel cannot grow either**, because the map is required to resolve that same
   125 m chord. 256² over the shipped 30 km radius is a **234 m** texel — 1.9x past it — and the only way
   to legalise 234 m is to raise the chord by lowering Max Steps, which step 3 forbids.
   **The `256² / 16-sample` tier the CS section sketched at "⅛, about 0.6 ms" is therefore not available
   at all.** This is the one place the brief's own arithmetic and the repository's own tests disagree, and
   the tests are right.
5. **And the map's sample count cannot fall**, for the third face of it: 3.6 km of shipped congestus over
   32 samples is 112 m against the same chord, so the floor is **28.8 samples**. Sixteen is 225 m. The CS
   section said exactly this beside its own price; it is confirmed here and is now asserted per tier.

**What the map's cost is bought back with instead: its FOOTPRINT.** Resolution, extent and snap scale by
ONE number together, which leaves the texel at **117.2 m on every tier** — the relation does not move at
all — and moves only the radius of world around the camera that receives cloud shadow. That is a limit the
map already had and already stated; the tier moves it rather than inventing it.

### The knob price list, measured

Frame-count slope, `--look 0,0.45,1` (cloud across the whole frame), eight configurations interleaved in
one session, three passes, **minimum of three**. Every row differs from the first by ONE authored field,
so none of them needed a rebuild.

| configuration | slope | vs default | what it does to the frame | verdict |
|---|---|---|---|---|
| **default** — 32 shadow samples, 3 octaves, 256 steps, stop 0.005, map on | **18.04 ms** | — | — | — |
| shadow ray 24 samples | 15.92 ms | −2.12 ms | 8 % of converged highlight radiance | in the tier |
| shadow ray 16 samples | 14.36 ms | **−3.68 ms** | 34 % of it; sunward zenith p95 0.887 → 0.927 | **in Low** |
| shadow ray 8 samples | 12.47 ms | −5.56 ms | back past the defect OE-FIX removed | refused |
| multiple-scattering octaves 3 → 1 | 17.72 ms | **−0.32 ms** | grey clouds | **refused** |
| Max Steps 256 → 128 | 13.56 ms | −4.48 ms | 97.5 % of pixels, **max 62/255** | **refused — breaks the library** |
| Stop Transmittance 0.005 → 0.05 | 15.21 ms | **−2.83 ms** | 90 % of pixels, **max 10/255** | **in Low** |
| cloud shadow map off entirely | 12.97 ms | −5.07 ms | no shadow on the world at all | the map's whole price |

**The spread: 0.3–1.3 % run to run on the four shadow-ray rows and on `stop05`, and 5–16 % on `mso1`,
`steps128` and `noshadow`** — the wide rows were measured while a compile of my own was running on the
same machine. That is exactly the contamination the minimum-of-N exists for, and it is named here rather
than hidden. The shadow ray is **linear at 0.232 ms per sample** over the three intervals
(0.230 / 0.236 / 0.236), reproducing OE-FIX's 0.230 to under one per cent on a different day.

**Two refusals worth recording, because both are cheap to re-derive wrongly:**

- **The multiple-scattering octaves are already free.** Three orders cost 0.32 ms more than one — under
  two per cent, inside that row's own spread — while turning the clouds grey. The reason is structural
  and is written above the loop in `CloudRaymarch.shader`: the octaves **re-use ONE shadow march**, so
  the expensive part is paid once however many orders are summed. A knob that changes the picture and not
  the cost is the wrong half of a tier.
- **Shortening the shadow ray's DISTANCE saves nothing at all.** The cost is the sample COUNT; the
  distance only decides how far apart those samples are. It is a way to make the clouds flat for free,
  not fast.

### The tiers

`Core::CloudQuality` on `SceneSettings`, three enumerators, and each step down the ladder adds **exactly
one** new degradation. The numbers come from `Graphic::CloudQualityScale`, a pure function of the
enumerator, which is what `Desert/Tests/Engine/CloudShadow` drives to assert the relations **tier by
tier** instead of on the constants that happen to be compiled in.

| tier | shadow ray | shadow map | march stops at | slope | vs High |
|---|---|---|---|---|---|
| **High** (default) | authored, 32 | 512² over 30 km, snap 20 km | 0.005 | **17.99 ms** | 1.00x |
| **Medium** | authored, 32 | **256² over 15 km, snap 10 km** | 0.005 | **14.24 ms** | **0.79x** |
| **Low** | **capped at 16** | 256² over 15 km, snap 10 km | **0.05** | **8.61 ms** | **0.48x** |

Four passes interleaved in one session, minimum of four; the spread was 0.3–2.7 % on five of the six
(tier, frame-count) pairs and 7.6 % on `high@300`, which is why the slope is taken from the minima. The
17.99 ms reference agrees with two other sessions on this tree (18.04 and 18.14), so **the tier costs
nothing at High** — which is the first thing a tier has to prove.

**The tier composes with the artist's numbers, it does not replace them** — `min()` on the sample count,
`max()` on the stop threshold, both pointing at "cheaper". A scene authored at 16 samples renders at 16 on
every tier, because asking for the cheap answer is an authored intention and not a budget a tier is
entitled to overrule. `Desert/Tests/Engine/ComponentReflection` asserts both directions.

### Tier x cost x what exactly got worse

The third column is the point of the table, and it is filled with frames and numbers rather than words.

**High → Medium, −3.76 ms (21 %).**

| | measured |
|---|---|
| the sky | **Does not change.** Zenith and mid, sunward and away: **0 of 980 480 pixels**. The two horizon points move 553 and 547 pixels — 0.06 %, **max 1/255** — and that is the sliver of `Clouds_Demo`'s own 4 km checker floor at the bottom of the frame, not sky. The deferred pass discards texels with no geometry, so the map is a ground-only quantity, and this is the strongest available statement that the tier touched only what it claims. |
| the sky's banding | LineJump on all four sky frames reads **exactly the same numbers as High**, to five decimals. |
| the ground | **Cloud shadow stops at ~15 km instead of ~30 km.** 4.22 % of the frame, **max 26/255** — and a row-band split says exactly where: rows 40–221 (the far ground) carry the whole of it at max 26, while every band below row 221 reads **max 1**, which is dither. The change is where the theory puts it and nowhere else. |

**Medium → Low, −5.63 ms (a further 40 %).**

| | measured |
|---|---|
| the sunward sky | **Brightens, and only sunward.** Zenith into the sun p95 **0.887 → 0.927**, mid 0.747 → 0.755, horizon 0.778 → 0.782. The AWAY axis barely moves: zenith 0.696 → 0.699, mid 0.723 → 0.722, horizon 0.739 → 0.739. That asymmetry is the OE-FIX signature exactly — the error is a multiplier on sun visibility and the sunward lobe is ~16x the away one. Pixel diff against High: sunward zenith **98.8 %, max 66/255**; away zenith 79.6 %, **max 21**. |
| what it looks like | On the frame the lit rim of the near cloud goes from a graded silver lining to a blown streak, and the body of the deck reads flatter. This is a difference a person sees, which is the test a tier has to pass in the other direction. |
| the whole frame | The earlier stop adds an everywhere-but-tiny change of its own: 90 % of pixels at **max 10/255** when measured alone. |
| the ground | 7.40 % at max 26 — the Medium change plus the brighter unshadowed ground. |
| banding | LineJump 0.0027–0.0073 across the four sky frames, against High's 0.0016–0.0072 and the 0.010 threshold. No band introduced. |

**ImageStat cannot see the Medium step at all, and that is the expected reading rather than a failure of
the tier.** The three ground frames are identical on every percentile to three decimals
(`mean 0.412 / p95 0.681 / contrast 0.450`) while the pixel diff finds 4.22 % of pixels moved by up to
26/255. It is the same lesson the CS section recorded about 16 vs 32 map samples: a shadow that MOVES
takes as many pixels dark as it gives back, so the distribution does not notice. The percentile is the
wrong instrument for a shadow boundary; the diff and the frame are the right ones.

### The temporal stage, which no still frame can speak for

The tier does not touch the trace or reconstruction resolution — those are the checkerboard's own 2x2
invariant, and moving them is a different reconstruction rather than a tier. It does halve the shadow
map's SNAP (20 → 10 km), and the snap is the entire anti-boil mechanism, so that had to be checked rather
than assumed. Two ways:

- **Asserted.** `CloudShadowMapTiers.EveryTierKeepsBothSnapsUnderAMovingCamera` walks a camera a fifth of
  each tier's own grid and requires the projection to be bit-for-bit constant, then steps it exactly one
  grid step and requires the map to move by exactly that. It also asserts the thing that makes halving
  the snap safe: **the sub-texel lattice the second snap works on is the same size on every tier**,
  because the texel is. Re-anchoring more often is not re-anchoring less accurately.
- **Rendered.** A 1.2 km translation under the deck, six shots, at High and at Low. Low shows no
  ghosting, no smear and no disocclusion trail, and LineJump on the moving frame reads **0.00525 at Low
  against 0.00530 at High** — indistinguishable.

### Where the setting lives, said plainly

A quality tier is a property of the MACHINE, and the right home for it is a user-level scalability store
this engine does not have. It is on `SceneSettings` because every sibling cost-versus-quality choice
already is — `RenderingPath`, `GlobalIllumination`, `EnableSSAO`, `EnableSSR`, `AA`, `Anisotropy` — and
adding a second settings system for the ninth such knob is the duplication the contract forbids. When a
machine-level store arrives, this field is one of nine that move into it together, not one that has to be
un-invented first.

**No scene migration, deliberately.** The default is High, High reproduces the shipped constants to the
digit (`CloudShadowMapTiers.TheReferenceTierIsExactlyWhatWasShippedBeforeTheTierExisted`), so a scene
saved before this field existed has no key, deserializes to High, and renders the frame it always
rendered. **Rendered rather than reasoned about:** the shipped `Clouds_Demo.desce`, which carries no such
key, was shot against the explicitly-High variant at two points and comes back **0 differing pixels of
980 480** at both. Compare `Tonemapper`, which needed a migration because there the new default meant
something different from what old scenes were authored against.

### The frames

| file | what it shows |
|---|---|
| `Shots/QT_high_zenith_sun.png` / `QT_low_zenith_sun.png` | the sky difference Low makes — a graded silver lining against a blown one |
| `Shots/QT_high_mid_sun.png` / `QT_low_mid_sun.png` | the same at the elevation a player actually looks |
| `Shots/QT_high_ground.png` / `QT_med_ground.png` / `QT_low_ground.png` | 40 km of ground under the deck; the tier's cost is the far band |
| `Shots/QT_refused_maxsteps128.png` | the knob that would have saved 4.48 ms and breaks five of nine shipped types |
| `Shots/QT_knob_stop05.png` | the knob that saves 2.83 ms for max 10/255, which is why it IS in Low |
| `Shots/QT_high_flyover_f120.png` / `QT_low_flyover_f120.png` | the moving camera; the temporal stage is untouched by the tier |

## The instrument

`Tools/ImageStat` — a standalone CLI over the vendored `stb_image`, linked against nothing else.

```
./build/Bin/Debug/ImageStat <png> <x0> <y0> <x1> <y1> [<png> <x0> <y0> <x1> <y1> ...]
```

Several images per invocation, because the numbers only mean something side by side: a contrast of 0.333
says nothing until the reference's 0.482 is on the line below it. Every visual claim in this programme
that turned out to be wrong was wrong in a way this would have caught in one run.

## The second instrument — `Tools/LineJump`, for the defect ImageStat cannot see

`Tools/LineJump` — the same shape as ImageStat: a standalone CLI over the vendored `stb_image`, linked
against nothing else, in `build/Bin/Debug/`.

```
./build/Bin/Debug/LineJump <png> <x0> <y0> <x1> <y1> [<png> <x0> <y0> <x1> <y1> ...]
```

Several images per invocation, for ImageStat's reason: a jump of 0.024 is a number until 0.006 from the
repaired build is on the line below it.

### What it measures

The **largest step in mean luminance between two ADJACENT lines** inside the rectangle — separately
along both axes — the **index of the line the step falls at**, and the **mean step**, which is the noise
floor the maximum has to be read against.

```
AZ_bands_sunward_zenith.png   rows max 0.02469 @y 146   mean 0.00108   cols max 0.00280 @x 774   mean 0.00062
```

Read as: between row 146 and row 147 the mean luminance of the row moves by 0.0247, against a typical
row-to-row move of 0.0011 across the same rectangle. Twenty-three times the noise floor at one row is a
band. `@y` is the line BEFORE the step; the edge lies between it and the next.

### What ImageStat cannot see, and why it is structural

ImageStat's five figures are percentiles over the whole rectangle. **A band shifts none of them.** The
pixels on either side of a hard horizontal edge are ordinary sky pixels that were already in the
distribution — the edge is a fact about their ORDER, and a percentile has thrown the order away. The
banding frame below measures `mean 0.792 / p05 0.300 / p95 0.989` on ImageStat, which is a report about
its exposure and says nothing whatever about the stripes that are the reason the frame exists.

### Both axes, always

The lens-flare defect existed on rows AND on columns, and the column half went unseen for a stage and a
half for one reason: nobody measured it. That is why the tool always prints both and is not called
`RowJump`.

### The rectangle has to be inset, and the tool will not hide it for you

The same trap as the UE reference's editor chrome, one pixel wide instead of two hundred. Our shots
carry a two-pixel border that is not part of the picture, and it is a bigger step than any band:

| rectangle on `AZ_bands_sunward_zenith.png` | rows max | at |
|---|---|---|
| `0 0 1103 480` (no inset) | 0.04369 | **y 0** — the border |
| `1 1 1102 480` | 0.03489 | **y 1** — still the border |
| `2 2 1101 480` | **0.02469** | y 146 |
| `4 4 1099 480` | 0.02477 | y 146 |
| `8 8 1095 480` | 0.02495 | y 146 |

Stable from an inset of two pixels onward, and the row it names does not move. **Every LineJump row in
this document uses the band from `CALIBRATION.md` inset by two pixels**: `2 2 1101 480` at 1103x668 and
`2 2 1278 552` at 1280x766. A maximum sitting at `@y 0` or at the last column means the rectangle, not
the render.

### The numbers, and what counts as normal

Measured 2026-08-20 on the committed frames, `Clouds_Demo` throughout.

| frame | rows max | @y | rows mean | cols max | @x |
|---|---|---|---|---|---|
| `AZ_bands_sunward_zenith.png` — **the known banding defect** | **0.02469** | 146 | 0.00108 | 0.00280 | 774 |
| `AZ_clean_sunward_mid.png` — same azimuth, 24°, clean | 0.00508 | 315 | 0.00105 | 0.00459 | 437 |
| `AZ_clean_awaysun_zenith.png` — away from the sun, clean | 0.00752 | 267 | 0.00076 | 0.00350 | 1018 |
| `LF_before_sun_worstcase.png` | **0.02222** | 182 | 0.00130 | 0.00316 | 867 |
| `LF_after_sun_worstcase.png` | 0.00623 | 88 | 0.00096 | 0.00297 | 867 |
| `LF_before_sun_zenith42.png` | 0.01078 | 134 | 0.00072 | 0.00258 | 863 |
| `LF_after_sun_zenith42.png` | 0.00610 | 141 | 0.00089 | 0.00281 | 863 |
| `LF_before_sun_atedge.png` | **0.03456** | 435 | 0.00137 | 0.00264 | 19 |
| `LF_after_sun_atedge.png` | 0.00594 | 12 | 0.00089 | 0.00288 | 512 |

**The norm on this rectangle is a row maximum around 0.006, and the three clean frames sit at
0.005–0.008.** Above roughly 0.010 there is something to look at; the two frames that were reported as
visibly striped measure 0.022 and 0.035. The mean step is 0.0008–0.0015 everywhere, defect or not,
which is what makes the maximum readable: a band is one row twenty times the noise floor, not a frame
that is generally noisier.

`LF_before_sun_atedge` is worth a line of its own. Its ImageStat figures are unremarkable, and the
column axis says nothing (0.00264, ordinary). All of the defect is in one row, 435, at fourteen times
the mean — precisely the shape of thing the percentiles average away.

### The figures this replaces, and why they do not match

The tool was first written inside task Ц9/LF, where it reported `0.05366 → 0.02485` for the lens-flare
repair and `0.11974` on the worst frame. **Those figures cannot be reproduced, because the rectangle
they were taken over was never recorded** — the same failure this document already opens with for
ImageStat's own rows, now for the second time. The ratios survive and the ordering survives; the scale
does not, and a number without its rectangle is not a measurement. The table above states its rectangle
in the section heading above it, and the sweep three paragraphs up is the evidence that the rectangle is
the right one. Take the 0.006 / 0.010 / 0.022 scale, not the old one.

## The teamlead's own baseline at `2804b096`, and the caveat that comes with it

Shot and measured by the integrator rather than quoted from a task report, on
`Clouds_Demo`, camera `0,200,0`, 90 frames, over full width × the top 71.9%
(`0 0 1103 480` at this frame size).

| point | mean | p05 | p50 | p95 | contrast | sat |
|---|---|---|---|---|---|---|
| zenith away | 0.525 | 0.293 | 0.534 | 0.703 | 0.410 | 0.133 |
| zenith sunward | 0.598 | 0.507 | 0.550 | 0.883 | 0.376 | 0.050 |
| mid away | 0.581 | 0.327 | 0.582 | 0.723 | 0.396 | 0.088 |
| mid sunward | 0.570 | 0.487 | 0.546 | 0.735 | 0.248 | 0.064 |
| horizon away | 0.625 | 0.510 | 0.625 | 0.739 | 0.229 | 0.066 |
| horizon sunward | 0.639 | 0.503 | 0.635 | 0.782 | 0.279 | 0.060 |

**THE CAVEAT IS THE POINT.** Saturation at the zenith reads 0.133 where the same
camera measured 0.258 two phases ago, and the blue did NOT wash out — the frame
looks it, and the gaps between clouds are as deep as they ever were. What changed
is how much of the frame is cloud. `Clouds_Demo` was authored as a partly-cloudy
sky and now reads as a dense low deck, because the coverage default, the species
envelope and the per-species placement scales each moved under it across T0, T2b,
T1, T3 and SP.

So the six contrasts in this table are **not comparable with the six in the
tables above them**. They describe a different sky through the same camera. Every
cross-phase comparison this document invites has been quietly measuring
composition as well as lighting.

The instrument was never wrong. The subject moved.

---

## A0: the authored producer — slot A of the seam, 2026-08-20

Phase Э4 A0 (`Docs/Clouds/PLAN_AUTHORED_CLOUDS.md`). The cloud field's second producer exists: a sculpted
body in a `.dcmv`, placed in the sky by an entity's transform, joined to the procedural field by the `max`
the seam was written for in Э0, with a cutout that keeps the procedural field out of it.

### The memory arithmetic, recomputed rather than quoted

Measured from the engine's own allocation log on `Clouds_Demo` at 1280x766, High tier — every line below
is a number the renderer printed, not an estimate:

| | MiB |
|---|---|
| noise volume `.dcnv`, 128³ RGBA8 | 8.00 |
| cloud shadow map, 512² RGBA32F | 4.00 |
| vertical profile table, 256x64 RGBA32F | 0.25 |
| trace + reconstruction targets (two quarter-res, four half-res, RGBA16F) | 8.42 |
| **occupied before slot A** | **20.67** |
| one sculpted body, 128 x 64 x 128 RGBA8 | **4.00** |
| **occupied with one hero cloud** | **24.67** |

**Against the 64 MiB of decision D-9 that leaves 43.33 MiB, which is TEN more bodies and not eight.** The
plan's "eight" and its "~21.5 occupied" are the same quantities in MB rather than MiB (20.67 MiB is
21.68 MB); the arithmetic behind them is sound and the count of volumes that fit is 10.

**The shadow map does not grow with the tier**, which is what makes this budget stable: `ShadowMapScale`
is 1.0 at High AND at Ultra and 0.5 below, so 4.00 MiB is a ceiling rather than a High-tier figure.

**The voxel is 15.6 m on every axis** at the shipped body's authored 2 x 1 x 2 km, which is the plan's §2
exactly. Guerrilla store 8 m and the same deck says 8 m of data yields 0.5 m of visible detail, because
the detail is carried by the up-rez noise and not by the voxel — the volume carries the silhouette and
`Common/CloudField.glslh`'s erosion carries the edge.

### The procedural sky did not move, and five points of six say so with a zero

`Clouds_Demo` — no hero cloud in it at all — through the protocol's six points, the baseline binary built
from `a5cfa2ab` against this branch's binary. Camera `0,200,0`, `--shot-frames 90`, 1280x766.

**The repeat floor is zero, measured rather than assumed**: the same command run twice on the zenith-away
point gives **0 differing pixels of 980 480, max delta 0**. Every number below is therefore exact.

| point | file bytes differing | pixels changed | max delta |
|---|---|---|---|
| zenith away `0,0.9,-1` | 1 113 396 of 1 165 438 | **199 / 980 480 (0.020 %)** | **1/255** |
| mid away `0,0.45,-1` | **0** | 0 | 0 |
| horizon away `0,0.12,-1` | **0** | 0 | 0 |
| zenith sunward `0,0.9,1` | **0** | 0 | 0 |
| mid sunward `0,0.45,1` | **0** | 0 | 0 |
| horizon sunward `0,0.12,1` | **0** | 0 | 0 |

**Five of six are byte for byte identical files.** The sixth moved 199 pixels by one 8-bit level, and the
cause is named rather than shrugged at: with no hero cloud the seam reduces to `Profile * (1 - 0)` and a
loop that runs zero times, which is the exact arithmetic identity — but `Common/CloudField.glslh` is a
different FILE now, so its SPIR-V is recompiled and the compiler is free to schedule and contract the
PROCEDURAL producer's floating point differently. One 8-bit level on 0.02 % of one frame is what that
costs.

Note also that the PNG file bytes differ wholesale on that point while only 199 pixels do: a deflate
stream is not a per-pixel encoding, so a file-byte count is only meaningful when it is ZERO.

### The join is order-independent to the BYTE, and it was not

The exponential smooth-min is commutative and associative in real arithmetic — the first of the three
properties it was chosen for — but floating-point addition is neither. Measured on the shipped recipe:
**shuffling the eight lumps moved 6 bytes of 4 194 304, each by one 255th.**

Tiny, and still the wrong shape of answer: a bake is a build artefact, and a build artefact whose bytes
depend on the order its inputs happened to be listed in cannot be compared, cached by hash or asserted
equal. `GenerateCloudModellingVolume` now sorts the lumps into a canonical order before it sums anything,
which costs one sort of at most 64 elements against 8.4 million exponentials and makes the property
exactly true. `Desert/Tests/Engine/CloudAuthored` asserts it on the bytes.

### Two guards that turned out not to be guards

Every test in the new suite was verified by breaking the thing it claims to measure. Fifteen of seventeen
breaks turned the suite red. **The two that did not are the finding**, and both say the same thing:

* deleting the seam's EXACT bounds test (the one that rejects the corner of a rotated instance's hull),
  and
* clamping the fetch against the wrong axis extent (128 where the vertical axis is 64)

leave every assertion green. The reason is the same for both: the bake REFUSES a body that reaches its own
box, so the volume's outermost shell is four zeroes, and any coordinate that lands on the boundary — by a
clamp, by a REPEAT wrap, or by a bounds test that was skipped — reads nothing and contributes nothing.

So the empty-shell guarantee is what keeps the picture right, and the two gates are a COST bound: the
exact test saves a 3D fetch for every point in the difference between a rotated box and its hull, which is
up to 3.4x the body's own volume. Both are worth having; only one of them is worth calling a correctness
test, and the suite now says which is which.

### The accepting frame, and the two differences that prove what is in it

`Clouds_HeroCloud.desce`, camera `0,200,0`, `--look 0,0.45,-1`, `--shot-frames 90`. The A and B of each
row below differ by ONE FIELD of the scene and no rebuild, which is this document's own method.

| comparison | pixels changed | max delta | what it is |
|---|---|---|---|
| hero cloud on vs `Enabled` off | **17.31 %** | **126/255** | the body itself: a sixth of the frame |
| cutout on vs `Suppress Procedural Field` off | **5.90 %** | **27/255** | the procedural cloud that was growing through it |

**The instrument's floor is zero** — the same command twice gives 0 changed pixels — so both numbers are
the change and nothing else. A third check, taken by accident and worth keeping: the showcase frame shot
in one session and again in another is **0 / 980 480 different**, which is the headless path's own
determinism measured a second time.

| file | what it shows |
|---|---|
| `Shots/A0_hero_mid_away.png` | **⬛ the accepting frame**: one fused convective mass — merged lobes, a shared surface — standing in a field of the procedural producer's separate cushions |
| `Shots/A0_hero_off_mid.png` | the same scene with the hero cloud disabled; the difference IS the cloud |
| `Shots/A0_hero_nocutout_mid.png` | the cutout switched off: procedural cloud growing through the sculpted body |
| `Shots/A0_hero_{zenith,mid,horizon}_{away,sun}.png` | the six-point protocol on the showcase scene |

**Why the frame answers the measured limit of §0 of the plan.** The procedural producer's coverage field
is an Alligator, `best - second`, which is exactly zero on the bisector between every pair of feature
points — so its lobes CANNOT merge and the sky reads as a deck of separate cushions.
`Desert/Tests/Engine/CloudAuthored` measures the sculpted body as **one six-connected component** by flood
fill, and the frame shows the difference directly: the hero cloud has one silhouette where its neighbours
have many.

**The composition of the showcase scene is its own choice and is stated rather than left to be
discovered**: `Coverage` is 0.08 where `Clouds_Demo` ships 0.24. At the shipped coverage the procedural
deck fills the frame from a two-metre camera and a hero cloud is one cushion among fifty — which is §4.4
of the analysis, "P as background and A in the near field", not honoured. The showcase scene is the
composition that phase is about; `Clouds_Demo` is untouched and is what the six-point regression above is
measured on.

### The price, measured, on a machine that was shared

Frame-count slope, `(t900 - t300) / 600`, which cancels the ~20 s fixed start-up. `Clouds_HeroCloud`,
camera `0,200,0`, `--look 0,0.45,-1`. **A and B interleaved in one session, four passes, minimum of four**,
where A is the identical scene with the hero cloud's `Enabled` off — one field, no rebuild.

| | min t300 | min t900 | slope | vs no hero cloud |
|---|---|---|---|---|
| **A — hero cloud disabled** (instance count 0) | 25.629 s | 35.255 s | **16.04 ms/frame** | 1.00x |
| **B — one hero cloud, cutout on** | 26.357 s | 38.205 s | **19.75 ms/frame** | **1.23x, +3.71 ms** |

**The spread, and one leg thrown away.** The four per-pass slopes are A 14.17 / 16.24 / 16.30 and
B 19.24 / 19.85 / 19.52 / 19.43 — B holds to 3.1 %, A to 15 %, the width of A being the first pass's cold
start. A's FOURTH 300-frame leg finished in 18.943 s against 25.6 s and is excluded because it did not
finish at all: it is one more sighting of the abort described below. Read conservatively — fastest A
against fastest B — the cost is **+5.07 ms**; read as the house method reads it, minimum leg against
minimum leg, it is **+3.71 ms**. Both are quoted because the honest answer is the range: **+3.7 to
+5.1 ms for one hero cloud covering a sixth of the frame**.

For scale, the cloud shadow map cost +4.92 ms when it landed and the shadow ray's own convergence cost
1.87x. This is the same order, and it buys the phase's whole reason for existing.

**Debug build, and the ratio is a lower bound on the shipping one**, for the reason the OE-FIX table
gives: the baseline carries CPU work a release build shrinks and this dispatch does not.

### "Zero when absent" is NOT zero, and here is how much it is

§4.4 of the analysis asks that a producer which is switched off cost **zero, not almost zero**. That is
the one requirement of this phase the implementation does not meet exactly, so it is measured rather than
claimed.

`Clouds_Demo` — a scene with no hero cloud component in it at all — on the binary built from `a5cfa2ab`
against this branch's binary, **the two binaries interleaved in one session, three passes**:

| | slope, per pass | min-of-legs slope |
|---|---|---|
| before this task | 18.67 / 17.88 / 18.83 ms | **18.02 ms/frame** |
| after | 18.78 / 18.87 / 19.13 ms | **18.97 ms/frame** |
| difference | **+0.11 / +0.99 / +0.30 ms** | +0.95 ms |

**Between +0.1 and +1.0 ms on an 18 ms frame, and the run-to-run spread of the BEFORE side alone is
0.95 ms.** The difference is therefore not separable from the noise, and the best-matched pass puts it at
+0.11 ms, or 0.6 %.

**What it physically is, so nobody looks for it in the wrong place.** With no hero clouds the authored
loop does not execute — but reaching that decision still costs ONE integer load and ONE compare, per
field sample, and a frame takes on the order of 95 million field samples (a quarter-resolution trace, a
two-tier march, and about thirty shadow-ray samples for every view sample that finds material). A tenth
of a millisecond is what that arithmetic predicts and what the best pair measures.

**The PICTURE, unlike the price, is exactly unchanged**: five of the protocol's six points are byte for
byte identical files and the sixth moves 199 pixels by one 8-bit level — see the table above. A frame is
where "zero" is provable, and there it is proven.

### THE ONE THING THIS PHASE DID NOT CLOSE: a rare abort on start-up

> **CLOSED by task AB — see §AB below.** It was not a cloud defect and not this phase's: EnTT creates a
> component pool on the FIRST touch of a type, that creation writes to the registry through a `const`
> reference, and `Scene::ExecuteSystems` runs collectors on several threads at once. The section below is
> left exactly as it was written, because its attribution table is what made the mechanism findable — and
> because two of its conclusions turned out to be wrong, which is worth being able to read back.

**Say it first.** With a hero cloud in the scene, roughly one headless run in twenty dies before it writes
its PNG:

```
libc++abi: terminating due to uncaught exception of type std::length_error: vector
```

always at the same point — after the render graph has been built and before the sky's first LUT is
allocated, i.e. in the first frame or two, never later. It is not a rendering defect: a run that survives
those two frames renders correctly and deterministically, and every frame in this document was taken from
a surviving run.

**What was measured.** All rows are the same headless command differing only in the scene, and all but
the last row are the same binary. `std::length_error` counted as a death; the known teardown segfault
(which happens on EVERY run, after the PNG is written) is not.

| configuration | aborts | runs |
|---|---|---|
| `Clouds_Demo` — engine-written file, no hero component | **0** | 30 |
| `ZZ_Probe_DemoCov08` — the same file with ONE byte pair changed (coverage 0.24 → 0.08) | **0** | 30 |
| `ZZ_Probe_NoComp` — this task's scene file, hero component removed | **0** | 22 |
| `ZZ_Probe_NoVolume` — the component present, disabled, and its `Volume` slot EMPTY | **0** | 20 |
| `Clouds_HeroCloud_Off` — the component present, disabled, `Volume` set | **3** | ~26 |
| `Clouds_HeroCloud` — one hero cloud drawing | **3** | ~90 |
| the same probe scene on the binary built from `a5cfa2ab`, i.e. before this task | **0** | 30 |

*(The `ZZ_Probe_*` and `Clouds_HeroCloud_Off` scenes were instruments for one afternoon and are not in the
tree: each is `Clouds_HeroCloud.desce` with the one field named in its row changed. `ZZ_Probe_DemoCov08`
is `Clouds_Demo.desce` with two bytes changed, which is why that row can say "one byte pair".)*

**What that rules in and out.** It is not the coverage and not this task's scene file (rows 2 and 3), and
it is not the authored producer's per-frame path — row 5 has the instance count at ZERO and still dies. It
did not happen on the pre-change binary. The signal is with the scenes whose component carries a `.dcmv`
handle; aggregated, 6 aborts in ~116 such runs against 0 in ~102 without, which is Fisher p ≈ 0.03 —
suggestive, and NOT a proof, and it is quoted as what it is.

**What was tried and did not work.** ~70 runs under `lldb` never reproduced it, so there is no stack. The
macOS crash reporter wrote no report for the abort (only the known teardown segfault, whose signature it
had already recorded). Two real hazards were found by reading and were removed, and NEITHER stopped it:

* the cloud collector was `CanRunParallel() == true` while touching `RelationshipComponent`, a type no
  scene in this repository instantiates, so its pool is created on the fly — and EnTT's `assure<T>()`
  MUTATES (`pools.resize`) even through a const registry, which races with any other parallel system
  doing the same. It is serial now, and the note on the class says why;
* the frame's cloud command was emplaced with `clouds.Data` — a reference INTO the registry — in the same
  argument list as `CollectHeroClouds( registry )`, which touches the registry. Argument evaluation order
  is unspecified. Both are hoisted into locals now.

**What to do next, so this is not re-derived.** The exception type is the whole clue: libc++ throws
`length_error("vector")` from exactly one place, `vector::__vallocate` when the requested count exceeds
`max_size()` — i.e. a `size_t` that is corrupt or has underflowed, not a legitimately large allocation.
Two instruments would settle it and neither was affordable in the time this phase had:

1. a run loop under `MallocGuardEdges=1 MallocScribble=1`, which turns a heap overrun into a fault AT the
   overrun rather than at the next allocation;
2. a build with `-fsanitize=thread` for the first two frames, which is the only tool that will name a race
   this rare.

Until then this is the phase's one open defect, and it is named here rather than in a commit message so
that it is found by whoever picks up A1.

## AB: the abort, named — it was the ECS scheduler, and it was never ours, 2026-08-20

### The mechanism, in one paragraph and then in numbers

EnTT creates a component pool on the **first touch** of a type. Every path that touches one —
`view<T...>()`, `has<T>()`, `get<T>()`, `try_get<T>()` — goes through `basic_registry::assure<T>()`, and
`assure` **mutates**, through its `const` overload as well, because `pools` is `mutable`
(`ThirdParty/entt/include/entt/entt.hpp`). One first touch performs three unsynchronised writes:

1. `type_index<T>::value()` takes an index from **one global counter**, and that counter is
   `ENTT_MAYBE_ATOMIC(id_type) value{}` incremented with `value++`. `ENTT_USE_ATOMIC` **was not defined
   anywhere in this workspace**, so it was a plain read-modify-write on a global: two threads could be
   handed the SAME index for two DIFFERENT types;
2. `pools.resize( index + 1 )` — a `std::vector` grow;
3. `pools[index].pool.reset( new ... )`.

`Scene::ExecuteSystems` runs maximal runs of `CanRunParallel()` systems concurrently. Any two collectors
that first-touch a type in the same frame therefore race on all three. **`std::length_error: vector` is
exactly what libc++ throws from `vector::__recommend` / `__vallocate` when it reads a size that a torn
resize left beyond `max_size()`** — which is why the exception names `vector` and no cloud symbol.

It also explains the timing the A0 report could not: pools are created **only in the first frame**, so
after that `assure` is a bounds check and a pointer test and there is nothing left to race on. "In the
first frame or two, never later" is not a hint about clouds; it is the shape of lazy initialisation.

### The window, measured deterministically rather than waited for

The instrument was one lambda: count the registry's pools with `registry.visit` before and after each
parallel group, and print when the count moves. **It fires on every run**, so a defect that needed fifty
runs to appear once became a per-run observable.

`Clouds_HeroCloud`, camera `0,200,0`, `--shot-frames 3`, on the branch as `06887fcc` left it:

| parallel group | systems | threads | pools created INSIDE the group |
|---|---|---|---|
| `[0,2)` | MeshECSSystem, TextECSSystem | 2 | **5** |
| `[6,9)` | TerrainECSSystem, PointLightECSSystem, SpotLightECSSystem | 3 | **1–2** |

**Six to seven component pools were being created concurrently on every single run.** The count varies
run to run by one, which is itself a sighting of the timing dependence.

After `Scene::PrepareComponentPools`: **zero, on every run.** That is the knock-out.

### The same race, in isolation, with no engine around it

A 90-line program — nothing but EnTT, as this workspace builds it — races 64 component types across
threads on a fresh registry, one trial per process so each outcome is attributable:

| 200 trials x 4 threads | count |
|---|---|
| clean | **3** |
| hard crash | **196** |
| two types sharing one pool index | **1** |

Crash signals: 119 SIGSEGV, 64 SIGTRAP, 7 SIGABRT, 4 SIGBUS, 2 SIGKILL. At two threads the dominant
outcome was instead a **hang** — a corrupted registry that never returns.

The one "two types sharing a pool index" is the `value++` race caught directly: after it, each type reads
the other's storage, and every `std::vector` inside that storage has a garbage size.

### Two of the A0 report's conclusions were wrong, and they cost the phase its answer

* **"`RelationshipComponent` — a type NO scene in this repository instantiates, so its pool is always
  created on the fly."** It is not: `Scene::CreateEntityWithUUID` adds a `RelationshipComponent` to
  **every** entity it makes, so that pool exists before any system runs. Making
  `VolumetricCloudECSSystem` serial removed one racer from a race it was not in, which is why it changed
  nothing — and the report says so honestly.
* **"The signal is with the scenes whose component carries a `.dcmv` handle."** With the mechanism in
  hand the handle is irrelevant: what decides whether the window opens is which component types the
  scene leaves ABSENT, because those are the pools nobody creates during load. Regrouping the report's
  own table by SCENE rather than by handle gives `Clouds_Demo` 0/60 against the `Clouds_HeroCloud`
  family 6/158 — a cleaner split than the handle gave, and one with a mechanism behind it.

Neither was carelessness. Both are what a correlation table says when the variable that actually matters
is not one of its columns.

### The fix

* **`Scene::PrepareComponentPools`** creates every pool a system may touch, serially, before any group
  opens. After it, `assure<T>()` does not write, so there is nothing to race on. Cost: one bounds check
  per type per frame, against an 18 ms frame.
* **`ENTT_USE_ATOMIC`, at workspace scope** (`BuildScripts/Workspace.lua`), so the type-index counter is
  an atomic increment. Preparing the pools already means the engine's own collectors never reach that
  counter concurrently; this is what covers every OTHER first touch — preview scenes, thumbnail scenes,
  scripts. It is workspace-wide because the macro changes the TYPE of a shared static: all projects or
  none, or two objects disagree about that static's layout.
* **`Desert/Tests/Engine/ComponentPools`** reads the system headers and `Scene.cpp` as text and requires
  every touched type to be prepared, because an omission compiles and only shows up one run in fifty.
* **`Engine/EntryPoint.hpp`** installs a `std::terminate` handler that prints the exception and the
  throwing stack. For an UNCAUGHT exception libc++abi calls terminate **without unwinding**, so
  `backtrace()` sees the frames that threw. Thirty lines that would have named this in one run instead of
  a phase.

### What was tried and did NOT work — recorded so it is not tried again

| amplifier | result |
|---|---|
| 16 CPU spinners on 10 cores, 30 runs | **0 crashes** — pure CPU contention does not widen it |
| a 0.3 ms sleep inserted in `assure` before `pools.resize`, 20 runs | **0 crashes** |
| the same, with the fix on, 20 runs | 0 crashes |

**And the honest limit of this report: the abort was never observed on this machine at all** — 0 in
roughly 80 pre-fix runs, against the teamlead's 1/80 and A0's 6/116. A "zero after" series therefore
proves very little on its own, and is not what the case rests on. The case rests on the deterministic
instrument (6–7 racing pool creations per run, then 0), on the isolated demonstration (196 crashes in 200
trials), and on the exception type matching the throw site exactly.

### The picture did not move, on all six points

The A0 branch committed its six-point protocol as files, which makes this the cheapest regression in the
programme: shoot the same six points on the fixed binary and compare the bytes. `Clouds_HeroCloud`,
camera `0,200,0`, `--shot-frames 90`, against `Docs/Clouds/Shots/A0_hero_*.png` as taken on the PRE-fix
binary.

| point | bytes | differing |
|---|---|---|
| zenith away `0,0.9,-1` | 1 160 396 | **0** |
| mid away `0,0.45,-1` | 1 239 541 | **0** |
| horizon away `0,0.12,-1` | 1 293 977 | **0** |
| zenith sunward `0,0.9,1` | 1 197 316 | **0** |
| mid sunward `0,0.45,1` | 1 271 909 | **0** |
| horizon sunward `0,0.12,1` | 1 310 949 | **0** |

**Six of six identical files**, three elevations and both azimuths. Expected, and worth measuring anyway:
the change moves WHEN a pool is created, never what is in it, and the diff touches no rendering code at
all.

### The acceptance series

`Clouds_HeroCloud`, `--shot-frames 3`, the binary built from the committed source: **0 aborts in 215
runs** (175 + a 40-run top-up after the harness capped the first run's lifetime).

### Did it live before Э4? Yes, and Э4 made it LESS likely rather than more

`git diff a5cfa2ab 06887fcc` over `Core/Scene.cpp`, `Core/Scene.hpp`, `ThirdParty/entt` and the five
systems that make up the two racing groups — `MeshECSSystem`, `TextECSSystem`, `TerrainECSSystem`,
`PointLightSystem`, `SpotLightSystem` — is **empty**. Every line that races is byte-identical to the
commit Э4 branched from.

The one scheduling change Э4 did make points the other way. `Scene::ExecuteSystems` groups a maximal RUN
of `CanRunParallel()` systems, and Э4 flipped `VolumetricCloudECSSystem` from `true` to `false`:

| | groups, in EditorLayer registration order |
|---|---|
| before Э4 | `[0,2)` Mesh+Text · TimeOfDay serial · **`[3,9)` Skybox+HeightFog+VolumetricCloud+Terrain+Point+Spot — SIX threads** |
| after Э4 | `[0,2)` Mesh+Text · TimeOfDay serial · `[3,5)` Skybox+HeightFog · VolumetricCloud serial · `[6,9)` Terrain+Point+Spot |

**Э4 split a six-wide parallel group into a two-wide and a three-wide**, which can only reduce the number
of pairs able to race. The defect was strictly more exposed before the phase that got blamed for it.

A0's "0 aborts in 30 runs on the pre-change binary" is not evidence against this: at the teamlead's own
measured 1.3 %, a clean run of 30 happens 68 % of the time. Measuring absence needs a sample size nobody
in this programme has spent on it, which is exactly why the mechanism — not the frequency — is what
closes it.


## A1: the sculpting tool — what the three primitives cost and what the blend radius buys, 2026-08-20

Phase Э4 A1 (`Docs/Clouds/PLAN_AUTHORED_CLOUDS.md` §5). A0 proved a fused cloud body could exist by
writing one in C++; A1 makes the capability belong to an artist. The panel is
`Editor/Source/Editor/Panels/Clouds/CloudModellingVolumePanel.{hpp,cpp}`, and it is an editor for
`CloudModellingVolumeRecipe` and nothing else — which is exactly what A0 put the recipe in the file's
header for.

### The accepting claim, measured before it was photographed

The same five lumps, the same box, the same everything except the join's blend radius. Six-connected
components of the body voxels, counted:

| blend radius | body voxels | components | sizes |
|---|---|---|---|
| **4 m** | 6 223 | **4** | 2167, 1504, 1276, 1276 |
| **75 m** | 16 434 | **1** | 16434 |

**That is the phase's thesis as a number.** The procedural producer's coverage is `best - second`, which
is exactly zero on the bisector between every pair of feature points, so its lobes are ALWAYS separate
components and no threshold can join them — three tasks measured that independently before Э4 was
approved. A smooth-min union can be either, and which one it is is a knob an artist turns.

The 2.6x growth in body voxels is not a side effect to be tuned away: the join inflates the surface by
`BlendRadius * ln(sum of weights)`, which at 75 m over five lumps of total weight 5.2 is 124 m. It is why
`ValidateCloudModellingRecipe` reserves that much room before it will accept a recipe.

### The format grew and the shipped cloud did not move

The container is version 2: a lump carries a primitive, a rotation and a weight, so its record is 52
bytes rather than 32. There is no version 1 reader — contract §4 — and the one v1 file that ever existed
was re-baked by `Tools/CloudVolumeBaker` in the same change.

**Its voxel payload is byte for byte what it was: 0 of 4 194 304 bytes differ.** Eight unrotated
unit-weight ellipsoids are the identity case of everything this phase added, so the re-bake moved the
container version and not one voxel. That is asserted in
`Desert/Tests/Engine/CloudModellingRecipe` rather than left as a claim in a commit message.

### The three primitives, and why the set is closed at three

**The capsule is the one an ellipsoid cannot stand in for.** Both shapes below are 0.12 km across and
0.40 km tall; the measurement is the body's half-width at two heights on the axis:

| | at the middle | at 60 % of the height | ratio |
|---|---|---|---|
| capsule | 0.1250 km | 0.1250 km | **1.000 — no taper at all, it is a swept sphere** |
| ellipsoid | 0.1250 km | 0.0938 km | **0.750 — a quarter of its width gone** |

The ellipsoid's predicted ratio is `sqrt(1 - 0.6^2) = 0.800`, which is 0.100 km, which is 12.8 voxels;
counting whole voxels gives 12 and therefore 0.0938. The gap between 0.750 and 0.800 is that
quantisation and not a disagreement with the maths — the point stands either way, and the capsule's
1.000 has no quantisation to hide in.

The constant cross-section is the spreading cumulus base and the elongated growths of
`ANALYSIS_APPROACH.md` §6; built from ellipsoids they come out lens-shaped, and the only fix is a row of
overlapping lumps at one lump per unit of length.

**The sphere is an ellipsoid, and a cheaper one.** Equal radii reduce Quilez's bounded form to `|p| - R`
algebraically, but the compiler cannot perform that reduction because it cannot know at the call site
that the radii agree — so the reduced form saves two vector divides, a `length` and a division per lump
per voxel, and is EXACT where the general form is a tight underestimate. The suite asserts the two agree
on the BYTES, so switching a lump between Ellipsoid and Sphere cannot twitch the body.

**Bake times, debug build, the shipped eight-lump recipe:** 1 570 ms, against A0's ~1 000 ms claim for an
optimised build. The per-voxel work also went DOWN this phase — each lump's distance is now computed once
and kept, where A0 evaluated every ellipsoid twice (once to find the nearest, once to sum). Both halves
of that are in the same evaluator, so the preview and the bake cannot drift apart.

### The weight is a dilation, and its size is algebra rather than feel

Weighting a lump's term in the sum is the same function as the unweighted join over distances
`d - r*ln(w)`. So a weight grows its lump by exactly `BlendRadius * ln(Weight)`:

A 0.25 km sphere at a 50 m blend radius, its surface found by walking out along +x until the profile
falls to zero:

| weight | surface reach | |
|---|---|---|
| 1 | 0.2422 km | |
| 4 | 0.3047 km | |
| | **measured dilation 62.5 m** | **predicted `0.05 * ln 4` = 69.3 m** |

The 6.8 m between them is under half a voxel (15.6 m), which is the resolution of measuring a surface by
counting voxels.

The bound therefore counts the SUM OF THE WEIGHTS and not the lumps. The two agree while every weight is
1 — which is A0's case, and is why a bound left counting lumps would have passed every test A0 wrote —
and they part company exactly when the surface starts moving outward.

### The six-point protocol: SIX of six byte for byte

`Clouds_HeroCloud` through the protocol's six points, the A0 binary against this branch's. Camera
`0,200,0`, `--shot-frames 90`, 1280x766.

| point | file bytes differing | pixels changed | max delta |
|---|---|---|---|
| zenith away `0,0.9,-1` | **0** | 0 | 0 |
| mid away `0,0.45,-1` | **0** | 0 | 0 |
| horizon away `0,0.12,-1` | **0** | 0 | 0 |
| zenith sunward `0,0.9,1` | **0** | 0 | 0 |
| mid sunward `0,0.45,1` | **0** | 0 | 0 |
| horizon sunward `0,0.12,1` | **0** | 0 | 0 |

Six identical FILES, which is the strongest form the protocol has. A PNG file-byte count is only
meaningful when it is zero — deflate is not a per-pixel encoding — so the pixel columns are decoded
rather than inferred from file size.

### THE FIRST RUN IN A FRESH WORKTREE IS NOT A BASELINE, and this is a correction to A0's method

Getting that table took an investigation, and the finding is worth more than the table.

The first baseline taken here — shot before a line of A1 was written — disagreed with this branch on
**zenith away, and only there: 110 pixels of 980 480, each by one 8-bit level, scattered across the whole
frame.** That is very nearly the shape A0 reported (199 pixels, one level, zenith away, five other points
byte-identical), and A0 attributed it to `Common/CloudField.glslh` becoming a different FILE and its
SPIR-V being recompiled with different floating-point scheduling.

**That attribution is wrong, and A1 did not change a shader at all.** Four experiments, each ruling out
one candidate:

| experiment | result |
|---|---|
| same binary, same point, run twice | identical files — **the repeat floor is zero** |
| same binary, shader cache deleted between runs | identical files — **not shader compilation** |
| a `.dcmv` with the SAME voxels but a header declaring 1 lump instead of 8 and a 123 m blend radius instead of 50 m | identical file — **the recipe is inert at render time; only `SizeKm` is read** |
| the new panel un-registered from `EditorLayer`, and separately an inert function added to an unrelated engine translation unit to move the binary's layout | identical files — **not the panel, not code layout** |

Every input to the renderer was byte-identical and the frames still differed, so the last suspect was the
baseline itself. **The A0 binary was rebuilt from `f130f2de`, with the v1 volume restored, and shot
again**: it reproduces THIS BRANCH byte for byte and does NOT reproduce the original baseline PNG. Run
twice more, it is stable.

So the outlier was the baseline shot, and what was special about it is that it was **the first time the
Editor had ever run in a freshly created worktree**. Whatever that first run initialises, it initialises
differently from every run after it — and it is not the shader cache, because deleting that changes
nothing.

The practical rule, and it is cheap: **discard the first render in a new worktree, or take the baseline
twice and keep the second.** A baseline that has not been shown to reproduce itself is not a baseline,
and the cost of not knowing that here was four experiments chasing a change that had moved nothing.

### Every test verified by breaking it — 14 of 14, and the three that did not break first time

`Desert/Tests/Engine/CloudModellingRecipe` is 16 tests. Each claim was checked by sabotaging the thing it
measures, rebuilding, and confirming the suite went red. **Three sabotages passed on the first attempt,
and the A0 report's rule is that such a break is a hole in the suite rather than luck.** All three are
worth recording, because two of them were the TEST's fault and one of them was the BREAK's:

| sabotage | first result | what it actually meant |
|---|---|---|
| the canonical sort forgets the three fields this phase added | passed | **a real hole.** Dropping the new fields changes nothing semantic — only the order the sum accumulates in — so the only observable is rounding, and at the fixture's 50 m blend radius the terms decay so fast that the sum is dominated by its nearest one or two. Reordering a negligible tail cannot move a byte. Fixed with a fixture built for sensitivity: a 250 m blend radius so no term dominates, twelve mutually-equivalent lumps, and rotations with no shared symmetry (a regular 36-degree family leaves the lumps congruent under the very rotations that separate them, so their distances are exactly equal at a great many points and reordering exactly equal numbers changes nothing). It now moves 1–3 bytes of 4 194 304 across three shuffle seeds — the same order as A0's six — and all three seeds catch it. |
| the decoder stops refusing a primitive from the future | passed | **a real hole, of a gentler kind.** `ValidateCloudModellingRecipe` refuses an unknown primitive from its switch's default and runs at the end of `Decode`, so the file was still rejected — by the other guard, with the other message. Defence in depth is right; a test that cannot tell which layer caught it is not. The assertion now names a string only the decoder produces. |
| the progress callback is allowed to reach the arithmetic | passed | **the BREAK was wrong, not the test.** The sabotage skipped slab `z = 3`, which is empty air in that fixture, so it changed no voxel. Retargeted at `z = depth/2` it turns the suite red immediately. |

The remaining eleven went red first time. The most informative is the last: making the box check ignore a
lump's rotation reddens **five** tests at once, because a rotated capsule then reaches further than its
reserved room and the fixture stops validating at all.


## A2 + A3: several bodies in one sky, and the catalogue the phase is measured by, 2026-08-20

Phase Э4 A2 and A3 (`Docs/Clouds/PLAN_AUTHORED_CLOUDS.md` §5), taken together because A3 is what A2 is
for: A0 proved a sculpted body could exist, A1 gave it to an artist, A2 lets a sky hold several DIFFERENT
ones, and A3 asks whether the ten genera of `ANALYSIS_APPROACH.md` §6 come out distinguishable.

### How the volumes reach the march, and why it is an atlas

A0 bound one `sampler3D` and could draw one body per frame; a second, different one was named in the log
and not drawn. There are exactly two ways to reach several volumes from one dispatch, and the tree decides
between them rather than taste:

| | descriptors | fetch sites | per-hit cost | memory for N bodies |
|---|---|---|---|---|
| an ARRAY of `sampler3D` | — | — | — | — |
| **eight SEPARATE bindings** (what compiles) | **8, in each of TWO pipelines** | **8** | an eight-way compare chain around the fetch | N x 4.00 MiB + 8 fallbacks |
| **one atlas image** | **1** | **1** | **one clamp and one multiply-add** | N x 4.00 MiB + 1 fallback |

**The array row is empty because this engine refuses it in so many words.**
`Graphic::API::Vulkan::VulkanShaderReflection.cpp` rejects an arrayed sampled image with
*"arrays of descriptors are not supported — declare separate bindings"*, because every descriptor set
layout it builds hardcodes `descriptorCount = 1`. Making it work is a change to the descriptor machinery
of the whole renderer, which is not this task's zone and would be paid for by every shader in the tree.

So the version of "an array" that compiles today is eight separate bindings — and the fetch cannot then be
indexed at all, because a sampler is not a value: selecting one means comparing the loop counter against
eight constants and having eight `texture()` sites, on divergent paths, inside the function the march calls
once per march step AND about thirty times more often again per shadow-ray step.

**The atlas is one image holding the bodies end to end along the DEPTH axis.** The volume's layout has x
varying fastest and z slowest, so stacking on z makes the atlas the bodies' bytes CONCATENATED —
`Assets::AssembleCloudModellingAtlas` is a run of `memcpy` and `CloudAtlas.TheAtlasIsTheBodiesConcatenated`
asserts the whole result with one `==`. Stacking on the vertical axis instead would interleave the bodies
1 024 pieces deep for a picture that is identical.

**Nothing bleeds across a slab boundary, and it takes TWO guarantees rather than one.**
`CloudAuthoredAtlasUvw` clamps the depth coordinate to the body's own texel centres before folding it into
the slab, so in real arithmetic the filter's two depth taps are two texels of the same cloud.
`CloudAtlas.NoCoordinateOfOneSlabCanReachItsNeighbour` drives that over every slot of every slab count and
257 coordinates each — and it is where the second guarantee turned up. In FLOAT, dividing by a slab count
that is not a power of two leaves the last coordinate a few parts in a hundred thousand past the centre —
**measured at 3e-5 of weight on slot 1 of 3** — so the hardware would blend that much of the NEXT body's
outermost texel. It is nothing because the BAKE refuses a volume whose body touches its own boundary: that
texel is four zeroes. The claim in the first draft of this section — "by construction rather than by the
bake's empty shell" — was too strong by 3e-5, and the test is what said so.

### The atlas is built on demand, and a fixed one would have broken the budget

A fixed eight-slab atlas is simpler: the slab count is then a compile-time constant and an instance needs
only a slot index. It was refused by arithmetic:

| | 1280x766 | 1920x1080 |
|---|---|---|
| occupied before slot A (A0's measurement) | 20.67 MiB | ~30.1 MiB (the trace and history targets scale with the frame) |
| **fixed** 8-slab atlas | 32.00 | 32.00 |
| **total** | 52.67 | **62.1 of 64** |
| **demand-sized**, one hero cloud in the scene | 4.00 | 4.00 |
| **total** | **24.67** | **34.1** |

At 1080p a fixed atlas leaves 1.9 MiB of decision D-9's budget, which is not a margin. The service
therefore builds an atlas holding exactly the bodies the live hero clouds name, and rebuilds it only when
that set — or one of their revisions — changes. A project may carry fifty `.dcmv` files; a frame pays for
the ones an entity actually names, and that is measured rather than asserted: the six protocol frames were
taken twice, once with two extra `.dcmv` in the volumes directory and once without, and `mid_away` is the
same 1 239 541 bytes both times.

### Eight instances and eight bodies are two different limits

`kCloudAuthoredSlots` caps the INSTANCES, because each one costs the march a bounds test at every field
sample. `kCloudModellingAtlasMaxSlabs` caps the BODIES, because each one costs 4.00 MiB. They are the same
number today and the renderer warns about them separately, with the number that ran out in the message —
a wood of forty copies of one sculpted tree is one slab and eight instances, and the fix for each of those
two complaints is a different edit.

### The instance did not grow, because the cutout was a derived number

Adding a slab to the instance needed a float, and the struct was full: twenty floats, all read. The float
came from removing a duplicate rather than from padding. A0 carried `Strength` and `Cutout` separately,
where `Cutout` was `SuppressProceduralField ? Strength : 0` — one value derived from the other and a flag.
Signed, it is one number:

```
Strength = abs( BoundsMin.w )        Cutout = max( BoundsMin.w, 0 )
```

exact at every input including a strength of zero, where both must be zero whatever the flag says. So the
instance is still 80 bytes and still twenty floats with nothing spare, and `BoundsMax.w` carries where the
body's slab begins. The buffer's header gained the slab count — a property of the ATLAS and therefore of
the buffer, not of an instance, because eight copies of one number are eight chances to disagree.

### The picture did not move: SIX of six byte for byte, against the frames the owner accepted

`Clouds_HeroCloud` through the protocol's six points, this branch's binary against **the committed A0
accepting frames** rather than against a baseline shot here. Camera `0,200,0`, `--shot-frames 90`, 1280x766.

| point | file bytes differing | pixels changed | max delta |
|---|---|---|---|
| zenith away `0,0.9,-1` | **0** | 0 | 0 |
| mid away `0,0.45,-1` | **0** | 0 | 0 |
| horizon away `0,0.12,-1` | **0** | 0 | 0 |
| zenith sunward `0,0.9,1` | **0** | 0 | 0 |
| mid sunward `0,0.45,1` | **0** | 0 | 0 |
| horizon sunward `0,0.12,1` | **0** | 0 | 0 |

The reduction is arithmetic and asserted as such: with one slab the base is 0 and the divisor is 1, and
adding zero and dividing by one are exact in IEEE754, so `CloudAuthoredAtlasUvw` is bit for bit A0's clamp.
`CloudAtlas.OneSlabIsBitForBitTheAddressingA0Shipped` compares the BITS rather than using a tolerance.

### AND THE BASELINE I SHOT MYSELF WAS WRONG, which is worth more than the table above

The first comparison this task ran said **17.3 % of pixels changed at mid away, max delta 126/255** — which
is, to the pixel, the number A0 measured for *the hero cloud switched off*. It was not a defect in the
atlas. It was the baseline:

| step | what it showed |
|---|---|
| the two new `.dcmv` files removed from the assets directory and the frame retaken | identical to the suspect frame — **not the added assets** |
| this branch's frame against the COMMITTED `A0_hero_mid_away.png` | **0 differing pixels of 980 480** |
| the "baseline" against the committed `A0_hero_off_mid.png` | **0 differing pixels of 980 480** |

The baseline binary had rendered the scene with the hero cloud missing entirely, and the reason is a trap
this programme has not recorded before: **the shaders are cooked at runtime, so an old binary run after a
`.glslh` edit is a mismatched pair.** The baseline was taken after `Common/CloudAuthored.glslh` had already
grown `u_CloudAuthoredSlabCount`, so the A1 binary uploaded a payload whose byte 4 was alignment and the
new shader read it as a slab count of zero — a division by zero in the addressing, a NaN coordinate, and no
hero cloud, silently.

**The rule that follows is cheap and this document did not have it:** take the baseline BEFORE touching a
shader, or rebuild the baseline binary from its own commit with its own shader tree. A1's rule — discard
the first render in a fresh worktree — is necessary and was not sufficient. The committed accepting frames
turned out to be the better reference anyway, because they are what the owner signed off and they cannot be
contaminated by anything this worktree does.

### "Zero when absent" is proven where A0 said it could be — on the frame

`Clouds_HeroCloud` with its one hero cloud's `Enabled` switched off, so the component is present, the
handle is set and the instance count is zero. Against the committed `A0_hero_off_mid.png`: **0 differing
pixels of 980 480**, the same file to the byte.

The instruction count in that path is A0's exactly, and it is arranged that way on purpose:
`CloudSampleAuthoredField` reads the count, compares it to zero and RETURNS — the slab count is read
after that line and not beside it, so a sky with no hero clouds does not load it. A0 measured the cost of
that one load and one compare at +0.1 to +1.0 ms and could not separate it from a run-to-run spread of
0.95 ms; A2 adds nothing to it, and the frame is where the claim is provable rather than arguable.

**What was NOT measured, and why:** a slope for the absent case against the PREVIOUS binary. Building
A1's Editor would have meant restoring A1's shader tree with it — the shaders are cooked at runtime, and
mixing an old binary with a new `.glslh` is exactly the trap that produced the false baseline above. The
instruction-level argument plus a byte-identical frame is what is offered instead, and it is offered as
that rather than as a measurement.

### The price, measured on a machine that was shared

Frame-count slope, `(t900 - t300) / 600`, which cancels the ~20 s fixed start-up. Scene `ZZ_Perf<n>`:
eight hero-cloud entities over THREE distinct bodies, of which the first `n` are enabled — one field per
entity, one scene generator, no rebuild between configurations. Camera `0,200,0`, `--look 0,0.18,-1`,
1280x766, High tier, **debug build**. The four configurations were **interleaved inside every pass** and
the pass repeated, so each minimum is taken against neighbours that were equally busy.

| live instances | slabs in the atlas | min t300 | min t900 | **slope** | vs no hero cloud | per instance |
|---|---|---|---|---|---|---|
| **0** — components present, all disabled | 0 (the fallback is bound) | 30.774 s | 42.160 s | **18.65 ms/frame** | 1.00x | — |
| **1** | 1 | 31.364 s | 43.103 s | **19.40 ms/frame** | 1.04x, **+0.74 ms** | +0.74 |
| **3** | 3 | 32.001 s | 45.143 s | **21.75 ms/frame** | 1.17x, **+3.10 ms** | +1.03 |
| **8** | 3 | 33.250 s | 48.842 s | **25.98 ms/frame** | 1.39x, **+7.33 ms** | +0.92 |

Three passes, twelve legs, **no aborted run**. Per-pass slopes:
0 — 18.74 / 19.08 / 18.65 · 1 — 19.57 / 19.40 / 19.48 · 3 — 21.75 / 21.86 / 22.01 · 8 — 26.03 / 25.98 / 25.99.

**The atlas is uploaded ONCE per run and not once per frame**, which is the thing a demand-built atlas has
to prove: the engine's own log line appears exactly once in a 900-frame render —
`Modelling atlas built: 3 bodies, 128x64x384 RGBA8, 12.00 MiB on the device`.

**The machine is shared with other agents and the spread is quoted rather than hidden.** The per-pass spread is **0.2 % at eight instances, 0.9 % at one, 1.2 % at three and 2.3 % at none** — the
widest of the four is the configuration that does the LEAST work, which is what a shared machine looks
like when the signal is real: the noise is a constant number of milliseconds and it is a larger fraction
of a smaller number. Every difference in the table is many times its own spread. For contrast, A0 had to
throw a leg away and quote a range because one of its four passes did not finish; none of these twelve
aborted.

**One number is NOT comparable with A0's**, and it is the important one to say out loud: A0 measured
**+3.7 to +5.1 ms for one hero cloud**, and this table says +0.74 ms. Both are right about different
pictures. A0's body filled a sixth of the frame from two metres; these stand 13 to 46 km away and the
whole trio covers perhaps a twentieth of it. **What an instance costs is dominated by how much of the
frame its body covers**, because the bounds test is cheap and the march through real material is not —
which is also why eight instances cost 1.39x and not 8x.

**What the shape of it says.** The cost is not linear in the instance count and should not be: an
instance costs six compares at every field sample whether or not the ray is anywhere near it, and only
the ones a ray actually enters cost a fetch and a march through real material. Three instances of three
DIFFERENT bodies cost the same per instance as eight instances of three bodies — the atlas is one
descriptor and one fetch site either way, which is the whole point of choosing it.

**Debug, and the ratio is a lower bound on a shipping build**, for the reason the OE-FIX table gives: the
baseline carries CPU work a release build shrinks and this dispatch does not.

### The catalogue: ten genera, measured on the voxels

Every column below is read off the BAKED body by `Desert/Tests/Engine/CloudCatalogue`, which prints this
table itself so a reader can regenerate it rather than trust it. Aspect is the widest horizontal extent
over the vertical one; top/bot is the widest slice in the top fifth of the body over the widest in the
bottom fifth — the anvil measure; comps is six-connected components; detail is the mean of the volume's
Detail Type channel over the body, 0 wispy and 1 billowy; pocket is the largest air pocket a SLICE of the
body encloses, in voxels.

| genus | aspect | top/bot | occupancy | components | detail | air pocket |
|---|---|---|---|---|---|---|
| cumulus humilis | 3.78 | 0.49 | 8.15 % | 1 | 0.90 | 0 |
| cumulus mediocris | 1.24 | 0.39 | 8.50 % | 1 | 0.95 | 0 |
| cumulus congestus | **0.44** | 0.72 | 8.84 % | 1 | 0.97 | 0 |
| cumulonimbus | 1.19 | **2.67** | 4.78 % | 1 | 0.48 | 0 |
| stratocumulus | 6.91 | 1.00 | 11.93 % | **1** | 0.76 | 0 |
| stratus | 11.26 | 1.02 | 7.59 % | 1 | **0.16** | 0 |
| altocumulus | **20.42** | 1.00 | 1.85 % | **25** | 0.70 | 0 |
| cirrus | 6.92 | 0.49 | 2.27 % | 6 | **0.04** | 0 |
| lenticular | 2.90 | 0.85 | 12.19 % | **3** | 1.00 | 0 |
| freeform (arch) | 1.39 | 0.84 | 8.70 % | **1** | 0.95 | **1 368** |

**The cumulus ladder is monotone by a margin and not by a hair**: 3.78, 1.24, 0.44 — each step is a factor
of three where the test demands 1.5. **The anvil is 2.67**, and nine of ten genera measure 1.0 or below on
that column. **The arch is the only body that is one component AND encloses air**, which is the pair
`best - second` cannot hold at any coverage.

### Form by form, and what each one is missing

| genus | did it come out? | what is missing, and why |
|---|---|---|
| **cumulus humilis** | **yes** | Nothing of the genus. It is 1.2 km across and 0.2 km thick, so at 9.4 m per voxel the silhouette is finely resolved and the up-rez does the rest. `A3_genus_humilis.png` |
| **cumulus mediocris** | **yes** | The cauliflower. The crown is one smooth dome where a real one is a cluster of turrets at three scales — the solver's absence, and the entry where it is cheapest to live with. `A3_genus_mediocris.png` |
| **cumulus congestus** | **yes, after a retune** | Its first bake read as a STRING OF BEADS: consecutive lumps overlapped by 80 m where they are 400 m across, and the up-rez ate the necks. The fix is a rule the catalogue now follows everywhere — overlap by about half a lump's own height — and the residue is ring creases at 53 m per vertical voxel plus, again, no boil. `A3_genus_congestus.png` |
| **cumulonimbus with an anvil** | **yes, and it is the one that answers the procedural producer** | The canopy is 2.67x wider than the base, measured, and no vertical profile curve can do that. What is missing is texture rather than shape: the tower is a smooth column (no boil) and the canopy has no fibrous fallstreaks under it, so it reads closer to a mushroom than to a storm. `A3_genus_cumulonimbus.png` |
| **stratocumulus** | **partly** | It is ONE connected deck with rolls in it — which is the pair the Alligator cannot hold — but the rolls DO NOT READ from a level camera, because a deck seen edge-on is a bar. The structure is in the volume and the measurement finds it; showing it needs a camera above or below the sheet, and a hero cloud 8 km across is a thing a player flies over rather than looks at. `A3_genus_stratocumulus.png` |
| **stratus** | **yes, and it should not be a hero cloud at all** | Nothing is missing; the genus is the absence of features. This is the entry the PROCEDURAL producer does better and this report says so: a formless overcast is what a flattened profile and a high coverage give for free, over the whole sky rather than over one 10 km box, at no memory cost. `A3_genus_stratus.png` |
| **altocumulus** | **yes, and it is the genus Э4 was least needed for** | Nothing. Twenty-five separate elements on a lattice is a description of the Alligator's own output — separate lobes with a zero on every bisector. It is in the catalogue because §6 lists it and because a sculpted one can be placed exactly where a shot wants it, not because the procedural producer could not make one. `A3_genus_altocumulus.png` |
| **cirrus** | **partly** | The character is right — mean Detail Type 0.04, the only genus that is wispy everywhere — but the BODY contributes almost nothing: 2.3 % occupancy of a 9 km box, six thin rods that the up-rez turns into fibres. It is the clearest case of the volume carrying the silhouette and the noise carrying the cloud, and at this size the silhouette is nearly all noise. A cirrus deck is a job for the procedural producer with a wispy type; a sculpted one is for a specific streak in a specific shot. `A3_genus_cirrus.png` |
| **orographic (lenticular)** | **yes** | Nothing. Three smooth stacked lenses, separate by design, and the smoothness is finished on the component side with Detail Factor 0.15 — which is the knob `ECS::HeroCloudData` carries and the one place in the catalogue where turning the erosion DOWN is the right answer. `A3_genus_lenticular.png` |
| **freeform (arch)** | **yes, and it is the proof of the phase** | Nothing of the shape. One six-connected body with an enclosed air pocket of 1 368 voxels in a slice, where the other nine measure zero — connected AND holed, which `best - second` cannot be at any coverage. What is missing is that it does not look like WEATHER: it is architecture, and the up-rez softens its corners without making it a cloud. That is the correct outcome for the Tallneck class, which is the class §6 named it for. `A3_genus_freeform.png` |

### The one limit that is not a defect

The cauliflower surface of p. 69 of the deck comes out of a fluid solver, and §6 of the analysis records
that there will be no solver. It shows in exactly the two places it should: the congestus's crown and the
cumulonimbus's tower are SMOOTH where a real convective turret is recursively lumpy at every scale down
to metres. Lumps give the silhouette and the up-rez noise gives the edge; neither gives boil. That is the
refusal as it was written down before this phase started, and it is what the catalogue looks like when it
is honoured.

### The other limit, and this one IS the format

The vertical axis is 64 voxels for the whole box, so a tall body has a coarse one: the cumulonimbus at
5.60 km spends 87.5 m per voxel vertically, and the ring creases visible on its tower are that number
rather than anything about the join. The horizontal axes are 128 and are three to five times finer. Raising
the resolution is refused by §2 of the plan — the detail is the up-rez's job — and the honest workaround is
the one the recipes now use: overlap consecutive lumps by about half their own height, so the join has no
thin waist for the erosion to eat.

### Every test verified by breaking it

`Desert/Tests/Engine/CloudAuthored` gained 10 tests (`CloudAtlas`) and `Desert/Tests/Engine/CloudCatalogue`
is 10 more. Each claim was checked by sabotaging the thing it measures, forcing a rebuild and confirming
the suite went red.

| sabotage | result |
|---|---|
| the atlas is assembled in reverse slab order | RED |
| the assembler stops checking a body's length | RED |
| the slab base gains half a slab | RED |
| the depth clamp is removed from `CloudAuthoredAtlasUvw` | RED (two tests) |
| the seam passes slab base 0 for every instance | RED |
| the union keeps the FIRST instance instead of the deepest | RED |
| the cutout reads `abs` where it should read `max( w, 0 )` | RED |
| `IsBindable` stops requiring an empty payload against the fallback | **GREEN — a real hole, closed** |
| `kCloudAuthoredSlots` is raised to 12 | RED (the budget assert fails to build) |
| the humilis box is halved | RED |
| two genera share a key | RED |
| humilis returns the mediocris recipe | RED |
| the cumulonimbus canopy is pulled in over its tower | RED (three weaker versions first) |
| the stratocumulus rolls are spread apart | RED |
| the cirrus streaks turn billowy | RED |
| the lenticular plates are pushed together | RED |
| the arch loses its base bar and becomes a "П" | RED (two tests) |

**Three findings came out of this rather than one clean table.**

1. **One real hole.** `OnlyAnEmptyPayloadIsBindable` varied the slab count and the instance count TOGETHER
   and never independently, so deleting the clause that forbids a live instance against the FALLBACK image
   left every assertion green — and that is the subsystem's oldest rake in test form. The case is in the
   suite now.
2. **Three breaks that were the BREAK's fault, all on the same test.** Narrowing one disc of a four-lump
   canopy leaves the other three spreading, so the anvil is still an anvil and `TheCumulonimbusIsWider`
   was right to stay green. It went red the moment the whole canopy was pulled in over the tower. A1's
   report records the same shape of mistake, and this is the second sighting.
3. **A defect in the harness itself, and it is worth more than either.** Run in a batch, `make` twice
   reported success having compiled NOTHING — the sabotage was then measured against the PREVIOUS binary,
   which reads as green. The driver deletes the suite's objects before every build now. The lesson is the
   one this whole task kept re-learning from a different direction: **a measurement that cannot see what
   it claims to measure looks exactly like a measurement that found nothing.**

### The frames

| file | what it shows |
|---|---|
| `Shots/A2_trio_three_bodies.png` | **⬛ THREE DIFFERENT HERO CLOUDS IN ONE SKY** — a cumulonimbus with its anvil, a fused cumulus mass, and an arch with sky through it. Three bodies, three slabs, one atlas, one dispatch |
| `Shots/A2_fused_mass_among_cushions.png` | **⬛ THE FUSED MASS BESIDE THE PROCEDURAL FIELD** — the sculpted cumulonimbus, one continuous surface from base to canopy, standing in a sky of the Alligator's separate cushions. The contrast IS the phase |
| `Shots/A3_genus_*.png` | the ten genera, one frame each, every one framed for its own size |

**The far field is still cushions and that is not a defect of this phase** (§1 of the plan): Э4 gives the
artist the near field and leaves the procedural producer the rest. Both frames show it, deliberately.

---

## Э5 — the procedural modelling volume: the wall of zeros is gone

Phase Э5 (`Docs/Clouds/ANALYSIS_APPROACH.md` §3, variant C, points 1–3). The procedural producer's shape
field is a sum of smoothed volumetric lumps joined by an exponential smooth minimum, baked into a
camera-centric periodic volume; the coverage threshold on the Alligator, the vertical profile table and the
per-species placement basis are gone.

### The defect this phase existed to remove, stated once more

`Alligator = best - second` is **zero wherever two feature points contribute equally**. There is a wall of
zeros between every pair of cells, so no setting of any slider could fuse two lobes and the sky was a deck
of separate cushions. Three tasks measured it independently and all three worked around it. The exponential
smooth minimum has no such wall: merging is its defining property, not a setting.

### The four numbers this phase decided, and what decided them

| decision | value | what fixed it |
|---|---|---|
| volume shape | **256 x 32 x 256 RGBA8 = 8.00 MiB** | BELOW by Nyquist — a voxel finer than 62.5 m is structure the march provably cannot find (`CloudFinestResolvableChordKm` is 125 m at Max Steps 256, and trilinear filtering cannot express a feature under two voxels). ABOVE by decision D-9: §A0 measured 20.67 MiB occupied, so 8.00 leaves 35.33 MiB — still the **eight** hero slabs A2 shipped. Variant C's 512 x 512 x 32 would have been 32.00 MiB and cut that to six |
| region | **48 km, 187.5 m per voxel** | Max View Distance over the region is how many times the sky repeats to the vanishing point: 60/48 = **1.25** against the five repeats §4 measured as the cure for moire at twenty |
| where it is baked | **CPU, on a worker, except the first** | measured, below. A compute bake was not built: the cost is a LATENCY and not a frame time, and a rebake happens once per lattice cell of camera travel |
| join cutoff | **14 blend radii** | `exp(-14) = 8.3e-7` of the nearest lump's term, so the error in the joined distance is `r * N * 8.3e-7` — 8.3e-5 of a unit profile at 600 lumps in range, a fiftieth of the 1/255 the volume is stored in |

### The cost of a rebake, which is the phase's own exit criterion

`Desert/Tests/Engine/CloudProceduralField` measures it on every run, best of three on a shared machine,
**DEBUG build**:

| species | lumps | ms per rebake |
|---|---|---|
| 1 | 714 | **1746** |
| 2 | 1254 | **3263** |
| 4 | 1674 | **5168** |

Far past a frame, which is what the worker is for. **The first bake of a scene BLOCKS**, and that is a
measured correction rather than a preference: with the pass skipped the frames cost almost nothing, so a
headless shot of ninety frames finished BEFORE an 800 ms bake did and wrote an empty sky. A rebake does not
block — it has a previous volume, at most one snap step away, and the volume is periodic, so the frame reads
the neighbouring tile.

### The coverage slider did not mean the sky, and now does

Alive cells are not sky cover: a cluster does not fill its cell, and how full it is depends on how deep
inside the threshold its own hash fell. Measured on the top-down projection of the baked volume:

| Coverage | 0.15 | 0.24 | 0.35 | 0.50 | 0.75 |
|---|---|---|---|---|---|
| taken directly as the alive fraction | 0.059 | 0.090 | 0.145 | 0.221 | 0.358 |
| after the three corrections | **0.120** | **0.219** | **0.324** | **0.506** | **0.754** |

The three corrections, each measured:

1. **The stack is bottom-heavy** (`t^1.7`). Spread evenly, six lobes put one or two at the wide base and
   four up the narrow tower, so the base was a rosette with holes in it and a full cell measured 48 per cent
   covered where the geometry says it should be full.
2. **The fill ramp is `(1 - Coverage)/Contrast`** rather than `Coverage/Contrast`, so at Coverage 1 every
   cell is full. It was uniform on [0,1] at every setting, which capped the sky at about 60 per cent however
   high the slider went.
3. **The alive fraction is `Coverage^0.68`**, measured. A power keeps both ends exact, which is the property
   the ends were built to have.

Worst deviation over the five settings: **0.030 of the sky.** The suite re-measures it and fails past 0.10.

### The lobes have to OVERLAP, and fusion is not free because the join can express it

The first written cluster displaced each lobe by a third of its OWN radius. The join of six concentric
ellipsoids is one ellipsoid, and the top-down projection came out as **a scatter of round dots — the same
defect as the Alligator's, arrived at from the other side.** Two lobes one golden angle apart on a circle of
radius `spread` are `1.86 * spread` apart; at (spread 0.52 R, radius 0.50 R) that is 0.97 R against a sum of
radii of 1.00 R — they only TOUCHED. At (0.48 R, 0.62 R) it is 0.89 R against 1.24 R, so they
interpenetrate by more than a quarter of a lobe.

### The six points, before and against

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, 1280x766, ImageStat over the full width and the top
71.9 % of the frame.

**The repeat floor is ZERO and was measured rather than assumed**, which is what makes every number below
exact: the six points were shot twice, across two rebuilds of the engine and the editor, and all six PNGs
are byte for byte identical between the runs (`cmp`, not a pixel diff — the files themselves). **Two things changed at once and both are named**: the producer, and the scene's
authored Coverage — 0.24 meant ~66 % sky cover under the old threshold and means ~15 % under the new count,
so the shipped scenes were re-authored at EQUAL SKY COVER (0.24 -> 0.762). See the note on
`VolumetricCloudData::Coverage`.

| point | mean before / after | contrast before / after | saturation before / after |
|---|---|---|---|
| zenith away `0,0.9,-1` | 0.528 / **0.570** | 0.400 / **0.449** | 0.128 / **0.135** |
| mid away `0,0.45,-1` | 0.556 / **0.596** | 0.410 / **0.308** | 0.136 / **0.077** |
| horizon away `0,0.12,-1` | 0.617 / **0.628** | 0.244 / **0.240** | 0.076 / **0.071** |
| zenith sunward `0,0.9,1` | 0.601 / **0.662** | 0.384 / **0.407** | 0.050 / **0.037** |
| mid sunward `0,0.45,1` | 0.569 / **0.614** | 0.264 / **0.320** | 0.070 / **0.089** |
| horizon sunward `0,0.12,1` | 0.633 / **0.638** | 0.283 / **0.287** | 0.066 / **0.086** |

Every point is brighter, by between 0.005 and 0.061 of mean luminance, and **the two horizon points barely
move at all** (0.011 and 0.005) while the two zenith points move most (0.042 and 0.061). That is the answer
a re-authored coverage predicts and not a coincidence: a grazing ray was already crossing enough cells to
saturate under either producer, and a ray straight up was not.

**The numbers are not the finding here and the frames are.** What a contrast measures is the spread of a
histogram, and a deck of separate cushions with dark creases between them has a wide one; so does a sky of
fused masses with blue between them. The two are told apart by looking, which is what `Shots/E5_*` are for.

### The relations this phase added, and the breaks that verified them

Nine sabotages, eleven suite runs, **nine red and two green**. Every suite was rebuilt from deleted
OBJECTS *and* a deleted BINARY first — see the note below, which is why.

| break | result |
|---|---|
| the generator ignores its seed | RED |
| the bake's bin lists lose the lumps' canonical order | RED |
| the lump size clamp is removed | RED (two suites: `CloudProceduralField`, `CloudType`) |
| the lumps are not splatted at their wrapped positions | RED (two tests) |
| a cell is hashed on its place in the REGION instead of its absolute index | RED |
| the join cutoff is lowered from 14 radii to 10 | **RED — and this is how the 14 was found** |
| the coverage exponent is set back to 1 | RED |
| the shader's volume height disagrees with the C++ constant | **GREEN — a real hole, closed** |
| the volume's vertical read is not clamped to the texel centres | **GREEN — a real hole, closed** |

**Three findings, and only one of them is a confirmation.**

1. **The cutoff at ten radii was written with the wrong `N`.** The quantisation argument used about a
   hundred lumps in range; `N` is not a constant of the design — it is how many lumps reach a voxel — and
   widening the clusters to make the coverage slider mean the sky took it to about six hundred. The suite
   went red at 1.24 of a 255th, which is the assertion failing exactly where it was written to.
2. **Nobody was comparing the shader's volume height with the C++ constant, and the header said somebody
   was.** `CloudProceduralField` does not include `Common/CloudField.glslh` at all — it compiles only
   `CloudGeometry.glslh` — so the claim that it asserted the agreement was simply false. The assertion is
   in `CloudField` now, which does compile that header. **The same sabotage turned up two dead macros:**
   `CLOUD_PROCEDURAL_VOLUME_WIDTH` and `_DEPTH` were declared "for symmetry" and never read, because the
   horizontal mapping is `(world - origin) * invRegionSize` and is in texture units already.
3. **A property was deleted with the thing that used to carry it.** The profile table had
   `TheReadIsClampedSoTheLayerCeilingDoesNotWrapOntoItsFloor`; the table went, and the property went with
   it instead of moving to the volume that replaced it. It needed a FIXTURE, which is why it was easy to
   lose: an ordinary layer is the union of its types' bands, so both ends of the volume are empty and a
   wrap onto the floor is invisible. The new test bakes a cumulus into the bottom eighth of an 8 km layer,
   where 767 of 2304 columns carry cloud low down and none may carry any at the very top.

**And a defect in the harness, for the second time in this programme.** The sweep deleted the
intermediates before building and not the BINARIES, so `CloudAuthored` — which could not compile at all,
its reference still calling `CloudBuildProfileTable` — ran its binary from before the change and reported
**PASSED**. That is phase A2's finding arrived at from the other side, and the lesson is the same one:
*a measurement that cannot see what it claims to measure looks exactly like a measurement that found
nothing.* Both the sweep and the break driver delete the binary now.

### The frames

| file | what it shows |
|---|---|
| `Shots/E5a_before_mid_away.png` / `E5a_after_mid_away.png` | **⬛ THE SHOW.** The same camera, before and after. Before: a deck of separate cushions, each one an Alligator cell, with a dark crease between every pair — because `best - second` is zero there and no slider can close it. After: fused convective masses, a wide flat base with turrets growing out of it, one connected surface per cloud and blue sky between clouds |
| `Shots/E5a_before_zenith_away.png` / `E5a_after_zenith_away.png` | the same pair straight up, which is the angle the extrusion defect hides at and the angle the empty-zenith defect shows at |
| `Shots/E5a_fly_frame080.png` / `E5a_fly_frame240.png` | frames 80 and 240 of a camera crossing **12 km — four snaps of the shipped 3 km lattice**. Two rebakes happened during it, at 3936 and 4010 ms, and neither is visible: no seam at the region's boundary, no pop when the volume is swapped, because the field inside the region is invariant under the scroll and the volume is periodic outside it |

## DS — the erosion re-calibrated, and the number's argument was not the one suspected, 2026-08-24

Task DS was set to re-calibrate `Detail Strength`, which had stood at 0.10 since phase T2b. The brief's
mechanism was that phase Э5 had moved the number's carrying input: that the profile used to be **low
almost everywhere**, so a shallow cut sufficed, and that the Э5 normalised distance field is **high inside
bodies**, so the same cut now does nothing.

**Measured on both producers, that is backwards, by a factor of four.** The correction is the first
finding of the task and everything else follows from it.

### The two censuses, and what they say

Both fields were walked on the same grid — 64 columns per axis over one horizontal period, 24 levels
through the envelope — at the coverage the shipped scenes carried at the time (0.24 before Э5, 0.762
after, which the Э5 phase re-authored at equal sky cover). The pre-Э5 census is a standalone program built
from `git show 3ef714c1:...` — the shader headers, the profile table and the reference of the day — so it
is the field the 0.10 was last set against, not a reconstruction. **The erosion expression itself is byte
identical between `3ef714c1` and `HEAD`,** which is what makes the two censuses comparable at all.

| profile mass by decile | 0–.1 | .1–.2 | .2–.3 | .3–.4 | .4–.5 | .5–.6 | .6–.7 | .7–.8 | .8–.9 | .9–1 |
|---|---|---|---|---|---|---|---|---|---|---|
| pre-Э5, coverage 0.24 | 0.005 | 0.016 | 0.026 | 0.037 | 0.041 | 0.074 | 0.070 | 0.059 | 0.063 | **0.608** |
| Э5, coverage 0.762 | 0.044 | 0.065 | 0.067 | 0.073 | 0.106 | 0.095 | 0.119 | 0.103 | 0.121 | **0.207** |

The old field carried **60.8 %** of its profile mass above 0.9; the new one carries **20.7 %**. The old
producer was a threshold on the Alligator times a profile table, and a thresholded cushion is a PLATEAU at
1 with a thin skirt; the Э5 field is a normalised distance field, which spends most of its volume ramping.

What the erosion takes at 0.10, as a share of the profile mass it meets:

| | pre-Э5 | Э5 |
|---|---|---|
| removed at strength 0.10 | **1.7 %** | **7.1 %** |
| removed in the edge band, profile < 0.3 | 22.7 % | 33.6 % |
| samples moved by more than one 255th | 0.491 | 0.796 |

**The cut got four times STRONGER when the producer changed, not weaker.** And the same finding arrives
from the picture without any arithmetic: `Shots/E5a_before_mid_away.png` — the pre-Э5 frame, committed by
that phase — is as smooth as the frame this task was handed. Measured with the silhouette instrument
below, the old frame's raggedness is 0.0059 against the new one's 0.0032, and that difference is the count
of separate cushions, not tornness: both are smooth blobs.

**So 0.10 never produced an edge on either producer, and no change of producer took one away.**

### What was actually wrong, and it is a relation this programme has been bitten by four times

> the erosion's own wavelength, **884 m** at the four-kilometre tile
> against a body's own chord,   **1071 m**

One wave across a whole cloud. A field that is nearly constant over a body cannot cut billows into it; it
makes that body slightly larger on one side and slightly smaller on the other, which is a smooth blob of a
different size. **On the pre-Э5 producer the same ratio was 1.08** — so this had been wrong on both
producers, for the whole life of the subsystem, and no setting of `Detail Strength` could reach it.

Measured through the seam, at the depth the eye sees a cloud:

| Detail Tile Size | erosion half-wave | full wave | vs the march's 125 m | vs a 1071 m body |
|---|---|---|---|---|
| 8 km | 754 m | 1508 m | 12.06x | 1.41 |
| **4 km (what shipped)** | 442 m | **884 m** | 7.07x | **0.83** |
| 2 km | 216 m | 433 m | 3.46x | 0.40 |
| **1 km (shipped now)** | 117 m | **235 m** | **1.88x** | **0.22** |
| 0.5 km | 59 m | 119 m | **0.95x — past the march** | 0.11 |
| 0.2 km (the slider's floor) | 28 m | 56 m | 0.45x | 0.05 |

The window is two-sided and narrow: above by the body, below by `CloudFinestResolvableChordKm`. **One
kilometre is the smallest tile that still clears the march**, and it is what ships.

### The number, and the two bounds that fix it

The frame gives a monotone trade with no knee in it, so it cannot choose a value on its own:

| Detail Strength | cloud area | silhouette raggedness | texture inside the body (lap r4) |
|---|---|---|---|
| 0.10 | 0.9203 | 0.0032 | 0.00558 |
| 0.20 | 0.9097 | 0.0034 | — |
| 0.30 | 0.9011 | 0.0036 | 0.00603 |
| 0.40 | 0.8937 | 0.0038 | — |
| 0.50 | 0.8868 | 0.0039 | 0.00645 |
| 0.70 | 0.8756 | 0.0042 | — |
| 1.00 | 0.8617 | 0.0047 | 0.00727 |

So the value is fixed by a bound instead. **The floor is measured; the ceiling is a convention and is
labelled as one.**

**BELOW, BY THE MARCH.** For every column, the altitude at which a ray's optical depth first reaches 1 —
where the eye puts the surface — with the erosion on and off. The difference is what the setting buys, and
it has to exceed the chord the march can be relied on to find, or the structure carved is finer than the
renderer represents:

| Detail Strength | surface travel | opaque columns dissolved |
|---|---|---|
| 0.10 | **53.5 m** — 0.43x the march | 0.022 |
| 0.20 | 87.2 m | 0.038 |
| 0.30 | 113.5 m | 0.053 |
| 0.35 | 126.0 m | 0.056 |
| **0.40** | **139.1 m — 1.11x the march** | **0.060** |
| 0.50 | 161.8 m | 0.070 |
| 0.80 | 217.2 m | 0.099 |
| 1.00 | 256.7 m | 0.119 |

**ABOVE, BY A CONVENTION.** The floor does not fix the value on its own — 0.35 clears it by ONE metre and
0.40 by fourteen. A default is set with headroom over its floor rather than balanced on it: a one-per-cent
margin would make the suite fail on any change to the generator that moved a body by a voxel. The shipped
value is the first step with real headroom, and the suite asserts only that it stays within an octave of
the floor, which is what stops it drifting upward unnoticed.

### THE CEILING NOBODY CAN RAISE, and it is why this is a calibration and not a cure

The ray sees a cloud at a **profile of 0.694** — that is where the optical depth first reaches 1 — and the
erosion's own weight there is `1 - 0.694 = 0.306`. **Even at the top of the slider only 31 % of the
nominal depth reaches the surface the eye is looking at.** No setting of this number produces a shredded
silhouette; the frames at 1.00 still show smooth pills in the far field.

That ceiling is a property of the `(1 - Profile)` weight in `Common/CloudField.glslh`, it **predates phase
Э5**, and it is protected by a shipped test (`TheCoreKeepsItsDensityAndTheEdgeLosesMostOfItAtFullErosion
Strength`) that exists for a good reason. Re-deriving the weight against the OPTICAL surface rather than
against the geometric one is a design change with its own frames to shoot, and this task did not shoot
them. **It is recorded here rather than attempted.**

### What raising the layer cost the LIBRARY, which is the same defect one level down

The cut's depth is `clamp(DetailStrength * DetailFactor, 0, 1)`. Every type's factor was authored against
a layer of 0.10, so raising the layer to 0.40 multiplies **every** type's cut by four. Two types were
authored above 1 and both dissolved. Measured against a CLOUDLESS frame at the same camera, as mean
`|dLuma|` — the type's own contribution to the picture:

| type | factor | effective cut, 0.10 -> 0.40 | its contribution | share of un-eroded |
|---|---|---|---|---|
| cirrus, un-eroded | 2.50 | 0 | 0.00770 | 100 % |
| cirrus at the old layer | 2.50 | 0.25 | 0.00256 | 33.3 % |
| **cirrus at the new layer** | 2.50 | **1.00** | **0.00033** | **4.3 %** |
| altocumulus, un-eroded | 1.60 | 0 | 0.09578 | 100 % |
| altocumulus at the old layer | 1.60 | 0.16 | 0.04933 | 51.5 % |
| **altocumulus at the new layer** | 1.60 | **0.64** | **0.00720** | **7.5 %** |

Both are half the density of a cumulus, so a deep cut does not shred them — it deletes them. **There is no
layer value that serves both ends**: the march needs 0.40 for a type of factor 1, and altocumulus needs
0.10 to keep half of itself. So the factors were **re-based** — cirrus 2.50 -> **0.625**, altocumulus
1.60 -> **0.40**.

#### What the re-base restores, and what it does NOT — the correction that matters most in this section

**The re-base restores the DEPTH of the cut and nothing else, and the word "exactly" belongs to the depth
alone.** An earlier draft of this section claimed the two types "render BYTE FOR BYTE what they rendered
at the old layer strength". That claim is FALSE of the shipped configuration and the archive says so: the
md5 of `Shots/DS_cirrus_before.png` and `Shots/DS_cirrus_shipped.png` differ. The `cmp` behind the claim
had been run with the tile held at one kilometre in both arms — which isolates one variable honestly, but
is a configuration that does not ship.

The reason is a seam between two scopes, and it is worth naming because it is the same shape as the defect
this whole task is about:

> `Detail Tile Size` is a property of the **LAYER**. `Detail Factor` is a property of the **TYPE**.
> Re-basing the factor can restore a type's cut DEPTH; it cannot restore the SCALE of the field that cut
> is taken from, because the scale moved for all nine types at once.

So the shipped cirrus and altocumulus meet an erosion **four times finer** than anything their files were
authored against. What that costs, measured:

| | depth restored? | old shipped vs shipped now | its contribution to the frame |
|---|---|---|---|
| cirrus | yes, `0.40 x 0.625 = 0.10 x 2.50` | mean luminance delta **0.00076**, 21.7 % of pixels, max **10**/255 | 0.00257 -> **0.00256**, −0.4 % |
| altocumulus | yes, `0.40 x 0.40 = 0.10 x 1.60` | mean luminance delta **0.00510**, 74.4 % of pixels, max **16**/255 | 0.04879 -> **0.04933**, +1.1 % |

**How much of each type is in the sky is preserved to within one per cent; where its material sits is
not.** That is the honest statement, and it is the one an artist needs: the finer tile redistributes a
cirrus into shorter, more broken fibres and an altocumulus into smaller lobes, at the same total amount of
cloud. `Shots/DS_cirrus_before.png` against `Shots/DS_cirrus_shipped.png` is exactly that change and
nothing else, because the depth is identical between those two frames — it is the picture of what moving
the layer's tile does to a type that did not ask for it.

**And the arithmetic of the re-base IS verified byte for byte, on the shipped library, with the one
variable it is about held alone.** Driving the layer to 0.40 against the re-based files AT THE OLD TILE
reproduces the old shipped frame exactly (`cmp`, not a pixel diff), for both types. That is what proves
`0.40 x 0.625 = 0.10 x 2.50` survives the whole renderer, and it is all it proves.

`Desert/Tests/Engine/CloudType` asserts what follows: no shipped type may be cut deeper than the reference
congestus whose factor is 1 by definition.

#### THE TOP OF THE CIRRUS' RANGE IS GONE, AND THAT IS A DECISION RATHER THAN A SIDE EFFECT

Before the re-base a cirrus reached the clamp — an effective cut of 1.0, the deepest the maths allows — at
a layer strength of 0.40, so an artist could drive the wispiest type in the library to the deepest cut
there is. With a factor of 0.625 the deepest it can reach at the TOP of the layer's slider is 0.625. The
type that is shredded by definition has lost the top of its range.

**That range was not usable and the measurement is above: at an effective cut of 1.0 the cirrus keeps
4.3 % of its contribution to the frame.** The top of that range did not produce a more ragged cirrus, it
produced no cirrus — a control whose top end deletes the thing it controls, which is the contract's §1.3
complaint arrived at from the other side. Removing range that renders nothing is not a loss.

**What an artist who asks for a more ragged cirrus actually needs is a body that can carry a deeper cut.**
The type's own `DensityFactor` is 0.35 and its `ExtinctionFactor` 0.25 — it is thin on purpose, and thin
is what the erosion dissolves. The path is to raise the density first and the factor after; `Detail
Factor`'s own slider runs to 8, so the range exists once the body can hold it. That is content work with
its own frames, and it is recorded here so the request is met with this measurement rather than with a
surprise. Note also that it does not escape the ceiling above it: the ray sees a cloud at a profile of
0.694 whatever the type, so the `(1 - Profile)` weight still delivers 31 % of the nominal depth to the
surface.

### The six points

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, 1280x766, ImageStat over `0 0 1280 551`.

**The baseline was re-shot rather than quoted, and it reproduces the owner's table on all six points to
three decimals** — 0.570/0.449/0.135, 0.596/0.308/0.077, 0.628/0.240/0.071, 0.662/0.407/0.037,
0.614/0.320/0.089, 0.638/0.287/0.086. The first render in this worktree was discarded before it (§A1's
correction).

**The repeat floor is zero and was measured, not assumed:** with the two numbers authored back into the
scene at their old values, the REBUILT editor renders `mid_away` **byte for byte** identical to the
baseline. So everything in the table below is the two numbers and nothing else.

| point | mean before / after | contrast before / after | sat before / after |
|---|---|---|---|
| zenith away `0,0.9,-1` | 0.570 / **0.549** | 0.449 / **0.450** | 0.135 / **0.164** |
| mid away `0,0.45,-1` | 0.596 / **0.591** | 0.308 / **0.384** | 0.077 / **0.087** |
| horizon away `0,0.12,-1` | 0.628 / **0.630** | 0.240 / **0.281** | 0.071 / **0.073** |
| zenith sunward `0,0.9,1` | 0.662 / **0.680** | 0.407 / **0.404** | 0.037 / **0.033** |
| mid sunward `0,0.45,1` | 0.614 / **0.614** | 0.320 / **0.327** | 0.089 / **0.095** |
| horizon sunward `0,0.12,1` | 0.638 / **0.641** | 0.287 / **0.298** | 0.086 / **0.089** |

Contrast rises at five of six and the mean barely moves, which is what a cut that lands on the silhouette
and not on the body predicts. The silhouette instrument says it more directly — raggedness is
`perimeter / sqrt(area)` over the cloud/sky boundary, and `lap r4` is the mean absolute Laplacian of
luminance INSIDE cloud pixels at a billow's radius:

| point | area before / after | raggedness before / after | lap r4 before / after |
|---|---|---|---|
| zenith away | 0.8209 / 0.7601 | 0.0046 / **0.0058** | 0.00530 / **0.00618** |
| mid away | 0.9203 / 0.8913 | 0.0032 / **0.0038** | 0.00558 / **0.00636** |
| horizon away | 0.9560 / 0.9386 | 0.0040 / **0.0058** | 0.02512 / **0.02816** |
| mid sunward | 0.8668 / 0.8361 | 0.0043 / **0.0054** | 0.00636 / **0.00766** |
| horizon sunward | 0.8930 / 0.8718 | 0.0045 / **0.0060** | 0.03005 / **0.03454** |

**Raggedness up at every point, texture inside the body up at every point, area down by 2 to 7 %.** The
edge gained and the body was not eaten.

`LineJump` over `2 2 1278 551` finds nothing new. Away from the ground line every row maximum sits at
0.0013–0.0035 and every column maximum at 0.0024–0.0067, against the 0.010 that means "something to look
at"; the largest column figure (0.00668 at x 527, zenith away) is BELOW its own before-value of 0.00767.
The one large number — 0.098 at y 540 on both horizon frames — is the checker floor's own edge, and it
moves by 0.0002 between before and after.

### The frames

| file | what it shows |
|---|---|
| `Shots/DS_before_zenith_away.png` / `DS_after_zenith_away.png` | **⬛ THE SHOW.** Before: soft featureless blobs with no edge anywhere. After: scalloped silhouettes with cauliflower lobes on them, and the bodies are still solid |
| `Shots/DS_before_mid_away.png` / `DS_after_mid_away.png` | the same pair at the elevation a player looks at |
| `Shots/DS_before_horizon_away.png` / `DS_after_horizon_away.png` | the far field, which is where a finer erosion would show as dither if the tile had gone below the march's chord. It does not: the band at the vanishing point is clean |
| `Shots/DS_cirrus_before.png` / `DS_cirrus_dissolved.png` / `DS_cirrus_shipped.png` | **the break, and what the fix does and does not restore.** The middle frame is the new layer strength against the factor the file was authored with — the sky is very nearly empty, 4.3 % of the type left. The third is what ships. The FIRST and THIRD differ by the layer's tile ALONE, because the re-base makes their cut depths identical: that pair is the picture of a type meeting an erosion four times finer than its file was authored against — the same amount of cirrus, redistributed into shorter fibres |
| `Shots/DS_altocumulus_before.png` / `DS_altocumulus_shipped.png` | the second re-based type, the same comparison: cut depth identical, tile four times finer, 74.4 % of pixels moved by at most 16 of 255 and the type's contribution to the frame up 1.1 % |
| `Shots/DS_stratocumulus_before.png` / `DS_stratocumulus_after.png` | the type that gains the most: a soft smear becomes individual cloud elements |
| `Shots/DS_congestus_before.png` / `DS_congestus_after.png` | the reference type, at `0,0.65,-1` — **an elevation deliberately outside the six protocol points**, so that it is a third per-type frame rather than a protocol frame under a second name. An earlier draft shipped it as a byte copy of `DS_*_mid_away.png`, which carried no information the protocol did not already carry |

### The relations added, and the breaks that verified them

Nine sabotages, **eight red and one green**. Every suite was rebuilt from deleted OBJECTS *and* a deleted
BINARY first.

| break | result |
|---|---|
| the tile goes back to the 4 km that shipped | RED |
| the tile goes to 0.4 km, past the march's chord | RED |
| the strength goes back to the 0.10 that shipped | RED |
| the strength goes to the top of the slider | RED |
| the erosion is applied uniformly instead of by depth | RED (two tests) |
| the cirrus keeps the factor it was authored with | RED |
| the shipped default drifts by one step | RED |
| the billowy pair's fourth-root blend becomes linear | RED |
| the wispy pair's frequency blend is reversed | **GREEN — a real hole, closed** |

**The green one is the finding.** The erosion composite mixes a WISPY pair and a BILLOWY pair and then
blends the two on `DetailType`. The built-in congestus every fixture in the suite is built on has a
`DetailCharacter` of **1.00** — the billowy end EXACTLY — so the wispy pair multiplies out of the
expression entirely. **Two of the noise volume's four channels were never read by any assertion in this
programme.** The mirror test sweeps the character over 0, 0.35, 0.7 and 1 now; re-run, the same sabotage
is RED.

### What this task did NOT do, and why it is here rather than in a commit message

* **It did not produce a shredded silhouette,** and no setting of `Detail Strength` can. The reason is
  measured and stated above: the surface the eye sees sits at a profile of 0.694 and the erosion's weight
  there is 0.306. The next phase's question is whether `(1 - Profile)` should be re-derived against the
  optical surface, and it should be asked with frames.
* **It did not raise the slider's own floor** on `Detail Tile Size`, which at 200 m permits settings 0.45x
  past the march's chord. That is an artist's knob and the bound depends on Max Steps, so it is asserted
  as a RELATION in the suite rather than frozen into a range.
* **It did not re-author the library beyond the two factors the measurement showed dissolved.** A cirrus
  that should be MORE eroded than a cumulus needs the density to carry it first.

## GT — the four cost decisions re-measured per pass, and one of them measured the wrong thing, 2026-08-25

Every cost decision in this document above was taken on the **frame-count slope**,
`(t900 - t300) / 600`. The method is honest and its discipline was kept — configurations interleaved in
one session, minimum of N, spread named. But it measures **the whole frame**, and it cannot say which
pass the milliseconds are in.

GPU timestamps now can (`Docs/GPU_TIMESTAMPS.md`; `DESERT_PROFILE_PASS`, off by default, `--gpu-profile`
to arm). What follows are **corrections**, and each one names what the old number was actually measuring
rather than quietly replacing a digit. `Clouds_Demo` at High, camera `0,200,0`, `--look 0,0.45,1` — the
same framing the originals used, so the two are comparable. Debug, MoltenVK, machine shared.

### The frame, itemised

| pass | gpu self ms | share of an UNINSTRUMENTED frame |
|---|---|---|
| **Clouds: March** | **7.589** | 41 % |
| **Clouds: ShadowMap** | **3.796** | 20 % |
| Clouds: TemporalResolve | 0.502 | 2.7 % |
| everything else marked | 4.7 | 25 % |
| **cloud subsystem, total** | **12.15–12.46** | **77 %** (75.5–77.8) |

**Two denominators, and they answer different questions.** The instrument inflates the frame it measures
by ~8 %, so clouds are **71 %** of the instrumented frame and **77 %** of the uninstrumented one. **77 %
is the number a budget is set against** — the share of a frame the player actually gets. Quoting one
without the other is how the same measurement reads as two different numbers.

### Correction 1 — the shadow ray's ×1.87 measured the ratio of two whole frames

The OE-FIX table above records **1.87x** and **0.230 ms per sample** for `LightMarchSamples` 6 → 32.

Sweeping the same field from a scene copy (one field, no rebuild) and reading the **march's own line**,
three interleaved passes, minimum of three:

| samples | Clouds: March | GPU frame |
|---|---|---|
| 6 | 2.057 ms | 12.732 ms |
| 16 | 3.754 ms | 15.019 ms |
| **32 (shipped)** | **6.911 ms** | 17.047 ms |
| 64 | 13.071 ms | 22.161 ms |

* The march is linear at **0.190 ms/sample** over a **0.918 ms** fixed part (per-interval 0.170 / 0.197 /
  0.193). The recorded 0.230 is **17 % high**.
* 6 → 32 costs **+4.85 ms on the march** and **+4.32 ms on the whole frame — a ratio of 1.34x**, against
  the recorded 1.87x.

**What the old number measured:** a ratio of two *whole frames*, in a tree whose frame was 6.95 ms at 6
samples. Today the same frame is 12.73 ms, because everything else in it has grown. A ratio whose
denominator is mostly the part you did not change is not a property of the part you did — the identical
code change gives 1.87x on one tree and 1.34x on another. **Only the absolute per-sample cost transfers
between trees**, and that one is 17 % off.

The shadow map is unaffected by this knob (3.977 / 3.667 / 3.656 / 3.705 across the four counts), so the
sweep isolates the march cleanly.

### Correction 2 — the cloud shadow map's +4.92 ms is ~20 % high

Reproducing this document's own A/B (`CastShadows` on/off, one field, no rebuild) but reading the
breakdown, three interleaved passes, minimum of three:

| | gpu self, ON | gpu self, OFF | delta |
|---|---|---|---|
| Clouds: ShadowMap | 3.686 | 0.014 | **+3.672** |
| Clouds: March | 6.553 | 6.670 | −0.117 |
| **whole GPU frame** | **16.862** | **12.884** | **+3.978 (1.31x)** |

Against the recorded **+4.92 ms, 1.38x**. Pooling every run in the session — 17 of them — the map's own
line is **3.66–4.80 ms, median 3.96**; the recorded 4.92 sits **above the entire range**.

**What the old number measured:** a whole-frame delta, on the same instrument and with the same drift as
correction 1. The conclusion it was used for survives — the map really is ~20 % of the frame and really
is the second-largest line — but the digit is optimistic by a fifth.

**A hypothesis, tested and disproved:** that the missing millisecond was the march paying to *sample* the
map. It is not. The march is 6.553 ms with shadows and 6.670 ms without — a difference of −0.117 ms,
inside its own 15 % run-to-run spread. **Building the map is the whole cost; sampling it is free.**

### Correction 3 — the tier ladder's conclusion survives its constants

The tier table records 17.99 / 14.24 / 8.61 ms. Those are whole-frame slopes from the same instrument and
should be expected to carry the same error as the two corrections above.

The conclusion, however, is **confirmed by the itemisation**: the two knobs the ladder turns — shadow-ray
sample count and shadow-map resolution — are exactly the two largest lines in the frame, 41 % and 20 %.
The ladder was built by distributing a budget nobody had ever seen itemised, and the itemisation says it
reached for the right two knobs.

### Not re-measured

**The hero clouds' 1.39x on eight instances** belongs to `Clouds_HeroMass`, not `Clouds_Demo`, and was
left alone. It rests on the same whole-frame instrument as the three above and deserves the same
re-check; it is filed as its own task.

### What the instrument costs, so the next reader does not repeat the mistake

**+1.244 ms/frame**, and it is *all* the per-pass marks — the two-timestamp frame bracket is free:

| configuration | GPU frame, min |
|---|---|
| full per-pass marking (~80 timestamps) | 17.106 ms |
| frame bracket only (2 timestamps) | 15.862 ms |

On MoltenVK a `vkCmdWriteTimestamp` becomes a Metal counter sample, which can force an encoder boundary.
That is why it is **off by default**: an instrument that inflates its subject by 8 % must not be running
when nobody asked, or every number taken afterwards carries the tax and someone eventually compares an
instrumented figure against an uninstrumented one.

The cost lands **between** the passes, not inside them — with both marks at `BOTTOM_OF_PIPE` the encoder
split falls in the gap between one pass's end mark and the next one's begin mark. So the per-pass figures
above are trustworthy and only their **sum** is inflated. This was checked rather than assumed: the
unmarked remainder in the full configuration is 1.137–1.438 ms and the marks cost 1.244 ms, which are the
same number.
