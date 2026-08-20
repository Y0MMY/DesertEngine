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
