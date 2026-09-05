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
---

## RW — the sky was on a 3 km grid, and the cause was the confinement rather than the count, 2026-08-25

The owner of the product looked at the sky and said the clouds **"just go in a row"**, that the whole sky
was cloud **"with an obvious pattern"**, and asked whether there is **a parameter to do this by hand**.

### Neither instrument this programme owns can see a grid, so one was built first

`ImageStat` measures the DISTRIBUTION of luminance: a grid of clouds and a natural field of the same clouds
have the same histogram. `LineJump` measures the STEP between adjacent rows and columns: a period longer
than one row makes no step at all. **Both are satisfied by a perfect lattice**, so "it got better" would
have been an opinion, and an opinion cannot be reviewed.

`Tools/LatticePeak` takes the **autocorrelation** of a coverage field and reports the **PROMINENCE** of a
bump — its height above the higher of the two troughs bracketing it — which is exactly zero for any
decaying curve whatever that curve's height. It has two modes:

* `--field` bakes the placement through the shipped generator, projects it down and measures it. It PREDICTS
  the period it expects from the generator's own `CloudProceduralCellExtentKm` — which was promoted out of
  an anonymous namespace for exactly this, so the tool cannot check its own multiplication — and prints the
  prediction beside the measurement.
* `--frame` measures a rendered PNG's cloud mask, split at Otsu's threshold so the split is a property of
  the image rather than of the operator.

**Two things the instrument taught its author before it measured anything, and both are in it now.**

1. **One region is sixteen cells across, and an autocorrelation estimated from sixteen things wobbles by
   about a quarter of its own scale.** The very first run reported a "peak" of prominence **0.085 at
   9.5 km**, and there is no lattice at 9.5 km. Hence `--repeats`, which averages the curve over disjoint
   regions, and a **jackknife noise** figure on every line: half the difference between two halves of the
   realisations, which cancels everything they agree about and leaves what they do not.
2. **The background must NOT be the median prominence of the curve's own local maxima.** That was the first
   statistic tried, and on a strongly periodic curve nearly every local maximum IS a lattice harmonic — so
   it measures the signal and reports a floor of 0.05 that never falls however many realisations are
   averaged. It is estimated from the realisations instead.

### The instrument checked against a period this file chose

`Desert/Tests/Engine/CloudPlacementSpectrum` compiles `Tools/LatticePeak/Source/LatticePeakMath.hpp` — the
same trick the `.glslh` suites use on shader maths, arrived at from the other side — and feeds it fields
whose answer is not in question:

| field | verdict |
|---|---|
| a curve that only decays | **no peak at all**, which is what makes prominence the right quantity |
| a bump of 0.20 planted at lag 40 on a decay | found at 40, prominence within 0.005 of the height above its own trough |
| a perfect lattice of period 16 | bump at 16, prominence **1.108** |
| the same bodies placed by a hash over the whole map | prominence **0.0045** |
| two identical realisations | noise **0** |

### WHAT THE INSTRUMENT SAID ABOUT THE SHIPPED SKY

`Clouds_Demo` as it shipped: 48 km region, 12 km weather tile (3 km cells), one cumulus congestus,
coverage 0.762, wind along +X. **32 realisations.**

| axis | 1P (3.000 km) | 2P (6.000 km) | 3P (9.000 km) | 4P (12.000 km) | noise |
|---|---|---|---|---|---|
| X (east) | 3.375 km, prom 0.0152, 2.4x | 6.000 km, **0.0541, 8.6x** | 9.000 km, **0.0491, 7.8x** | 12.000 km, **0.0593, 9.4x** | 0.0063 |
| Z (north) | 3.188 km, prom 0.0073, 2.1x | 6.000 km, **0.0600, 16.9x** | 9.000 km, **0.0644, 18.1x** | 12.000 km, **0.0626, 17.6x** | 0.0036 |

**The bumps land on the predicted multiples to the voxel.** The prediction came from the generator and the
measurement from the volume, and they agree — which is the check that makes every other number here mean
something. The owner was right, and the period is the weather tile's own quarter.

**THE FIRST HARMONIC IS THE WEAK ONE, and an instrument that asked only about P would have called this sky
clean.** A cluster at the shipped size is 2.16 km wide against a 3.00 km cell, so a neighbour one period
away is still inside the body's own correlation lobe and the bump at 1P rides on its tail. That is why
`LatticeScore` asks about four multiples rather than one.

### THE CAUSE IS NOT THE ONE THE SHAPE OF THE DEFECT SUGGESTS

Four mechanisms were implemented behind four numbers and each was measured ALONE, on the same binary, at
16 realisations, changing nothing else:

| arm | LATTICE X | LATTICE Z | sky cover |
|---|---|---|---|
| all four at their old values | 0.0514 | 0.0549 | 0.8186 |
| **more clouds per cell** (density 1 -> 2.5) | **0.1504** | **0.1404** | 0.7446 |
| **the cloud may leave its cell** (scatter 0.66 -> 1.0) | **0.0117** | **0.0328** | 0.7794 |
| **clouds differ in size** (variety 0 -> 0.75) | 0.0344 | 0.0296 | 0.8061 |
| **busy and clear regions** (patch 0 -> 0.6) | 0.0528 | 0.0449 | 0.7948 |
| **all four, which is what ships** | **0.0000** | **0.0000** | 0.7446 |

> **Raising the number of clouds per cell, on its own, makes the grid THREE TIMES STRONGER.**

That is the finding, and it is the opposite of what "one per cell" suggests. Several small clouds crowded
into the middle third of a cell mark that cell's site MORE sharply than one large one did: the cluster is
no longer a smooth body wide enough to blur its own site, it is a knot of small bodies at it.

**What removes the lattice is letting a cloud leave the cell whose hash made it** — a factor of 4.4 on X
and 1.7 on Z, on that change alone. The size spread is second and worth about a factor of 1.6. The patch
modulation does nothing to the lattice at all (0.0528 against 0.0514), which is correct and expected: it
works at 21 km, not at 3 km, and it is in the sky for the OTHER half of the complaint.

### The invariance the old bound claimed to protect is not the one the wrap needs

The jitter was bounded at a third of a cell, and the comment said why: so that a cluster stays in the cell
whose hash produced it. Breaking that bound is the whole cure, so the argument had to be re-examined rather
than dropped.

> **What the bake actually requires is that exactly ONE PERIOD of cells is generated** — the comment above
> the cell loop says so, and it is right: the bake splats every lump at plus and minus one region, so
> generating a neighbouring cell as well would place it twice.
>
> **That requirement says nothing about where inside the period a cluster sits.** A cluster displaced out
> through one face of the region arrives back through the opposite one, because the wrap is what makes the
> volume periodic in the first place. The periodicity is measured and unchanged: the mean step across the
> wrap is **0.950/255 against 1.239/255 between ordinary neighbours** — a seam smaller than the ordinary
> texture of the field, which is the assertion `CloudProceduralField` already carried.

What the bound really bought is narrower and is now written on the knob: **a region SHIFT disturbs the sky
only within a third of a cell of the region's faces**, which is 24 km from the camera. At a scatter of 1.0
that strip goes from 1.0 km to 1.5 km at the same distance. The scroll-invariance test still passes with
its 4 km margin: **1298 of 1800 lumps lie in the overlap of two regions one snap apart, and every one of
them is unmoved.**

### The coverage slider had to be PAID FOR rather than re-authorised

Free placement packs worse than a jittered lattice — independently placed bodies overlap where a lattice
keeps them apart. Measured at the shipped setting, the sky fell from **0.781 to 0.640**, which would have
failed `CloudProceduralField`'s tenth at a coverage of 0.75.

**A packing law derived from first principles was tried and rejected BY MEASUREMENT.** Independently placed
bodies of area `a` at density `n` leave `exp(-n a)` of the sky uncovered, so the alive fraction wanted is
`-ln(1-c)/u` with `u` the cell-areas one cluster covers. Predicted `u = pi * 0.72^2 = 1.629`. It does not
hold, because `u` is NOT a constant: a cluster's radius rises with how deep inside the threshold its cell
fell, so `u` measured 0.545 at an alive fraction of 0.28 and 1.476 at 1.0. Driven by that law the slider
came out at **0.047 of the sky for a setting of 0.15**. It is recorded because it is the more attractive of
the two answers and it is wrong.

What ships is a **widening of the cluster linear in the coverage**, slope measured at the shipped placement:

| Coverage | 0.15 | 0.24 | 0.35 | 0.50 | 0.75 |
|---|---|---|---|---|---|
| sky delivered | 0.169 | 0.249 | 0.357 | 0.519 | 0.741 |
| out by | +0.019 | +0.009 | +0.007 | +0.019 | -0.009 |

**Out by at most 0.019, against the 0.11 the free placement was out by before it and against the tenth the
suite allows.** No scene needs re-authoring, which is decision **D-20's condition met rather than invoked**
— and the condition is stated as a number rather than as a hope. Each shipped scene's authored Coverage,
put through the new mapping:

| scene | Coverage | sky it now delivers |
|---|---|---|
| `Clouds_TwoSpecies` | 0.347 | 0.354 |
| `Clouds_HeroCloud` | 0.384 | 0.395 |
| `Clouds_Demo`, `Clouds_ShadowsOnGround` | 0.762 | 0.745 |
| `Clouds_Showcase` | 0.779 | 0.760 |
| `Clouds_Sunset` | 0.855 | 0.810 |

**The one scene measured against the shipped `dev` binary directly is `Clouds_Demo`: 0.7827 before, 0.7446
after — a fall of 0.038 of the sky at a setting of 0.762.** D-20's bar is what the previous two
recalibrations failed and this one has to clear: the frame still reads as the sky it was authored for, and
`Shots/RW_before_mid_away.png` against `Shots/RW_after_mid_away.png` is that comparison. Where the earlier
recalibration took `Clouds_Demo` from 66 per cent of the sky to 15 and produced an empty zenith, this moves
it by four points.

A second constant was needed and the suite is what found it. The density's compensation was `1/sqrt(d)` —
which preserves the total AREA of a cell's clusters exactly — and that is the answer to the wrong question,
because the ground they cover is not the sum of their areas when they overlap. One cluster covers 1.63
cell-areas and saturates its own cell; four of a quarter that area each cover 0.41 of a cell and leave 12
per cent of it open. Measured, a half took the sky from **0.701 at a density of 1 to 0.597 at 4**. The
shipped exponent is **0.40**, measured, and at it the same pair is **0.701 / 0.684**.

### The six points

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, 1280x766, `ImageStat` over `0 0 1280 551`.

**The repeat floor is zero and was measured.** Two runs of the same binary at `mid away` are byte for byte
identical — but only from the SECOND render onward: the first render in a fresh worktree differs, which is
§A1's correction reproduced exactly. The baseline also reproduces §DS's published table on all six points
to three decimals, so it is the shipped `dev` and not a local variant.

| point | mean before / after | contrast before / after | sat before / after |
|---|---|---|---|
| zenith away `0,0.9,-1` | 0.549 / **0.600** | 0.450 / **0.251** | 0.164 / **0.068** |
| mid away `0,0.45,-1` | 0.591 / **0.570** | 0.384 / **0.349** | 0.087 / **0.109** |
| horizon away `0,0.12,-1` | 0.630 / **0.639** | 0.281 / **0.281** | 0.073 / **0.071** |
| zenith sunward `0,0.9,1` | 0.680 / **0.714** | 0.404 / **0.435** | 0.033 / **0.029** |
| mid sunward `0,0.45,1` | 0.614 / **0.620** | 0.327 / **0.303** | 0.089 / **0.069** |
| horizon sunward `0,0.12,1` | 0.641 / **0.636** | 0.298 / **0.274** | 0.086 / **0.076** |

The two points that move are the two the change is FOR, and they move in OPPOSITE directions, which is the
whole content of the result.

* At `zenith away` the frame goes from a scatter of same-sized round blobs with blue between them to **one
  large body directly overhead** — contrast 0.450 to 0.251 and saturation 0.164 to 0.068, because there is
  less blue in it, and the cloud fraction of that frame rises from 0.744 to 0.946.
* At `mid away` the opposite: saturation 0.087 to 0.109 and the cloud fraction falls from 0.889 to 0.712 as
  a clear region opens.

**The direction at the zenith is worth naming rather than glossing, because at that one camera the sky got
BUSIER and the complaint was that the sky is all cloud.** It is not the coverage rising — the sky's average
cover fell from 0.781 to 0.745 — it is the size spread doing exactly what it is documented to do: a cluster
may now be 1.38 times the base width, and one that lands near the camera fills the zenith from two
kilometres below. The old sky could not produce that frame because every cloud in it was the same size,
which is the defect. An artist who does not want it has `Cloud Size Variety`, and the frame at 0 is
`Shots/RW_knob_variety_low.png`.

**And the frame mode of the instrument, on the same six.** It is the weaker measurement and the file note
says why: perspective maps one world period onto a pixel period that shrinks with distance, so a lattice
arrives smeared and the number is a lower bound rather than a size.

| point | columns before / after | rows before / after |
|---|---|---|
| zenith away | 0.0002 / **none** | 0.0185 / **none** |
| mid away | 0.0000 / 0.0009 | none / 0.0017 |
| horizon away | **0.0913 / 0.0276** | 0.0798 / **0.0535** |
| zenith sunward | 0.0344 / **0.0156** | none / 0.0000 |
| mid sunward | none / 0.0349 | 0.0020 / none |
| horizon sunward | 0.0127 / **0.0095** | 0.0142 / **0.0285** |

**Of the twelve pairs: four fall, three go from a bump to NO BUMP AT ALL, three rise, and two are pairs in
which one side has no bump to compare.** Counted honestly that is seven better and three worse, and the
three that rise are 0.0000 to 0.0009 (which is not a change), 0.0142 to 0.0285 and none to 0.0349.

The one that matters is `horizon away`, which is the frame the defect was visible in: **columns 0.0913 to
0.0276, a factor of 3.3**. The mixed result elsewhere is exactly what this mode's own caveat predicts — it
cannot separate a lattice from any other repeated feature of a perspective image — and it is why the
argument is settled in the field mode and not here.

### The price

**The placement is computed at the BAKE and not per frame, and here is that as a number rather than as an
assertion.** The A/B is authored in the SCENE — the four knobs are properties now, so the old placement and
the new one are two scene files and one binary. Interleaved, three repeats, minimum of each, Debug:

| | t(300 frames) | t(900 frames) | slope per frame | fixed cost |
|---|---|---|---|---|
| old placement | 32.398 s | 45.641 s | **22.07 ms** | 25.78 s |
| shipped | 35.705 s | 49.381 s | **22.79 ms** | 28.87 s |

* **The one-off cost is +3.09 s in Debug**, and it is the bake: 3144 lumps against 1248. `CloudProceduralField`
  re-measures it directly and reports **3060.7 / 5685.7 / 9426.5 ms** for 1 / 2 / 4 species against phase
  Э5's 1746 / 3263 / 5168, which is the same 1.75x on the same axis.
* **The per-frame cost is +0.72 ms of 22.79, which is 3.3 per cent** — and it is NOT the placement being
  computed per frame. It is the march meeting a different volume: more, smaller bodies means more silhouette
  crossings and fewer early exits.

### The knobs, and a frame for each end of each

Every one is read by `Engine/Assets/CloudProceduralVolume.cpp` at bake time, is in
`Assets::CloudProceduralParamsEqual` so that moving it invalidates the cached volume, and has a row in
`SettingConsumers` and `ComponentReflection`.

| knob | range, default | low | high |
|---|---|---|---|
| Cloud Density | 0.25 .. 8, **2.5 as this task shipped it; §RW2 moved the default to 1.75 and says why** | `Shots/RW_knob_density_low.png` | `Shots/RW_knob_density_high.png` |
| Cloud Scatter | 0 .. 4, **1.0** | `Shots/RW_knob_scatter_low.png` | `Shots/RW_knob_scatter_high.png` |
| Cloud Size Variety | 0 .. 1, **0.75** | `Shots/RW_knob_variety_low.png` | `Shots/RW_knob_variety_high.png` |
| Weather Patch Strength | 0 .. 1, **0.60** | `Shots/RW_knob_patch_off.png` | `Shots/RW_knob_patch_full.png` |
| Weather Patch Size | 5 .. 200 km, **21 km** | `Shots/RW_knob_patchtile_small.png` | `Shots/RW_knob_patchtile_large.png` |

All twelve knob frames are at one camera and differ only by the knob, and **all twelve md5s differ** — no
knob is inert. The shipped arm of that table is `Shots/RW_after_mid_away.png`, and the scene built for the
knob sweep reproduces it **byte for byte**, which is what proves the sweep's arms differ by the knob alone.

**The patch pair is shot at the HORIZON and not at the mid elevation**, because a modulation whose period
is 21 km cannot be judged in a frame that shows about that much sky. Its own number, from the field mode,
is the mid-range correlation the modulation puts back into the field:

| lag | 3.0 km | 4.5 km | 6.0 km | 8.0 km | 9.0 km |
|---|---|---|---|---|---|
| patch 0 | +0.003 | +0.006 | -0.011 | -0.003 | +0.003 |
| patch 1 | +0.045 | +0.045 | +0.027 | +0.018 | +0.014 |

### A SECOND DEFECT, MEASURED AND NOT FIXED, and it is the same slider lying for a different reason

`CloudProceduralCellExtentKm` holds the cell's AREA constant under anisotropy — `cell * root` by
`cell / root` — and its comment says this is "so that raising the anisotropy draws a cluster out into a band
instead of making the sky emptier". **The comment is false, and the cluster is what makes it false:** the
cluster's radius is `0.72 * min(extent.x, extent.y)`, so an anisotropic cell gets a cluster sized by its
SHORT side and the sky empties as the square of the stretch.

Measured at Coverage 0.5, one species, everything else shipped:

| Placement Anisotropy | cell | sky cover | slider out by |
|---|---|---|---|
| 1.0 (congestus, humilis, mediocris, cumulonimbus, stratus) | 3.000 x 3.000 km | 0.519 | +0.019 |
| 1.6 (stratocumulus; altocumulus is 1.5) | 3.795 x 2.372 km | 0.378 | **-0.122** |
| 0.2 (lenticular) | 1.342 x 6.708 km | 0.143 | **-0.357** |
| 8.0 (cirrus) | 8.485 x 1.061 km | 0.089 | **-0.411** |

**Four of the nine shipped types are affected, and a cirrus layer delivers a fifth of the sky its slider
asks for.** It also makes the row WORSE where it applies: on the old placement, anisotropy 4 took the
lattice bump along the wind from 0.066 to **0.134 at 32.5x noise** — twice as strong, and on one axis only,
which is the literal reading of "the clouds go in a row".

**It is reported rather than fixed, and the reason is scope rather than difficulty.** The cure is to size
the cluster by the cell's geometric mean and to stretch its lobes along the wind with a rotation, which
changes the silhouette of four shipped types — and phase A3 measures every type with a frame per genus
(§A2 "form by form"). That is a re-calibration of the catalogue with its own ten frames, and this task did
not shoot them. `Clouds_Demo`, the scene the owner was looking at and the scene of this protocol, uses the
congestus at anisotropy 1.0, so nothing here rests on it.

### The frames

| file | what it shows |
|---|---|
| `Shots/RW_before_horizon_away.png` / `RW_after_horizon_away.png` | **THE SHOW.** Before: a carpet of identical lozenges, one size, evenly spaced, receding to the vanishing point. After: clouds of visibly different sizes in groups with gaps between them |
| `Shots/RW_before_mid_away.png` / `RW_after_mid_away.png` | the same pair at the elevation a player looks at |
| `Shots/RW_before_zenith_away.png` / `RW_after_zenith_away.png` | the zenith, which the patch modulation changes most: a mixed sky becomes one overhead mass with clear sky beside it |
| `Shots/RW_before_*_sun.png` / `RW_after_*_sun.png` | the three sunward points, which the placement had to not break |
| `Shots/RW_field_before.png` / `RW_field_after.png` | **the instrument's own view** — the top-down column integral of the baked volume, 48 km across, which is what the autocorrelation is taken of. The grid is visible by eye in the first and the size spread in the second |
| `Shots/RW_field_patch_off.png` / `RW_field_patch_full.png` | the patch modulation on the same view, where its 21 km period fits |
| `Shots/RW_knob_placement_old.png` | the four knobs returned to their old values under the new coverage mapping — the row, back |
| `Shots/RW_knob_*.png` | the ten ends of the five knobs |

### The whole sweep, and the counts the contract asks for

`CI=true premake5 gmake`, then every generated makefile built and run with the OBJECTS AND THE BINARIES
DELETED FIRST.

| | |
|---|---|
| makefiles generated | **79** |
| excluded as tools and libraries | **16** |
| suite makefiles | **63** |
| suite binaries built | **63** |
| missing binaries | **none** |
| suites failed | **1**, and it was ours |

**The count balances exactly, and it balances only because the exclusion list was extended for this run.**
§2.4 item 5a records that the list had gone stale three times and that "makefiles against binaries" then
failed to reconcile by two. `Tools/LatticePeak` is the fourth tool of that kind — it lands in
`build/Bin/Debug/` and links no gtest — and with it excluded the audit is 63 against 63 with nothing
unexplained.

> ⚠️ **THE ONE-LINER IN `DEV_CONTRACT.md` §2.4 ITEM 5A IS NOW STALE FOR THE FOURTH TIME**, and this task did
> not edit it: that file is the teamlead's and §1.6 says a foreign file is asked for rather than taken. The
> change needed is one word — `LatticePeak` beside `ImageStat|LineJump|SceneMigrator` in the `case` — and
> without it the next developer's sweep will try to run a tool as a suite and count one binary short.

The one failure was `ComponentReflection.ExposesExactlyTheSpecifiedFieldsInOrder`: **45 fields against the
40 the assertion named.** That is the third of the three failures §2.3.1 tells a developer to expect after
adding a reflected field, and it is fixed by the truth (45, plus a new `Placement` category of 5) rather
than by deleting the assertion.

### Twelve sabotages, and TWO OF THEM STAYED GREEN — both holes are closed

Every one was applied, the affected suites' **objects and binaries deleted**, rebuilt, run, and reverted.

| break | result |
|---|---|
| the scatter's default returns to the old 0.66 | RED |
| the generator ignores `PlacementScatter` and hard-codes the old third of a cell | RED |
| the density compensation returns to one over the square root | RED |
| the packing compensation is deleted | **GREEN — a real hole, closed** |
| the size draw becomes uniform in RADIUS instead of in area | RED |
| `CloudProceduralParamsEqual` drops `PatchStrength` | RED |
| the autocorrelation's lag is off by one | RED |
| the prominence becomes the plain height of the bump | RED |
| the patch's three-cell bound is removed | RED |
| the cluster count rounds instead of drawing its fraction | RED |
| the patch modulation only ever ADDS cloud instead of being symmetric | RED |
| the anvil's width stops following its own cluster | **GREEN — a real hole, closed** |

**THE FIRST GREEN.** Deleting the packing compensation — the whole reason the Coverage slider still means
the sky after the placement was freed — left the entire repository green.
`CloudProceduralField.CoverageIsTheFractionOfSkyThatHasCloudInTheColumn` allows a TENTH, a bound set for a
placement that kept every cluster near its site, and it measures a fixture whose cell is finer than the
shipped one: its worst deviation went from 0.033 to 0.050 and a tenth swallowed both. The relation is now
asserted where it bites — at the shipped 3 km cell, near the top of the slider, on the real bake, at a
bound of 0.025 — and the same sabotage is RED.

**THE SECOND GREEN, and it is the more interesting of the two.** The anvil is the one lump whose width is
written a SECOND TIME rather than derived from the cluster's radius, which is the two-places-that-must-agree
shape §2.3.1 is entirely about. Deleting the cluster's size, density and packing factors from that line left
every suite in the repository green, because **only the cumulonimbus has an anvil at all and no suite baked
one procedurally and looked at it**. What is asserted now is the ratio between the anvil and the tower it
caps, which cannot depend on the density: 3.169 at a density of 1 against 3.217 at 4. Re-run, the sabotage
is RED.

**A third thing the sabotage runs taught, and it is about method rather than about clouds.** A sabotage
script that reverts the SOURCE leaves the sabotaged BINARY on disk. Running the suite straight after the
last revert reported a failure that no longer existed in any file — the same stale-binary trap that has
reported PASSED four times in this programme, arrived at from the opposite direction. Every number above is
from a binary rebuilt after the revert.

### What this task did NOT do, and why it is here rather than in a commit message

* **It did not fix the anisotropy defect** measured above, and that is the largest single thing left: four
  of the nine shipped types make the Coverage slider lie by up to 0.41 of the sky. It is a re-calibration of
  the form catalogue with a frame per genus, and this task did not shoot them.
* **The degenerate configuration is not byte-compatible with the sky that shipped.** Setting the four knobs
  back to their old values gives a statistically identical field, not the identical one: each cluster now
  hangs off its own sub-hash of the cell rather than off the cell's hash directly, which is what lets a cell
  hold more than one. `Shots/RW_knob_placement_old.png` is that sky, and it is a different realisation of
  the same lattice.
* **It did not touch the march, the erosion or the lighting.** The per-frame cost moved by 0.72 ms and the
  reason is measured — a different volume, not different work per sample — but no shader was edited.
* **The frame mode of `LatticePeak` is a lower bound and is documented as one.** A mode that undid the
  perspective — measuring the mask in the ground plane through the camera's inverse projection — would give
  a number comparable with the field mode's. It is not needed to settle this task and was not built.

---

## RW2 — three questions about §RW's defaults, answered with numbers, 2026-08-25

§RW removed the grid and the number that says so is `LATTICE 0.0000` on both axes. Looking at the frames it
delivered, the teamlead asked three questions the lattice peak structurally cannot answer, because **the
autocorrelation says where the bodies are and nothing at all about what they are**: two skies, one of towers
and one of plates standing on the same footprints, give the same curve and the same prominence.

1. Is a `Cloud Density` of **2.5** the right shipped default, or does a lower one hold the lattice inside
   the noise without covering the sky in identical lozenges?
2. Is the flooded zenith of `Shots/RW_after_zenith_away.png` a camera that happened to stand under a cloud,
   or is it what the default does?
3. Where does the **flatness** come from — the catalogue's altitudes, or somewhere else?

**The baseline reproduces byte for byte, and it was checked before anything was believed.** Three renders of
`Clouds_Demo` at `mid away` on this worktree's binary are identical to each other **and to the committed
`Shots/RW_after_mid_away.png`** (`aea16b7a…`), so the sky measured below is §RW's sky and not a local
variant. The repeat floor is zero.

### The instrument had to grow a second quantity first

`Tools/LatticePeak`'s field mode now reports, beside the cover it already reported:

* **the mean chord** — how far a ray stays inside cloud, gathered over every scan line of the baked volume,
  horizontally (both axes, wrapped across the region's faces because the volume is periodic) and vertically
  (not wrapped: the layer has a floor and a ceiling);
* **the column span** — first cloud voxel to last down one column, over whatever air is between them, and
  the **solidity** that is the chord divided by it.

A chord alone cannot tell a plate from a tower with holes in it; a span alone cannot tell a solid body from
a haze. `Desert/Tests/Engine/CloudPlacementSpectrum` pins both on lines whose answer it chose, including the
wrap (a body straddling a face is **one** body, not two) and the relation the pair must satisfy: **halving
the placement cell narrows the bodies and leaves their height alone**, because the width follows the cell
and the height follows the type's own band. Tying either to the other is the defect that test would name.

> ⚠️ **A column span is NOT a silhouette**, and the first version of this report called it one. It is
> measured down one column, so it says how much of the layer has cloud above one patch of ground — which is
> half of how tall the body looks from the side, because the lobes are spread over a disc and the top of the
> pile does not stand over its bottom. The two differ by a factor of two on the shipped type, so the wrong
> word would have been read as the wrong number.

### QUESTION 1 — 2.5 IS NOT NEEDED, AND THE MEASUREMENT SAYS WHERE THE BOUND ACTUALLY IS

One setting at a time, everything else at the values §RW ships, `Clouds_Demo`'s configuration (48 km region,
12 km weather tile → 3.000 km cell, coverage 0.762, wind +X), **32 realisations**. `LATTICE` is the
prominence of the strongest bump standing on a multiple of the predicted 3.000 km period.

| Cloud Density | lumps | sky cover | LATTICE X | LATTICE Z | verdict |
|---|---|---|---|---|---|
| **1.0** | 1242 | 0.7561 | 0.0000 | **0.0264 at 5.812 km (2P), 5.9x noise** | **the grid is back** |
| **1.5** | 1902 | 0.7387 | 0.0007 (0.2x noise) | 0.0000 | inside the noise |
| **1.75** | 2208 | 0.7431 | 0.0017 (0.3x noise) | 0.0000 | inside the noise |
| **2.0** | 2484 | 0.7504 | 0.0027 (0.6x noise) | 0.0000 | inside the noise |
| **2.5** (§RW) | 3144 | 0.7446 | 0.0000 | 0.0000 | inside the noise |

> **The lattice bump is inside the estimator's own noise at every setting from 1.5 upward, and only comes
> back at 1.0.** §RW's own arm table already said this and the conclusion was not drawn from it: the count is
> not what removes the grid — the SCATTER is — so paying for the count above the point where the grid is
> already gone buys nothing the instrument can see.

**And it costs the picture, which is the other half of the answer.** The compensation that keeps `Coverage`
honest narrows every cluster as the density rises, so raising it does not add cloud — it makes the same
cloud out of smaller bodies:

| Cloud Density | mean horizontal chord | mean vertical chord | wider than tall | column span (of 3.60 km) | solidity |
|---|---|---|---|---|---|
| 1.0 | 1.991 km | 0.532 km | 3.74x | 1.636 km (45%) | 0.33 |
| 1.5 | 1.784 km | 0.561 km | 3.18x | 1.685 km (47%) | 0.33 |
| **1.75** | **1.705 km** | **0.570 km** | **2.99x** | 1.706 km (47%) | 0.33 |
| 2.0 | 1.634 km | 0.575 km | 2.84x | 1.721 km (48%) | 0.33 |
| 2.5 | 1.541 km | 0.591 km | 2.61x | 1.748 km (49%) | 0.34 |

At 2.5 the typical body is **23 per cent narrower** than at 1.5. That is what turned the frame into a pelt:
in `Shots/RW_before_mid_away.png` the lozenges are confined to the lower third — the far distance — and the
near sky is made of large fused banks; at 2.5 the near bodies have shrunk to the angular size the far ones
already had, so the whole frame reads as one texture. **The shape did not change and the size cue did.**

The frame agrees, at `mid away`, `ImageStat` over `0 0 1280 551`:

| Cloud Density | mean | contrast | sat | frame cloud fraction |
|---|---|---|---|---|
| 1.5 | 0.568 | **0.378** | **0.147** | 0.762 |
| **1.75** | 0.574 | **0.379** | 0.141 | 0.773 |
| 2.0 | 0.598 | 0.374 | 0.107 | 0.836 |
| 2.5 (§RW) | 0.570 | **0.349** | 0.109 | 0.712 |
| the sky the owner accepted (`RW_before_mid_away.png`) | 0.591 | **0.384** | 0.087 | 0.889 |

**Contrast is what a field of identical lozenges costs, and 2.5 is the only setting that loses it.** At 1.75
the frame measures 0.379 against the 0.384 of the sky that shipped before §RW, and there is half again as
much blue in it as at 2.5.

> **THE DEFAULT MOVES TO 1.75.** It is the lowest setting that is a whole measured step above the one that
> fails (1.0, at 5.9x noise) and it sits on the contrast plateau. The sky's cover moves from 0.7446 to
> 0.7431 — a change of 0.0015 — so **no scene is re-authored and decision D-20's condition is untouched**;
> the deviation from the 0.762 the slider asks for is 0.019, inside the 0.025 the suite asserts at this
> cell. The bake gets cheaper: 2208 lumps against 3144, a fall of 30 per cent.

Frames: `Shots/RW2_density_mid_1p5.png`, `Shots/RW2_after_mid_away.png` (1.75, the new default),
`Shots/RW2_density_mid_2p0.png`, and `Shots/RW_after_mid_away.png` for 2.5 — that last one is §RW's own file
and re-shooting it would have put a duplicate in the directory. Same four at the horizon:
`RW2_density_horizon_1p5.png`, `RW2_after_horizon_away.png`, `RW2_density_horizon_2p0.png`,
`RW_after_horizon_away.png`.

### QUESTION 2 — THE FLOODED ZENITH IS NOT THE CAMERA, NOT THE SIZE SPREAD, AND NOT NEW

The measurement is the Otsu cloud fraction of the frame, which is `LatticePeak --frame`'s own split, over
`0 0 1280 551`. A frame above **0.90** has under a tenth of blue in it and is what "the view is flooded"
means here.

**Eight seeds, one camera** (`0,200,0`, `--look 0,0.9,-1`), at §RW's shipped default:

| seed | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| cloud | **0.9460** | 0.3769 | 0.3464 | 0.2325 | 0.6360 | 0.5308 | 0.1878 | 0.4487 |

One of eight. On that evidence alone the shipped seed would look like bad luck — **and it is the wrong
experiment**, because a player does not change the seed, he walks. **Fifteen camera positions along +X from
0 to 100 km, one seed, one sky**, against the same fifteen with §RW's four knobs returned to their old
values on this same binary:

| x km | 0 | 2 | 4 | 6 | 8 | 10 | 12 | 14 | 25 | 37 | 50 | 62 | 75 | 87 | 100 | mean | ≥0.90 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **density 2.5** | .946 | .664 | .659 | .872 | .961 | .947 | .897 | .720 | .642 | .783 | .786 | .732 | .709 | .909 | .821 | **.803** | **4 of 15** |
| **old knobs** | .920 | .808 | .822 | .974 | .389 | .956 | .833 | .850 | .927 | .901 | .444 | .204 | .609 | .855 | .982 | **.765** | **6 of 15** |
| **density 1.75** (new) | .923 | .706 | .740 | .905 | .360 | .745 | .896 | .751 | .724 | .618 | .634 | .400 | .331 | .656 | .565 | **.663** | **2 of 15** |

**The default this task chose halves it, and that was not what the default was chosen for.** At 1.75 the
same fifteen positions give a mean of **0.663** and **2 of 15** above 0.90, against 0.803 and 4 of 15 at 2.5.
Larger, fewer bodies leave larger holes; the sky's cover is the same to three decimals. It is a side effect
of question 1's answer and it is reported as one — the flooded zenith is still a property of a `Coverage` of
0.762 and the artist's lever for it is `Coverage` and `Weather Patch Strength`, not the density.

> **The old sky floods the zenith MORE often than §RW's, not less — 6 of 15 against 4 of 15 — and it floods
> it in an all-or-nothing way** (0.204 to 0.982) because it has no patch modulation, so a whole region is
> either busy or clear. §RW's sky is flatter in distribution and higher on average. Neither of them is a
> camera accident: **at a `Coverage` of 0.762 the flooded zenith is the sky that was authored.**

The geometry says the same thing without a render. The slider delivers 0.745 of the sky as cloud measured
straight down. The zenith point looks at **42 degrees** of elevation, and a ray at 42 degrees crosses the
3.6 km layer over `1/sin 42 = 1.49` times the vertical path, so the fraction of DIRECTIONS that meet cloud
must exceed the fraction of COLUMNS that do. 0.80 against 0.745 is that factor, arriving where it should.

**AND §RW'S OWN EXPLANATION OF THAT FRAME IS WRONG, which is worth more than the table above.** §RW wrote
that the zenith went to one mass because "a cluster may now be 1.38 times the base width, and one that lands
near the camera fills the zenith from two kilometres below", and offered `Cloud Size Variety` as the artist's
answer. Measured: **at `Cloud Size Variety` 0, the same camera and the same seed give 0.9398 against 0.9460**
— six thousandths. `Shots/RW2_zenith_variety_zero.png` is that frame and it is the same overhead mass. The
size spread is not what put it there. Neither is the density, which moves it by ten points and does not
remove it: **0.8380 at 1.5, 0.8947 at 2.0, 0.9460 at 2.5**.

`Shots/RW2_zenith_pos_8km.png` (0.961) and `Shots/RW2_zenith_pos_25km.png` (0.642) are two positions in one
sky; `Shots/RW2_zenith_oldknobs_pos_62km.png` (0.204) and `Shots/RW2_zenith_oldknobs_pos_100km.png` (0.982)
are the old placement's two extremes.

### QUESTION 3 — THE LENS IS THE LUMP, AND ITS ASPECT IS AN ACCIDENT OF TWO UNRELATED NUMBERS

**The catalogue is NOT the cause, and the arithmetic is short enough to check.** For the shipped congestus at
the new default: the cluster's horizontal half-extent is `0.48 R + 0.62 R = 1.10 R` and `R` is
`0.72 x 3.000 km` scaled by the density and the packing and the mean fill and the mean size draw — 1.672 km —
so the body is **3.66 km across**. Its vertical envelope is the type's band times the mean fullness plus a
lobe at each end: `3.60 x 0.849 + 0.72` = **3.78 km**. **0.97 to one. The shipped type is as tall as it is
wide.** Raising or lowering `BaseAltitudeKm`/`TopAltitudeKm` is not where the flat lens comes from.

**What is flat is the LUMP the cluster is built out of, and its two radii are written in two places that
have never had to agree:**

| | where it comes from | shipped value |
|---|---|---|
| a lump's **vertical** radius | `0.6 x band / kMaxBlobsPerCluster` — the TYPE's altitudes divided by a constant `6` that lives in the placement file | `0.6 x 3.60/6` = **0.360 km** (diameter 0.72 km) |
| a lump's **horizontal** radius | `(0.62 - 0.16 t) x clusterRadius`, and `clusterRadius` is `0.72 x` the placement CELL | **0.63 to 0.89 km** (diameter 1.3 to 1.8 km) |

> **A lump is between 2.1 and 2.5 times wider than it is tall, and nothing in the code asserts a relation
> between the two numbers that decide it.** Double the type's band and the lumps get twice as tall; halve
> the placement cell and they get twice as narrow. This is exactly the shape §2.3.1 of the contract is about
> — two places that must agree and nobody checks that they do — and `CloudPlacementSpectrum` now has the
> relation test, which passes and records the ratio rather than fixing it.

**And the lumps do not stack, they are spread over a disc**, one golden angle apart at a radius of
`0.48 R (1 - 0.55 t)`. So a column through a "tower" meets ONE lump and not six, and the measurement says so
exactly: the mean vertical chord is **0.570 km** against a lump diameter of 0.720 km — a random line through
one ellipsoid, and nothing above it. The column's cloud spans 1.706 km of the 3.60 km band at a **solidity
of 0.33**: two thirds of the body's own height is air.

**IN DEGREES, at the mid elevation the question asked about** — camera at 2 m, `--look 0,0.45,-1` is 24.23
degrees of elevation, the layer's middle at 4.0 km is a slant range of 9.75 km, and a vertical extent is
foreshortened by `cos 24.23 = 0.912`:

| | at the new default (1.75) | at §RW's 2.5 |
|---|---|---|
| the body's **opaque core** | **10.0 deg wide x 3.1 deg tall — 3.3 to 1** | 9.1 x 3.2 — 2.9 to 1 |
| the body's **whole envelope**, air included | 10.0 x 9.1 — **1.1 to 1** | 9.1 x 9.4 — 1.0 to 1 |

> **That is the whole answer to "why does it look like a flat lens".** The body is round; the part of it that
> is opaque is a slab three degrees tall in the middle of it, because the lumps sit BESIDE one another
> instead of ON one another. The eye reads the opaque part. The fix is the lobe layout — `spread` against
> `verticalKm` in `GenerateCloudProceduralBlobs` — and it is not the catalogue and not this task.

### The six points, at the new default

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, 1280x766, `ImageStat` over `0 0 1280 551`. The 2.5
column is §RW's published table, which this worktree reproduced byte for byte at `mid away` before anything
was changed.

| point | mean 2.5 / 1.75 | contrast 2.5 / 1.75 | sat 2.5 / 1.75 |
|---|---|---|---|
| zenith away `0,0.9,-1` | 0.600 / **0.616** | 0.251 / **0.354** | 0.068 / 0.067 |
| mid away `0,0.45,-1` | 0.570 / **0.574** | 0.349 / **0.379** | 0.109 / **0.141** |
| horizon away `0,0.12,-1` | 0.639 / 0.630 | 0.281 / **0.292** | 0.071 / 0.078 |
| zenith sunward `0,0.9,1` | 0.714 / 0.644 | 0.435 / **0.462** | 0.029 / **0.060** |
| mid sunward `0,0.45,1` | 0.620 / 0.610 | 0.303 / **0.274** | 0.069 / 0.073 |
| horizon sunward `0,0.12,1` | 0.636 / 0.634 | 0.274 / **0.282** | 0.076 / 0.091 |

**Five of six points gain contrast and one loses it**, and the one that loses it is named rather than
glossed: `mid sunward` falls from 0.303 to 0.274. Looking into the sun the bodies are lit through rather
than lit on, so making them larger and fewer removes edges from the frame instead of adding them — the same
mechanism that helps at every other point, running the other way at the one point where the silhouette is
the dark side of the cloud.

The point that moves most is `zenith away`: contrast 0.251 to 0.354, because the single undifferentiated
mass §RW's frame showed breaks into bodies with edges and blue between them.

### The whole sweep, and the counts the contract asks for

`CI=true premake5 gmake`, then **every generated makefile built and run with the objects and the binaries
deleted first**, on a `libDesert.a` rebuilt after the default changed.

| | |
|---|---|
| makefiles generated | **79** |
| excluded as tools and libraries | **16** |
| suite makefiles | **63** |
| suite binaries built | **63** |
| missing binaries | **none** |
| suites failed | **none** |

The exclusion list is §RW's, `LatticePeak` included — and it is still not the one written in
`DEV_CONTRACT.md` §2.4 item 5a. **That file is the teamlead's and §1.6 says a foreign file is asked for
rather than taken, so this task did not edit it either.** The change needed remains one word:
`LatticePeak` beside `ImageStat|LineJump|SceneMigrator` in the `case`. Without it the next developer's sweep
runs a tool as a suite and counts 62 binaries against 64 makefiles.

`ComponentReflection` did NOT fail this time, which is the expected outcome and worth saying: no reflected
field was added, only a default value changed, and a default is not part of the field list that assertion
pins.

### Three sabotages, three RED

Each applied, the suite's **objects and binary deleted**, rebuilt, run, and reverted — and the numbers above
come from a binary rebuilt after the last revert, which is the trap §RW's own report names.

| break | result |
|---|---|
| the chord census ignores the wrap, so a body across a region face becomes two | **RED** |
| the span stops at the first gap, which is what the chord already does | **RED** |
| a lump's height is taken from the placement cell instead of the type's band | **RED** |

The third is the relation the phase exists to state. It passes today at a ratio the report quotes rather
than at a bound, because the ratio is a defect that is **measured and not fixed** — see below.

### What this task did NOT do, and why it is here rather than in a commit message

* **IT DID NOT FIX THE FLAT LENS, and that is the largest thing left.** The cause is located to the line:
  a lump is 0.72 km tall because the placement divides the type's band by a constant `6`, and 1.3 to 1.8 km
  wide because its width is a fraction of the placement CELL — two numbers that have never had to agree —
  and the lumps are then spread over a disc rather than stacked, so a column meets one of them. The cure is
  the lobe layout, it changes the silhouette of every one of the nine shipped types, and phase A3 measures
  every type with a frame per genus. That is a re-calibration of the catalogue with its own ten frames and
  this task did not shoot them.
* **`LATTICE 0.0000` DOES NOT MEAN THE CURVE IS FEATURELESS, and this is a caveat on the instrument rather
  than on the sky.** At every density measured, the strongest bump on the Z axis sits at **5.25 to 5.44 km**
  — at 2.5 it is 0.0163 at 2.8x the noise, at 2.0 it is 0.0219 at 8.1x — and the `LatticeScore` does not
  count it, correctly, because it is not within an eighth of a period of any multiple of 3.000 km. It is
  repeatable, it is on one axis only, and this task did not find out what it is. Whoever takes the shape
  task should start by asking.
* **It did not re-shoot §RW's knob frames.** Ten of the twelve are at settings this task did not touch, and
  the two that are the density's own ends (`RW_knob_density_low/high.png`) are still the ends of the same
  slider. What moved is where the default sits between them.
* **It did not touch the march, the erosion, the lighting or the placement's arithmetic.** The only value
  changed is one default, in the two places that hold it, and the two comments that described the density
  compensation with the wrong exponent.

## NB — the NaN in the density was `0 * (+inf)`, and the picture did not move, 2026-08-25

macOS Release went red on
`CloudFieldDensity.AtZeroStrengthTheProfileSurvivesUntouchedAndTheDensityScaleIsTheOnlyMultiplier`, with
`full` = 0.4 and `halved` = NaN out of the same `CloudFieldSample` — only `DensityScale` differed between
the two calls. Windows Release and every Debug job were green.

### Where the NaN was born, to the expression

`Common/CloudField.glslh`, the erosion term:
`clamp( params.DetailStrength * max( field.DetailFactor, 0.0f ), 0.0f, 1.0f )`. The test sets
`DetailStrength` to zero, so this is `0 * max(DetailFactor, 0)`. Injected directly, at a strength of zero:

| DetailFactor | full | halved |
|---|---|---|
| 0 | 0.4 | 0.2 |
| 1 | 0.4 | 0.2 |
| 8 | 0.4 | 0.2 |
| +inf | **NaN** | **NaN** |
| NaN | **NaN** | **NaN** |

`max(+inf, 0)` is `+inf` and `0 * inf` is NaN. The outer `clamp` cannot help — it is outside the product.

### Why only Release, and it is not fast-math

There is no `-ffast-math`, `-Ofast` or `-ffp-contract` anywhere in the premake scripts; Release differs
from Debug by `optimize "On"` and `NDEBUG` and nothing else. The cause is that the FIXTURE built a
`CloudFieldSample` out of three of its five fields, and the struct is GLSL compiled as C++ so it has no
default member initialisers. The `-O2` IR of the test translation unit shows the mechanism exactly: clang
gives each by-value argument its OWN alloca and writes only bytes 0..11 into each —

```
store <2 x float> <float 0x3FD99999A0000000, float 0x3FE3333340000000>, ptr %12
store float 1.000000e+00, ptr %50            ; %12 + 8
call ... CloudSampleDensity(ptr %11, ptr %12, ...)
store <2 x float> <float 0x3FD99999A0000000, float 0x3FE3333340000000>, ptr %14
store float 5.000000e-01, ptr %52            ; %14 + 8
call ... CloudSampleDensity(ptr %13, ptr %14, ...)
```

Bytes 12..19 of `%12` and of `%14` are never written and the two allocas are DIFFERENT slots, so the two
calls read two different leftovers — one finite, one with an all-ones exponent. At `-O0` there is a single
slot and both calls read the same bytes, which is why no Debug job could ever see it.

### Is it reachable in a frame

Not through the producers as they stand — `SampleCloudField` writes all five fields on every path. But the
FACTOR being unbounded above is reachable, and the two producers are not equal:

* procedural: `CloudTypeShape.DetailFactor` is refused outside [0, 8] by `Assets::ValidateCloudTypeShape`
  at load (`Engine/Assets/CloudTypeData.cpp`).
* authored: `ECS::HeroCloudData.DetailFactor` has a reflected `Range(0, 4)` that constrains the editor's
  SLIDER and nothing else. `ComponentRegistry` clamps nothing on deserialisation and the packer's
  `std::max(x, 0.0f)` passes `+inf` through unchanged.

So a hand-edited scene plus Detail Strength at zero — a documented, invited slider position — puts NaN in
the density of every sample inside that body, and a NaN density is a NaN optical depth, a NaN
transmittance, and a black or fully transparent hole with nothing in the log.

### The fix, and the four sites

`CLOUD_MATERIAL_FACTOR(x) = min(max(x, 0), 8)`, applied wherever a factor enters a product with a slider:
the erosion depth, and — found by looking for siblings rather than by a failure — the composition of
`DensityScale` in BOTH producers, which has the identical `slider * factor` shape and a slider whose zero
is just as legitimate.

Eight is the ceiling the procedural validator already enforces, so the bound is the identity there by
construction. Measured against the shipped content it is the identity everywhere: the nine cloud types
carry Detail/Density/Extinction factors in **[0.15, 1.35]**, and every hero cloud in every shipped scene
carries **1.0** (one carries 0.5). The layer's Density Scale slider tops out at 2.

### The six-point protocol: SIX of six byte for byte

`Clouds_HeroCloud`, camera `0,200,0`, `--shot-frames 90`, 1280x766, Release. The SAME BINARY throughout —
this change touches no C++ outside tests — with only `Common/CloudField.glslh` swapped between runs, which
isolates the shader edit exactly. First render in the worktree discarded per §A1's rule.

| point | dev's shader vs this branch |
|---|---|
| zenith away `0,0.9,-1` | identical |
| mid away `0,0.45,-1` | identical |
| horizon away `0,0.12,-1` | identical |
| zenith sunward `0,0.9,1` | identical |
| mid sunward `0,0.45,1` | identical |
| horizon sunward `0,0.12,1` | identical |

The repeat floor was taken and is zero: the fixed shader shot twice, six of six identical. The frames were
LOOKED AT and carry sky and cloud at all three elevations — an identical pair of black frames would have
proved nothing.

### The sabotages, and one that stayed green

| sabotage | result |
|---|---|
| `CLOUD_MATERIAL_FACTOR(field.DetailFactor)` back to `max(field.DetailFactor, 0)` | reds `ASliderAtZeroTimesAMaterialFactorOutOfRangeIsStillANumber` |
| ceiling 8 -> 1 | reds `TheBoundOnAMaterialFactorIsTheIdentityEverywhereTheFactorIsLegal` |
| producer `DensityScale` unbounded | reds `TheProducerBoundsTheSpeciesRowRatherThanHandingItToTheMarchAsItFoundIt` |
| authored `DetailFactor` unbounded | reds `CloudAuthored.TheWinnersMaterialNumbersAreBoundedBecauseNothingUpstreamOfThemIs` |

**The original test stayed GREEN under the first sabotage.** It only ever fired because the CI runner's
stack happened to hold a non-finite leftover, so as a detector of this defect it was a coin toss. The four
above are deterministic.

**And the first version of the producer test was a hole.** It asked about one point at the default
coverage, where there is no cloud; the composition under test lives inside the branch a species takes when
it WINS the union, so the loop skipped it and the assertion passed without executing the line it was about.
It stayed green against the unbounded version. What turned it into a test is a count of winners over a
grid at coverage 0.9.

---

## SIL — the cluster was sized by the cell's short side, and a lump had two sizes, 2026-08-25

Two defects, both already measured and neither fixed: §RW's closing note (*"it did not fix the anisotropy
defect… the largest single thing left"*) and §RW2's (*"IT DID NOT FIX THE FLAT LENS, and that is the largest
thing left"*). They are one phase because they touch one catalogue and need one set of frames.

**The baseline is the shipped `dev` sky and that is proven rather than assumed.**
`Shots/SIL_before_mid_away.png` is **byte for byte identical** to the committed `Shots/RW2_after_mid_away.png`
(`204cc71f59bdff06c51a0cff0a02c33e`), so every "before" number below is §RW2's own sky measured again on
this worktree's binary. The repeat floor is zero from the SECOND render onward — the first render in a fresh
worktree differs, which is §A1's correction reproduced for the third time in this programme.

---

### DEFECT A — the comment said the area was held constant, and the cluster threw it away

`CloudProceduralCellExtentKm` returns `cell * root` by `cell / root` and says why: *"with the AREA held
constant so that raising the anisotropy draws a cluster out into a band instead of making the sky
emptier."* The cluster was then sized by `0.72 * min(extent.x, extent.y)` — **the SHORT side** — so an
anisotropic cell got a cluster scaled by its narrowest dimension on BOTH axes and the sky emptied as the
square of the stretch.

The cure is one line and one consequence: the cluster's size is the **geometric mean** of the two sides,
which is `species.CellKm` itself, and the stretch the cell no longer spends on its area is spent on the
cluster's SHAPE — the lobe disc becomes an ellipse in the wind's frame, and the lumps are given anisotropic
radii and a yaw so that they turn with the lattice instead of pointing east in a north-west wind.

**`stretch` is taken from the extents the cell function returned and not from `species.Anisotropy`.** That
is deliberate and it is the same discipline the defect broke: one statement of the quantity, so a floor
applied inside the cell function cannot be forgotten outside it.

---

### DEFECT B — a lump's height came from one file and its width from another

§RW2 located it to the line. A lump's vertical radius was `0.6 * band / kMaxBlobsPerCluster` — the TYPE's
altitudes divided by a constant living in the PLACEMENT file — and its horizontal radius was
`(0.62 - 0.16t) * clusterRadius`, a fraction of the placement CELL. Nothing asserted a relation between
them, and measured they were 2.1 to 2.5 times wider than tall. The lobes were then spread over a
golden-angle disc rather than stacked, so a vertical column met **one** of them: chord 0.570 km against a
lump diameter of 0.720, solidity 0.33.

#### THE DECISION THE TEAMLEAD ASKED FOR, AND THE ALTERNATIVE IT WAS CHOSEN OVER

> **A lump has ONE size.** Both of its radii come from one quantity — the cluster's own radius — through a
> single dimensionless constant `kLumpVerticalOverHorizontal`. The type's band no longer decides how TALL a
> lump is; it decides only WHERE the stack sits, by insetting it so that the body's vertical envelope is
> exactly the altitudes the type declares.

**The alternative was to make the lump's aspect a property of the TYPE** — a fifteenth number in
`.decloudtype`, authored nine times. It is refused, and the argument is not "one constant is simpler":

* **A lump is the convective PARCEL, and a genus is a statement about how parcels are ARRANGED** — a heap,
  a deck, a downwind band, a sheet — not about what one parcel looks like. At the small scale every genus
  in the licence record shows the same roughly-isotropic turret texture.
* **Nine numbers with no independent evidence behind any of them can only ever be asserted equal to
  themselves.** That is a dead setting in the sense of `DEV_CONTRACT.md` §1.3 arrived at from the far side:
  live, wired, and meaningless. One constant can be asserted, and
  `Desert/Tests/Engine/CloudPlacementSpectrum` asserts it by moving the cell by a factor of three and the
  band by a factor of four and demanding the measured ratio not move.
* **A genus's flatness survives anyway, and it survives THROUGH the type rather than beside it.** A lump may
  not be taller than half the band it lives in, so a 400 m stratus deck gets 200 m lumps however wide its
  cell is and a 3.6 km congestus never meets the clamp at all. **The squashing is done by the layer the
  type already declares — which is what squashes a real stratus.**

#### AND THE COUNT STOPPED BEING A CEILING

`kMaxBlobsPerCluster` was a ceiling shortened by the band, and the arithmetic behind that shortening —
"the vertical radius is 0.6 of the spacing" — is exactly the rule being deleted. It is `kBlobsPerCluster`
now, a count, and the relation it used to protect (a lump the march cannot find) is enforced where it
belongs: the per-lump floor at half of `ResolvableChordKm`. **Six is a property of the DISC**: the lobes are
one golden angle apart as well as up the band, and six is how many a disc needs before its outline reads as
a lumpy mass rather than as a rosette. The two types the old ceiling actually bit — humilis and stratus,
both 400 m bands — went from three lumps per cluster to six.

---

### THE CONSTANT IS FIXED FROM BOTH SIDES, AND THE CEILING BELONGS TO ANOTHER PHASE

The shipped congestus, `Clouds_Demo`'s configuration (48 km region, 3.000 km cell, coverage 0.762,
wind +X), 8 realisations, `Tools/LatticePeak --field`. The **EROSION** row is
`Desert/Tests/Engine/CloudField`'s own measurement of how far §DS's cut moves the surface at which the
optical depth first reaches 1, against the **125 m** floor that suite asserts:

| | §RW2's sky | 0.40 | **0.45** | 0.50 | 0.60 | 0.75 | 1.00 |
|---|---|---|---|---|---|---|---|
| mean horizontal chord | 1.705 km | 1.924 | **2.002** | 2.084 | 2.264 | 2.541 | 2.916 |
| mean vertical chord | 0.569 km | 0.681 | **0.790** | 0.905 | 1.144 | 1.509 | 1.992 |
| column span, of 3.60 km | 1.713 | 1.786 | **1.814** | 1.842 | 1.894 | 1.970 | 2.113 |
| solidity | 0.33 | 0.38 | **0.44** | 0.49 | 0.60 | 0.77 | 0.94 |
| **OPAQUE CORE** | **3.3 : 1** | 3.1 | **2.8** | 2.5 | 2.2 | 1.8 | 1.6 |
| whole envelope | 1.1 : 1 | 1.2 | **1.2** | 1.2 | 1.3 | 1.4 | 1.5 |
| sky cover | 0.7388 | — | **0.7390** | 0.7394 | — | 0.7404 | 0.7409 |
| **EROSION travel** | 139 m | 155 m | **139 m** | 126 m | **113 m ✗** | **101 m ✗** | — |

**THE CEILING IS A BOUND THIS TASK DID NOT KNOW IT HAD, and it is the finding of the phase's second half.**
§DS fixed the layer's Detail Strength by a floor: the cut must move the surface the eye sees by more than
the 125 m the march can be relied on to find, *"or it costs a fetch and changes nothing a viewer can see"*.
A taller lump makes the body **optically thicker per metre** — the surface at which the optical depth
reaches 1 sits at a profile of 0.630 at 0.40 against 0.576 at 0.75 — so **the same cut moves that surface
less far**. At 0.60 and above `CloudField` goes red. This was found by the sweep and not by inspection: the
first arm of this phase was measured, framed and committed at **0.75** before the sweep named it.

> **0.45 IS THE LARGEST VALUE THAT CLEARS BOTH BOUNDS, and it clears the erosion floor at 139 m — the same
> 1.11x headroom §DS itself shipped at, to the metre.** 0.50 clears it by ONE metre, which is exactly the
> balanced-on-the-bound case §DS looked at (at its own 0.35) and refused by name, because *"a one-per-cent
> margin would make the suite fail on any change to the generator that moved a body by a voxel."*

**THE COVER DOES NOT MOVE over the whole ladder — four ten-thousandths — so this constant does not spend
the Coverage slider and decision D-20 is untouched by it at any setting.**

#### ⚠️ WHAT 0.45 LEAVES ON THE TABLE, AND THE DECISION IS THE TEAMLEAD'S

0.45 takes the congestus' OPAQUE CORE from **3.3 : 1 to 2.8 : 1** against a whole envelope of 1.2 — about a
quarter of the defect §RW2 measured. **0.75 takes it to 1.8 : 1**, and the difference is large enough to see
without an instrument: `Shots/SIL_alt_aspect075_mid_away.png` beside `Shots/SIL_after_mid_away.png`.

**The price of 0.75 is §DS's erosion, and it is quantified rather than waved at.** At 0.75 the surface
travel at the shipped Detail Strength is 101 m; §DS's own ladder re-measured against the 0.75 body gives
116.7 m at a strength of 0.50 and 157.1 m at 0.80, so restoring §DS's 1.11x headroom needs a strength of
about **0.60**. Raising it multiplies every type's cut by 1.5, which §DS documented as deleting rather than
shredding the two thin types, and its own recipe for that is to re-base their Detail Factors by the same
ratio — cirrus 0.625 → 0.417, altocumulus 0.40 → 0.267. **That is a re-calibration of another phase's number
with its own frames, and this task did not take it.** What ships is the value that spends nobody else's
calibration.

---

### FORM BY FORM, WHICH IS WHERE DEFECT A IS ACTUALLY PAID FOR

Every row is `LatticePeak --field --type <the shipped asset> --coverage 0.5`, 6 realisations, everything
else at the values that ship. The type's own `PlacementScale` and `PlacementAnisotropy` are read from the
asset by the shipped parser — the tool does not retype nine shapes.

**`Coverage` is set to 0.5, so a column that does not read 0.5 is the slider lying by that much.**

| genus | aniso | cover before | cover after | slider out by, before → after |
|---|---|---|---|---|
| cumulus humilis | 1.0 | 0.4505 | **0.5279** | −0.050 → +0.028 |
| cumulus mediocris | 1.0 | 0.5288 | **0.5165** | +0.029 → +0.017 |
| cumulus congestus | 1.0 | 0.5111 | 0.5113 | +0.011 → +0.011 |
| cumulonimbus | 1.0 | 0.8543 | 0.8543 | **+0.354 → +0.354** |
| stratocumulus | 1.6 | 0.3778 | **0.5220** | −0.122 → +0.022 |
| stratus | 1.0 | 0.4268 | **0.5065** | −0.073 → +0.007 |
| altocumulus | 1.5 | 0.4408 | **0.5581** | −0.059 → +0.058 |
| **cirrus** | **8.0** | **0.0827** | **0.4989** | **−0.417 → −0.001** |
| **lenticular** | **0.2** | **0.1338** | **0.5016** | **−0.366 → +0.002** |

**The two worst rows in the library go from delivering a fifth and a quarter of what their slider asks for
to delivering it to the third decimal.** The four anisotropic types are the four §RW named, and all four are
inside 0.06 now.

> **AND ONE ROW IS UNCHANGED AND STILL WRONG, which is a finding rather than an omission.** The
> **cumulonimbus delivers 0.854 for a slider of 0.5**, before and after, to four decimals. Its anisotropy is
> 1, so neither defect touches it — its cause is its 6.000 km cell, the coarsest in the library by a factor
> of two, against a region that holds only eight of them across. `kPackingCompensation` and the 0.68 alive
> exponent were both fitted at the 3 km cell and neither carries a cell dependence. **This is measured and
> not fixed, and it is now the largest single lie left in the Coverage slider.**

#### The shape, form by form

| genus | solidity before / after | **OPAQUE CORE** before / after | envelope before / after |
|---|---|---|---|
| cumulus humilis | 0.91 / 1.00 | 6.2 : 1 / **5.6 : 1** | 5.6 / 5.6 |
| cumulus mediocris | 0.61 / 1.00 | 6.5 : 1 / **4.0 : 1** | 4.0 / 3.9 |
| cumulus congestus | 0.42 / 0.49 | 3.0 : 1 / **2.7 : 1** | 1.2 / 1.3 |
| cumulonimbus | 0.21 / 0.23 | 4.4 : 1 / 4.4 : 1 | 0.9 / 1.0 |
| stratocumulus | 0.57 / 0.72 | 2.2 : 1 / **2.2 : 1** | 1.2 / 1.6 |
| stratus | 0.80 / 1.00 | 46.4 : 1 / **38.4 : 1** | 37.3 / 38.4 |
| altocumulus | 0.77 / 0.93 | 2.3 : 1 / 2.4 : 1 | 1.8 / 2.2 |
| cirrus | 0.43 / 0.72 | 1.4 : 1 / 1.8 : 1 | 0.6 / 1.3 |
| lenticular | 0.57 / 1.00 | 3.1 : 1 / 3.2 : 1 | 1.8 / 3.2 |

**The column that moved is the ENVELOPE, and it moved DOWN toward the core rather than the other way.**
§RW2's finding was that the two disagreed — a core of 3.3 : 1 inside an envelope of 1.1 : 1 — so the eye,
which reads the opaque part, saw a plate where the geometry said a ball. At 0.45 the two columns agree in
every genus, and in six of the nine they agree because the body now **fits the altitudes its type declares**
instead of standing a lump-radius outside them on each side. **That is the honest reading: at 0.45 the flat
lens is corrected mostly by the body getting shorter and only partly by the core getting taller.** At 0.75
the core rises to meet the envelope instead, which is why the alternative is recorded above rather than
dismissed.

> ⚠️ **TWO ROWS ARE NOT A MEASUREMENT AND ARE MARKED AS SUCH.** `stratus` reports a core 807 degrees wide,
> which is a body wider than the 48 km region the chord census runs in: its 12.000 km cell puts one body
> across the whole map and the wrap counts it as a single run. Its RATIO is still meaningful (the same
> measurement window in both arms) but its width in degrees is not. `cumulus humilis` at 35 degrees is the
> same effect one order weaker.

---

### THE SIX POINTS

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, 1280x766, `ImageStat` over `0 0 1280 551`.

| point | mean before / after | contrast before / after | sat before / after |
|---|---|---|---|
| zenith away `0,0.9,-1` | 0.616 / 0.598 | 0.354 / **0.386** | 0.067 / **0.079** |
| mid away `0,0.45,-1` | 0.574 / 0.552 | 0.379 / **0.388** | 0.141 / **0.172** |
| horizon away `0,0.12,-1` | 0.630 / 0.622 | 0.292 / 0.273 | 0.078 / **0.087** |
| zenith sunward `0,0.9,1` | 0.644 / 0.648 | 0.462 / 0.444 | 0.060 / **0.063** |
| mid sunward `0,0.45,1` | 0.610 / 0.601 | 0.274 / **0.297** | 0.073 / **0.099** |
| horizon sunward `0,0.12,1` | 0.634 / 0.621 | 0.282 / 0.277 | 0.091 / **0.102** |

**Saturation rises at all six** — there is more blue in the sky at every point, which is the direction the
owner's "the whole sky is cloud" asks for. Contrast rises at three, falls at three, and the largest single
move is `mid sunward` +0.023. **Nothing in this table is large, and saying so is the point: at 0.45 the
change is a change of SHAPE at a fixed amount of cloud, and a histogram of luminance is the wrong instrument
for it.** The frame mode's own numbers are below and the genus frames are where it shows.

| point | cloud fraction before / after | ragged before / after | lap r4 before / after |
|---|---|---|---|
| zenith away | 0.9234 / 0.9274 | 3.11 / 2.22 | 0.00533 / 0.00527 |
| mid away | 0.7730 / 0.7313 | 6.26 / **6.79** | 0.00720 / **0.00724** |
| horizon away | 0.8033 / 0.5496 | 20.16 / **41.64** | 0.02615 / 0.02217 |
| zenith sunward | 0.1961 / 0.2163 | 5.61 / 5.50 | 0.00520 / **0.00530** |
| mid sunward | 0.3719 / 0.3919 | 18.35 / 15.32 | 0.00771 / **0.00779** |
| horizon sunward | 0.5862 / 0.4720 | 36.35 / **44.16** | 0.03359 / 0.02833 |

**The two horizon points, which is where §RW's own "THE SHOW" pair was taken, gain the most**: the frame's
cloud fraction falls from 0.80 to 0.55 and from 0.59 to 0.47 while the raggedness roughly doubles at one and
rises by a fifth at the other. That is the far field breaking into separate bodies with sky between them
rather than closing into a carpet.

---

### THE THREE THINGS THE EARLIER PHASES BOUGHT, EACH CHECKED BY A NUMBER

**1. THE SKY'S COVER — kept.** `LatticePeak --field`, 32 realisations, `Clouds_Demo`'s configuration:
**0.7431 before, 0.7432 after.** §RW's own published figure is 0.7446 and §RW2's is 0.7431.

**2. NO LATTICE — kept, with one line of honest small print.**

| | X (east) | Z (north) |
|---|---|---|
| before | LATTICE **0.0017** at 9.188 km (3P), 0.3x noise | LATTICE **0.0000**; strongest bump 5.438 km, prom 0.0131 |
| after | LATTICE **0.0000** | LATTICE **0.0117** at 5.625 km (2P), 1.9x noise |

The X axis, which is the wind's own axis and the one the owner's "the clouds go in a row" was about, goes
from a countable bump to none at all. **The Z figure is NOT a returning grid and the evidence is in the
before column beside it**: §RW2 recorded an unexplained repeatable bump at 5.25–5.44 km on Z and said it is
not within an eighth of any multiple of 3.000 km. It is still there, its prominence has FALLEN
(0.0131 → 0.0117), and it has drifted 0.19 km — which is enough to carry it across `LatticeScore`'s
tolerance window around 2P and make it countable. Asked at a 4.000 km cell (`--tile 16`, 24 realisations)
the after field reports **LATTICE 0.0000 on both axes**, which a real lattice at 8.000 km could not do.

> **What this phase learned about §RW2's unexplained bump, since it was asked to say if it found out:** it
> **moves with the lump geometry** — 5.438 km before, 5.625 km after, on a change that touched nothing but
> the shape of a lump — so it is a property of the BODY and not of the placement lattice. That is one fact
> more than §RW2 had and it is short of an explanation. It was not pursued further.

**3. THE SCALLOPED EDGE FROM THE EROSION — kept, and it is the bound that chose the shipped constant.**

`Desert/Tests/Engine/CloudField` measures it directly and is green at **139 m against its 125 m floor**,
which is §DS's own 1.11x. The frame instrument agrees from the other side: raggedness up at three of the
six points, and up at both horizon points, which are the two the erosion's own §DS table was argued on.

**AND THE FRAME INSTRUMENT HAD TO BE BUILT, because §DS's was never committed.** §DS chose Detail Strength
on a "silhouette raggedness" it reported to four decimals and **the code that produced those numbers is not
in the tree**, so nothing could show a later phase had not spent it. `Tools/LatticePeak --frame` reports it
now, with §DS's other half — the mean absolute Laplacian inside the body at a billow's radius — beside it,
both covered by tests.

> ⚠️ **THE UNITS ARE NOT §DS'S AND THE CONVERSION IS WRITTEN DOWN.** §DS reported the perimeter and the
> area as fractions of the FRAME, which carries a `1/sqrt(N)`: the same picture at twice the resolution
> measures half as much, and the test written to assert scale-invariance is what caught it. What is computed
> is the raw `perimeter / sqrt(area)` in pixels, so a solid square measures exactly 4; §DS's `0.0038` at
> `mid away` converts by `sqrt(1280 x 551) = 839.8` to **3.19**, against the 6.26 this file measures at the
> same camera — the two differ because §DS shot `Clouds_Demo` at §RW's density of 2.5 and §RW2 then moved
> the default to 1.75. **The `lap r4` column additionally differs by definition**: §DS centred its stencil
> on cloud pixels, so a pixel within one radius of the silhouette sampled SKY; this version requires the
> whole stencil to be cloud, which a test insisted on after the centre-only form reported 0.0714 of
> "texture" on a flat cloud beside a checkered sky.

---

### THE FRAMES

Every one at `--shot-frames 90`, camera `0,200,0`. **All md5s are distinct** — checked, because this
programme has shipped byte-identical copies under two names before. The congestus genus pair is NOT in the
directory for exactly that reason: its scene IS `Clouds_Demo`, so its frames were byte-identical to
`SIL_before_mid_away.png` / `SIL_after_mid_away.png` and those are what the table points at.

| file | what it shows |
|---|---|
| `Shots/SIL_genus_Cirrus_before.png` / `_after.png` | **THE SHOW, AND IT IS DEFECT A ALONE.** Before: a dust of specks over an empty sky, 0.083 of the sky for a slider set to 0.5. After: fibrous bands combed downwind across the whole frame, which is what `Cirrus.decloudtype`'s own note has described since it was written |
| `Shots/SIL_genus_Lenticular_before.png` / `_after.png` | the same defect with the stretch the other way round: 0.134 to 0.502 |
| `Shots/SIL_before_mid_away.png` / `SIL_after_mid_away.png` | defect B at the elevation a player looks at. The `before` file is byte-identical to the committed `RW2_after_mid_away.png`, which is what proves the pair is a comparison against `dev` |
| `Shots/SIL_before_horizon_away.png` / `SIL_after_horizon_away.png` | the far field, where the frame's cloud fraction falls 0.80 → 0.55 and the raggedness doubles |
| `Shots/SIL_*_zenith_*.png`, `SIL_*_*_sun.png` | the remaining protocol points, which the change had to not break |
| `Shots/SIL_genus_<genus>_before.png` / `_after.png` | the other six genera, one pair each, all at `mid away` |
| `Shots/SIL_alt_aspect075_mid_away.png`, `_zenith_away.png`, `_genus_Cirrus.png` | **THE ALTERNATIVE THE TEAMLEAD IS BEING ASKED ABOUT** — the same three cameras at a lump aspect of 0.75, which is the sky that would ship if §DS's erosion were deepened to about 0.60 |

**The nine genus scenes are IN THE REPOSITORY** — `Resources/Assets/Scenes/SIL_<genus>.desce`, each one
`Clouds_Demo` with a single field changed, its `CloudType1`. They are committed rather than generated on the
spot because a frame whose scene is not in the tree cannot be re-shot by the next reader, which is the same
argument §RW made for authoring its knob sweep in scene files.

> **The nine genus frames are all at ONE elevation and that is a stated limitation rather than an
> oversight.** §A3 framed each sculpted body for its own size; a procedural LAYER cannot be framed that way
> without moving the camera between the two arms, and a camera that moves between arms is not a comparison.
> `mid away` is the elevation §RW2 asked its question at. A stratus deck at 0.15 km and a cirrus deck at
> 8.0 km are both partly outside that frame, and the stratus pair's difference lies almost entirely BELOW
> the `0 0 1280 551` crop — measured over the whole frame it is mean 0.398 → 0.400.

---

### THE RELATIONS ADDED, AND THE BREAKS THAT VERIFIED THEM

`Desert/Tests/Engine/CloudPlacementSpectrum`, which compiles `Tools/LatticePeak/Source/LatticePeakMath.hpp`
as C++ the way the `.glslh` suites compile shader maths.

| relation | what it would catch |
|---|---|
| a lump's height and its width are ONE quantity — the cell moved by 3, the band by 4, the ratio must not move | either radius picking up an input the other does not, which is the defect itself |
| every lump stands inside its type's own Base and Top Altitude | a body standing outside the altitudes an artist reads off a meteorological table |
| the sky's cover does not move with the anisotropy | defect A, in the form its own comment claimed |
| a stretched lump is long ALONG the wind — asked of the DISTANCE FIELD | a yaw of the wrong sign, which no aggregate in this suite can see and only a frame otherwise would |
| the BODY's height still follows the band while the LUMP follows the cell on both axes | §RW2's version of this test demanded the opposite and is inverted here, deliberately and in the comment |
| raggedness sees the edge and not the area; a solid square measures exactly 4 | a normalisation drifting under §DS's published numbers |
| the interior Laplacian reads only what the mask calls cloud | the boundary ring arriving as "texture" |
| the anvil is drawn out downwind by the same factor as the tower it caps | a storm that is a band with a circular lid, which §RW's own anvil assertion cannot see |

#### TWELVE SABOTAGES, AND ONE OF THEM STAYED GREEN

Every one applied, the suite's **objects and binary deleted**, rebuilt, run, and reverted — and the numbers
in this section come from a binary rebuilt after the last revert, which is the stale-binary trap §RW's own
report names.

| break | result |
|---|---|
| the cluster is sized by the cell's SHORT side again | RED |
| the stretch is thrown away and the cluster stays round | RED |
| the yaw's sign is flipped | RED |
| the lump keeps its round radii and only its PLACEMENT is stretched | RED |
| a lump's height comes from the band over the count again | RED (two tests) |
| the stack is no longer inset into the band | RED |
| a cluster is built from three lumps instead of six | RED |
| the suite's proxy paints a DISC of the longer radius again | RED |
| raggedness normalises by the area instead of its root | RED |
| the Laplacian stencil may straddle the silhouette again | RED |
| the anvil's own aspect is written a second time (see below) | RED — it does not build |
| **the anvil stops being drawn out with the cluster it caps** | **GREEN — a real hole, closed** |

**THE GREEN IS THE SAME LINE §RW FOUND, FROM A NEW DIRECTION, AND THAT IS THE INTERESTING PART.** §RW's own
sabotage run found that the anvil's width was written a SECOND time rather than derived from the cluster's,
and closed it with the anvil-over-tower ratio. That assertion is blind to ANISOTROPY — it is taken on one
axis, and the shipped cumulonimbus' anisotropy is 1 — so deleting the anvil's `stretch` left every suite in
the repository green: a storm whose tower is a downwind band under a circular lid, which is two bodies.
`TheAnvilIsDrawnOutDownwindWithTheTowerItCaps` asserts it at an anisotropy of 4 and the same break is now
RED. **A line that has been the hole twice is worth naming as such: the anvil is the one lump of a cluster
whose radii are authored rather than derived, and every property of a cluster has to be re-asserted for it
by hand.**

**AND TWO RELATIONS WERE RED ON THEIR FIRST RUN, BOTH REAL DEFECTS IN THIS TASK'S OWN WORK.**

1. **The band fit was made against a vertical radius that was then multiplied.** The wobble draw (0.85 to
   1.15) was applied at emission, after the stack had been fitted into the band, so the shipped congestus
   reached **5.883 km out of a 5.800 km band** and hung to 2.079 km under a 2.200 km base. It is the same
   two-places-must-agree shape one scale smaller, and it was invisible in every aggregate. The wobble is
   drawn with the layout now, and the test reports the band filled exactly: `2.200..5.800`.
2. **The suite's own rasterised proxy painted every lump as a DISC of its longer radius.** Exact while the
   two horizontal radii were equal, and this phase made them unequal. At an anisotropy of 8 the proxy
   reported the sky **0.98** covered where the bake reports 0.52 — it would have hidden defect A's cure
   behind a worse defect of the instrument. It paints the ellipse, in the lump's own yawed frame, now.

**A third finding, about the environment rather than about clouds.** The scratchpad directory this session
was given is SHARED with another agent's session, and a helper script written into it was overwritten
mid-run by that other agent's script — pointed at a different worktree, a different binary and a different
scene. Four frames were produced by it before the collision was noticed; they were deleted rather than
measured and every script moved to a path keyed to this worktree. **A frame produced by the wrong binary
looks exactly like a frame produced by the right one.**

---

### THE WHOLE SWEEP, IN BOTH CONFIGURATIONS

`CI=true premake5 gmake`, then **every generated makefile built and run with the objects and the binaries
deleted first**, in Debug AND in Release.

| | debug | release |
|---|---|---|
| makefiles generated | **80** | **80** |
| excluded as tools and libraries | **16** | **16** |
| suite makefiles | **64** | **64** |
| suite binaries built | **64** | **64** |
| missing binaries | **none** | **none** |
| `not-a-suite` lines | **none** | **none** |
| suites failed | **none** | **none** |

**The count balances with nothing unexplained and there is not one `not-a-suite` line to read**, which is
the outcome §2.4 item 5a asks a developer to check for by name. It balances at 80 against §RW's and §RW2's
79 because the generator produced one more project on this worktree than it did on theirs; the exclusion
list is §RW's with `LatticePeak` in it, and **it is still not the one written in `DEV_CONTRACT.md` §2.4 item
5a**. That file is the teamlead's and §1.6 says a foreign file is asked for rather than taken. The change
needed remains one word: `LatticePeak` beside `ImageStat|LineJump|SceneMigrator` in the `case`.

> **THE SWEEP IS WHAT FOUND THIS PHASE'S OWN REGRESSION.** The first Debug sweep, run on the arm committed
> at a lump aspect of 0.75, reported `FAIL CloudField` — §DS's erosion floor, 101 m against 125. Nothing
> else in the repository could see it, the frames looked better than the ones it replaced, and it would have
> shipped. That is the whole argument for running every suite rather than the ones whose name looks like the
> task.

**⚠️ THE KNOWN RED TEST DID NOT REPRODUCE, AND IT WAS CHECKED RATHER THAN ASSUMED.** The brief names
`CloudFieldDensity.AtZeroStrengthTheProfileSurvivesUntouchedAndTheDensityScaleIsTheOnlyMultiplier` as red in
macOS Release and belonging to another developer. It is **GREEN here in Release**, and it is green at the
merge base too — the same suite built in Release against `8e8cc1cc`'s own generator passes. So it is neither
fixed nor hidden by this change; on this machine it does not fail at all.

---

### THE PRICE

| | before | after |
|---|---|---|
| lumps in one region, `Clouds_Demo` | 2208 | 2208 |
| lumps, cumulus humilis | 3306 | **6612** |
| lumps, stratus | 42 | **84** |

**The shipped scene's bake is unchanged in cost to the lump**, because the congestus' 3.60 km band never
made the old ceiling bite. The two 400 m types double, and that is the count ceiling becoming a count —
named on `kBlobsPerCluster` with the reason.

---

### WHAT THIS TASK DID NOT DO

* **It did not take the lump aspect past 0.45**, and the reason is §DS's erosion floor rather than a
  judgement about the picture. The measured price of going to 0.75 is above, the frames are in the
  directory, and the decision is the teamlead's.
* **The cumulonimbus' Coverage slider still lies by 0.354**, measured above, and neither defect this phase
  fixed is its cause: it is the 6.000 km cell against a 48 km region, and both `kPackingCompensation` and
  the 0.68 alive exponent were fitted at 3.000 km with no cell dependence. It is the largest single thing
  left in the slider and it is a calibration with its own frames.
* **The Coverage slider's low end moved by three points.**
  `CloudProceduralField.CoverageIsTheFractionOfSkyThatHasCloudInTheColumn` measures −0.030 / −0.025 /
  −0.032 / +0.022 / +0.009 at 0.15 / 0.24 / 0.35 / 0.50 / 0.75 against the tenth it allows and the 0.025 it
  asserts near the top. It is inside both bounds, it is not a scale error (the sign flips), and refitting
  `kPackingCompensation` cannot remove it because the shape of the error changed rather than its slope.
  **Named as a trade rather than absorbed.**
* **It did not re-shoot §RW's twelve knob frames.** Every one is at a setting this task did not touch, and
  the knobs' ends are still the ends of the same sliders. What moved is what a cluster is made of.
* **It did not touch the march, the erosion, the lighting, or the coverage mapping.** No shader was edited
  and no default outside the generator changed.
* **It did not pursue §RW2's unexplained Z bump at 5.25–5.44 km**, which the brief put outside its scope.
  What is recorded above is the one new fact that fell out: it moves with the lump's shape.

## SIL2 — the lump goes to 0.75, and the erosion floor is paid for rather than moved, 2026-08-25

§SIL measured a trade honestly and refused to take it alone, because taking it meant re-calibrating another
phase's number. The teamlead took the decision; this is the other half of it. **Nothing here is a new idea —
it is §DS's own recipe applied a second time, to a bound §DS did not know it was setting.**

**The baseline is the shipped `dev` sky and it is proven rather than assumed.** All FIFTEEN baseline frames
shot for this task are **byte for byte identical** to the frames §SIL committed — the six protocol points
against `Shots/SIL_after_*.png`, eight genus frames against `Shots/SIL_genus_*_after.png`, and the congestus
genus frame against `Shots/SIL_after_mid_away.png` (§SIL's own note that the congestus scene IS
`Clouds_Demo`). So every "after" number below is attributable to this change and to nothing else.

**The repeat floor is ZERO and it was measured rather than quoted.** The first render in this fresh worktree
differs from the second; the second and third are byte for byte identical. That is §A1's correction
reproduced for the fourth time in this programme, and the first render was discarded before anything was
measured.

---

### WHAT MOVED, AND THE THING THAT IS NOT A NUMBER IS THE POINT

| | before | after |
|---|---|---|
| `Assets::kCloudLumpVerticalOverHorizontal` | 0.45 | **0.75** |
| `ECS::VolumetricCloudData::DetailStrength` | 0.40 | **0.65** |
| `Cirrus.decloudtype` Detail Factor | 0.625 | **0.3846154** |
| `Altocumulus.decloudtype` Detail Factor | 0.40 | **0.2461538** |

The first is what the teamlead asked for. The second is what it costs. The third and fourth are what the
second costs. **And the thing that moved which is not a number at all is that the first two are now ONE
calibration in the repository instead of two numbers in two files** — which is the only part of this that is
engineering rather than arithmetic.

---

### THE STRENGTH WAS MEASURED, AND THE ESTIMATE IT REPLACES WAS WRONG

The brief asked for §SIL's estimate to be checked by measurement rather than believed. It does not survive.

§SIL estimated *"restoring §DS's 1.11x headroom needs a strength of about **0.60**"* from two anchor points.
**Both anchors reproduce here to a tenth of a metre** — 116.7 m at 0.50 and 157.1 m at 0.80 — so the
measurement was right and the inference from it was not: even a straight line through that pair reaches
1.11x only at 0.66. The re-measured ladder, `Desert/Tests/Engine/CloudField` walking 48 x 48 columns through
the volume the 0.75 lump actually bakes:

| strength | 0.40 | 0.45 | 0.50 | 0.55 | **0.60** | **0.65** | 0.70 | 0.80 | 1.00 |
|---|---|---|---|---|---|---|---|---|---|
| surface travel | 101 m | 109 | 117 | 124 | **131** | **139** | 145 | 157 | 180 |
| against the 125 m march | 0.81x | 0.87x | 0.93x | 0.99x | **1.05x** | **1.11x** | 1.16x | 1.26x | 1.44x |
| opaque columns dissolved | 0.054 | 0.056 | 0.060 | 0.061 | **0.065** | **0.068** | 0.075 | 0.079 | 0.088 |

> **0.60 CLEARS THE FLOOR BY SIX METRES, WHICH IS THE CASE §DS LOOKED AT AND REFUSED BY NAME.** §DS chose
> 0.40 over 0.35 because *"0.35 clears it by ONE metre and 0.40 by fourteen… a one-per-cent margin would make
> the suite fail on any change to the generator that moved a body by a voxel."* At the 0.75 lump the same
> sentence reads: 0.60 clears it by six metres and **0.65 by fourteen**. The two calibrations land on the
> same headroom because they are the same rule applied twice — §DS shipped 139.1 m against 125, this ships
> **138.7 m against 125**.

---

### THE RE-BASE IS AN IDENTITY, AND IT IS VERIFIED ON THE FRAME WITH ONE VARIABLE ALONE

Raising the layer multiplies **every** type's cut by 1.625. §DS measured that the two thin types are
*deleted* rather than shredded by a deeper cut, and its repair is to divide their factors by the same ratio
so the product does not move:

| | authored | §DS | §SIL2 | the product |
|---|---|---|---|---|
| cirrus | 0.10 x 2.50 | 0.40 x 0.625 | **0.65 x 0.3846154** | **0.250000** |
| altocumulus | 0.10 x 1.60 | 0.40 x 0.40 | **0.65 x 0.2461538** | **0.160000** |

`Desert/Tests/Engine/CloudType.TheReBasedTypesKeepTheCutDepthTheirFilesWereAuthoredAt` asserts this on the
shipped assets rather than on the arithmetic in this table, and prints both products.

**AND IT IS PROVEN THROUGH THE WHOLE RENDERER, with the lump held at 0.75 in BOTH arms** — which is §DS's own
method (*"with the one variable it is about held alone"*) and is what makes it a statement about the re-base
rather than about the lump:

| control: layer 0.40 x old factor, against shipped: layer 0.65 x re-based factor | result |
|---|---|
| **cirrus** | **BYTE FOR BYTE IDENTICAL** (`cmp`) |
| **altocumulus** | **9 pixels of 980 480 differ (0.001 %), max delta 1/255** |

The nine pixels are the float-rounding floor of `0.65 x 0.2461538` against `0.40 x 0.40`, which are not the
same bits even though they are the same number. **The re-base changes nothing a viewer can see, and that is
measured rather than argued.**

---

### THE COUPLING, WHICH IS THE ENGINEERING AND NOT THE ARITHMETIC

> The lump's shape and the erosion's depth were two numbers in two files, and neither named the other.

They are not independent. A taller lump packs more density into a metre of ray, so the altitude at which the
optical depth first reaches 1 — the surface the eye puts the cloud at — sits at a **shallower profile**
(0.632 at the 0.45 lump, **0.576** at the 0.75 one), and a given cut moves that surface a **shorter
distance**. Detail Strength is fixed from below by a floor on exactly that distance.

**HOW THAT COST §SIL A COMMIT.** §SIL raised the lump to 0.75, measured the sky, shot the frames, wrote the
report and committed it. The travel had gone to 101 m against a 125 m floor. Nothing in the repository said
so until a full sweep of every suite — run for an unrelated reason — turned `CloudField` red, and §SIL's own
report says it *"would have shipped"*. §SIL then backed the lump down to the largest value that clears a
floor nobody had connected it to, and recorded the coupling **in prose**.

**Prose is not a relation.** What ships instead:

1. **The aspect is exported as one public symbol**, `Assets::kCloudLumpVerticalOverHorizontal`, and the
   generator's own constant is an alias of it rather than a second spelling.
2. **`CloudField.TheLumpsAspectAndTheErosionsStrengthAreOneCalibrationAndNotTwoNumbers`** reads that symbol
   *and* the component's default, bakes the volume the pair produces, and asserts what the pair delivers.
   Moving either number alone is red and the message names the other one.
3. **`CloudPlacementSpectrum.ALumpsHeightAndItsWidthAreOneQuantity` now asserts the emitted lumps MEASURE
   that symbol.** Without it the exported constant could drift from the generator and the coupling test would
   be checking the erosion against a number nothing in the sky uses. This was found by working through the
   sabotage list, not by a failure.

**THE THRESHOLD IS A WINDOW AND BOTH ENDS ARE MEASURED.** The physical bound is 1.00x. The shipped pair sits
at 1.11x. The floor of the window is **1.05x** — halfway between the bound being protected and the value
protecting it, so a body moving by a voxel does not trip it and a calibration quietly giving up its headroom
does. The ceiling is **1.35x**, and it exists because of a finding:

> ⚠️ **§DS'S OCTAVE CEILING HAS LOST THE BITE IT WAS WRITTEN FOR, AND THAT IS A CONSEQUENCE OF THIS CHANGE.**
> Its stated job is to catch "somebody raised Detail Strength and changed nothing else". Against the 0.45
> lump the top of the slider travelled 243 m — 1.94x, just inside the octave, so the bound only barely
> caught it. Against the 0.75 lump the top of the slider travels **180 m, 1.44x**, comfortably inside. The
> bound still fires, but now for the OPPOSITE drift — a lump made flatter without the strength coming down —
> because a flatter lump is optically thinner per metre and the same cut travels further. Its original bite
> was an accident of the aspect it was measured at. The 1.35x ceiling restores it: the shipped pair is a
> fifth below it and the slider's top a fifteenth above.

---

### THE FOUR THINGS THAT COULD NOT BE LOST, EACH CHECKED BY A NUMBER

**1. THE SKY'S COVER — kept, and the movement is named rather than rounded away.**
`LatticePeak --field`, 32 realisations, `Clouds_Demo`'s configuration: **0.7432 before, 0.7446 after.**
The before figure is the teamlead's own to four decimals. **The cover rises by 0.0014** — fourteen
ten-thousandths, 0.19 % relative, in the direction of MORE cloud. §SIL's ladder predicted this: it recorded
the cover rising from 0.7390 at 0.45 to 0.7409 at 0.75 on 8 realisations, and 32 realisations here reproduce
the same slope. **It is not zero and it is not rounded to zero; it is named as a trade below.**

**2. THE LATTICE ALONG THE WIND — kept at exactly zero.**

| | X (east, the wind's own axis) | Z (north) |
|---|---|---|
| before | **LATTICE 0.0000** | LATTICE 0.0117 at 5.625 km (2P), 1.9x noise |
| after | **LATTICE 0.0000** | LATTICE 0.0114 at 5.625 km (2P), 1.8x noise |

The Z figure is §RW2's unexplained bump, which §SIL showed is not a lattice; its prominence FALLS and its
position does not move. Asked at a 4.000 km cell (`--tile 16`, 24 realisations) the after field reports
**LATTICE 0.0000 on both axes**, which is §SIL's own cross-check reproduced.

**3. THE COVERAGE SLIDER PER GENUS — kept, and the four the teamlead named are all inside 0.06.**
No genus moves by more than **0.0095** (the cirrus; the next largest is the lenticular at 0.0031 and five of
the nine move by under 0.002). Against the slider's own 0.5, the four rows the brief lists read
**cirrus 0.508, lenticular 0.505, stratocumulus 0.521, altocumulus 0.556** — the worst is the altocumulus at
**+0.056**, which is inside 0.06 and is a shade tighter than the +0.058 §SIL shipped.

**4. THE EROSION'S HEADROOM — restored to §DS's own figure to the metre.** 138.7 m against a 125 m floor,
**1.11x**, against §DS's 139.1 m and §SIL's 139 m. `CloudField` is green.

---

### FORM BY FORM — WHAT THE LUMP'S SHAPE BUYS

Every row `LatticePeak --field --type <the shipped asset> --coverage 0.5`, 6 realisations, everything else
at the values that ship. **`Coverage` is 0.5, so a column that does not read 0.5 is the slider lying.**

| genus | cover before | cover after | **OPAQUE CORE** before / after | envelope before / after |
|---|---|---|---|---|
| cumulus humilis | 0.5279 | 0.5261 | 5.6 : 1 / **5.4 : 1** | 5.6 / 5.4 |
| cumulus mediocris | 0.5165 | 0.5172 | 4.0 : 1 / **3.7 : 1** | 3.9 / 3.7 |
| **cumulus congestus** | 0.5113 | 0.5130 | 2.7 : 1 / **1.8 : 1** | 1.3 / 1.5 |
| **cumulonimbus** | 0.8543 | 0.8543 | 4.4 : 1 / **3.3 : 1** | 1.0 / 1.1 |
| stratocumulus | 0.5220 | 0.5213 | 2.2 : 1 / **1.8 : 1** | 1.6 / 1.7 |
| stratus | 0.5065 | 0.5065 | 38.4 : 1 / 38.4 : 1 | 38.4 / 38.4 |
| altocumulus | 0.5581 | 0.5560 | 2.4 : 1 / **2.2 : 1** | 2.2 / 2.2 |
| cirrus | 0.4989 | 0.5084 | 1.8 : 1 / **1.4 : 1** | 1.3 / 1.4 |
| lenticular | 0.5016 | 0.5047 | 3.2 : 1 / **3.1 : 1** | 3.2 / 3.1 |

**THE SHIPPED CONGESTUS' OPAQUE CORE GOES FROM 2.7 : 1 TO 1.8 : 1 AGAINST AN ENVELOPE OF 1.5.** That is the
whole object of the task: §RW2 measured the defect as a core of 3.3 : 1 inside an envelope of 1.1 : 1 — a
ball of air with a plate of cloud through the middle — §SIL took the core to 2.8, and this takes it to 1.8.
**The core and the envelope now agree because the CORE came up to meet the envelope**, which is the arm §SIL
said it could not reach at 0.45 (*"at 0.45 the flat lens is corrected mostly by the body getting shorter"*).

> **AND THE CUMULONIMBUS MOVED, WHICH NEITHER EARLIER PHASE COULD DO.** Its anisotropy is 1, so neither of
> §SIL's two defects touched it and its core sat at 4.4 : 1 before and after. The lump's shape is not an
> anisotropy effect, so it reaches it: **4.4 : 1 to 3.3 : 1**, the second largest move in the table.

> **AND THE COVERAGE LIE IT CARRIES IS UNTOUCHED, exactly as before: 0.8543 for a slider of 0.5, to four
> decimals, in both arms.** §SIL named its cause (a 6.000 km cell against a 48 km region, with
> `kPackingCompensation` and the 0.68 alive exponent both fitted at 3.000 km and carrying no cell dependence)
> and this task did not touch it either. **It remains the largest single lie left in the Coverage slider.**

> **STRATUS DOES NOT MOVE IN THIS TABLE AND ITS FRAME DOES.** Its 400 m band puts every lump against the band
> clamp (a lump may not be taller than half its band) at both aspects, so the lump's shape cannot reach it —
> which is the band clamp doing exactly what its comment claims, and the claim is now checked by a genus
> rather than asserted. Its FRAME still moves, by the deeper erosion alone: 2.81 % of pixels, max 12/255.

---

### THE SIX POINTS

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, 1280x766, `ImageStat` over `0 0 1280 551`.
**The before column reproduces §SIL's published table on all six points to three decimals.**

| point | mean before / after | contrast before / after | sat before / after |
|---|---|---|---|
| zenith away `0,0.9,-1` | 0.598 / 0.570 | 0.386 / **0.415** | 0.079 / **0.098** |
| mid away `0,0.45,-1` | 0.552 / 0.534 | 0.388 / **0.395** | 0.172 / **0.174** |
| horizon away `0,0.12,-1` | 0.622 / 0.606 | 0.273 / 0.205 | 0.087 / 0.085 |
| **zenith sunward `0,0.9,1`** | **0.648 / 0.571** | **0.444 / 0.303** | 0.063 / **0.084** |
| mid sunward `0,0.45,1` | 0.601 / 0.560 | 0.297 / 0.254 | 0.099 / **0.117** |
| horizon sunward `0,0.12,1` | 0.621 / 0.590 | 0.277 / 0.248 | 0.102 / **0.113** |

**Saturation rises at five of six** — more blue in the sky — and the mean falls at all six, which is a
thicker body letting less light through it. **Contrast rises at the two away-from-sun angles a player looks
at and falls at the three sunward ones**, and the largest single move in the whole table is the sunward
zenith's −0.141. That one is a real cost and it has its own heading below.

| point | cloud fraction before / after | ragged before / after | lap r4 before / after |
|---|---|---|---|
| zenith away | 0.9274 / 0.3543 | 2.22 / **13.34** | 0.00527 / **0.00568** |
| mid away | 0.7313 / 0.7566 | 6.79 / 4.51 | 0.00724 / 0.00706 |
| horizon away | 0.5496 / 0.4493 | 41.64 / 41.32 | 0.02615 / 0.01886 |
| zenith sunward | 0.2163 / 0.1433 | 5.50 / **6.04** | 0.00530 / **0.00562** |
| mid sunward | 0.3919 / 0.1897 | 15.32 / **21.51** | 0.00779 / **0.01196** |
| horizon sunward | 0.4720 / 0.3048 | 44.16 / 43.93 | 0.02833 / 0.02593 |

> ⚠️ **THE `cloud fraction` COLUMN IS NOT TRUSTWORTHY HERE AND SAYING SO IS THE POINT.** `zenith away`
> reports 0.9274 falling to 0.3543, which reads as the sky emptying by two thirds. **It is not**: the frames
> are beside each other in `Shots/` and the sky/cloud split is plainly similar. The mask is an Otsu split,
> and Otsu's own threshold moved from 0.471 to **0.592** on that pair because the deeper erosion and the
> rounder bodies put a mass of new mid-tones into the histogram. A threshold that moves is a mask that
> measures something else. **The raggedness on the same frame moving 2.22 -> 13.34 is the honest reading of
> the same effect: the flat carpet broke into bodies with edges.** The sky's cover is measured on the
> VOLUME instead, above, and it moved by 0.0014.

---

### ⚠️ THE COST, NAMED: THE SUNWARD ZENITH LOSES ITS HOT SPOT

The single largest move in the six points is `zenith sunward`: mean 0.648 -> 0.571, contrast 0.444 -> 0.303,
**p95 0.965 -> 0.801**. Looked at rather than read off, `Shots/SIL2_before_zenith_sun.png` has a large blown
white region around the sun and `Shots/SIL2_after_zenith_sun.png` is a flatter, greyer overcast with the
bright region much reduced.

**The mechanism is the lump and not the erosion, and the sign says so.** A deeper cut REMOVES material and
would brighten a backlit deck; a taller lump makes the body optically thicker along the ray to the sun, so
less light comes through it. This is the same thickening that buys the opaque core, seen from the one
direction where it costs instead of paying.

**It is a trade and it is the teamlead's to accept or refuse.** What can be said for it: it is one of six
protocol points, it is the angle where a blown highlight was arguably too blown, and p95 0.801 is still a
bright sky. What can be said against it: a silver lining is a thing this programme has spent phases on
(§OE, §OE-FIX), and this spends some of it. **The measurement is here rather than in a footnote because
nothing else in the task moves a number this far.**

---

### ⚠️ THE SECOND COST, NAMED: THE CIRRUS IS LESS FIBROUS

The cirrus is the type the lump's shape reaches hardest, because a fibre is thin by definition:

| | before | after |
|---|---|---|
| mean / sat | 0.483 / 0.272 | 0.543 / **0.180** |
| ragged | 27.67 | **19.24** |
| lap r4 (texture inside the body) | 0.01306 | **0.00711** |
| its cover at a slider of 0.5 | 0.4989 | 0.5084 |

**The amount of cirrus in the sky is preserved — that is the re-base working — and its CHARACTER is not.**
`Shots/SIL2_genus_Cirrus_before.png` is a sky of thin combed strands; `_after.png` is the same bands with
the strands fattened into a broader veil. The type's own note says the fibre is the point of the file.

**This is the lump's shape and not the re-base**, and the control frames prove it: with the lump held at
0.75, the re-based file renders byte for byte what the old file rendered. **The path if the teamlead wants
the fibre back is the type's own `PlacementScale`, which is what decides a fibre's width, and it is content
work with its own frames** — it is NOT the lump aspect, which is a property of every genus at once.

---

### THE FRAMES

Every one at `--shot-frames 90`, camera `0,200,0`, 1280x766.

> ⚠️ **THE SET IS PRUNED RATHER THAN COMPLETE, AND THE DUPLICATE CHECK IS WHY.** Thirty-two frames were shot
> and **fourteen are committed**; the check this programme asks for found the other eighteen to be byte
> copies of pictures the repository already holds, which is the defect §SIL warned about by name. What was
> dropped, and what each omission is evidence OF:
>
> * **all fifteen baseline frames**, because each is byte-identical to a committed `SIL_after_*` /
>   `SIL_genus_*_after` frame. **That identity is the proof the baseline is `dev`'s sky** and is worth more
>   as a reported number than as fifteen duplicate files. The "before" column everywhere above is those
>   committed frames.
> * **`SIL2_genus_Cumulus_Congestus_after`**, byte-identical to `SIL2_after_mid_away` because the congestus
>   genus scene IS `Clouds_Demo` — §SIL's own reason for leaving that pair out.
> * **the shipped cirrus and the cirrus control**, both byte-identical to the ALREADY COMMITTED
>   `SIL_alt_aspect075_genus_Cirrus.png`. **That is the strongest single piece of evidence in this task and
>   it was found by the duplicate check:** §SIL shot that frame at this lump aspect with the OLD layer
>   strength and the OLD factor — a cut depth of 0.25 — and the shipped file renders the same 0.25 from
>   0.65 x 0.3846154. **A picture committed before this task began independently verifies its re-base.**

| file | what it shows |
|---|---|
| `Shots/SIL2_after_mid_away.png` (against `SIL_after_mid_away.png`) | **THE SHOW.** Before: flattened pills stacked in rows. After: round bodies with shoulders and cauliflower lobes |
| `Shots/SIL2_after_mid_sun.png` (against `SIL_after_mid_sun.png`) | the clearest single pair in the set: stacks of discs become turrets around a hole of blue |
| `Shots/SIL2_after_zenith_away.png` | the zenith, where the flat carpet breaks into separate masses with edges |
| `Shots/SIL2_after_zenith_sun.png` | **THE COST.** The hot spot around the sun flattens — see the heading above |
| `Shots/SIL2_after_horizon_away.png`, `SIL2_after_horizon_sun.png` | the far field, which had to not turn to dither and does not |
| `Shots/SIL2_genus_<genus>_after.png` | eight genera at `mid away`, from the committed `SIL_<genus>.desce` scenes; the cirrus is `SIL_alt_aspect075_genus_Cirrus.png` for the reason above |
| `Shots/SIL2_control_Altocumulus.png` | **the re-base isolated**: the lump held at 0.75, the layer driven back to 0.40 against the OLD factor. Nine pixels from the shipped frame |

**The genus frames are at ONE elevation, which is §SIL's stated limitation carried forward** for the same
reason: a procedural layer cannot be framed per type without moving the camera between arms, and a camera
that moves between arms is not a comparison.

---

### `LineJump` FINDS NOTHING NEW

Over `2 2 1278 551`, both arms, all six points. Away from the ground line every row maximum sits at
0.0014–0.0036 and every column maximum at 0.0014–0.0055, against the 0.010 that means "something to look
at". The one large figure — 0.096 at y 540 on both horizon frames — is the checker floor's own edge and it
moves by 0.0003 between arms. **The deeper erosion did not put banding or streaking into the far field.**

---

### THE RELATIONS ADDED

| relation | where | what it would catch |
|---|---|---|
| the lump's aspect and the layer's Detail Strength deliver a surface travel inside [1.05, 1.35] x the march's chord | `CloudField` | either number moved without the other — the defect this whole task exists for |
| a deeper cut always moves the surface further | `CloudField` | the erosion ceasing to be the lever both files say pays for the lump, which would make the recipe advice that does nothing |
| the pair does not dissolve more than 0.15 of the opaque columns | `CloudField` | a future aspect paid for with a cut deep enough to put holes through the deck |
| the two re-based types' `strength x factor` is the depth their files were authored at | `CloudType` | a third task raising the layer and re-basing one file, or neither |
| the emitted lumps MEASURE the exported aspect constant | `CloudPlacementSpectrum` | the generator going back to spelling the shape out for itself, which would leave the coupling test checking a number nothing renders |

#### TWELVE SABOTAGES, AND ONE OF THEM STAYED GREEN

Every one applied to the shipped source, the suite's **objects and binary deleted**, rebuilt, run and
reverted. The numbers elsewhere in this section come from a binary rebuilt after the last revert.

| break | result |
|---|---|
| the lump aspect is raised to 0.90 and the strength left alone | RED |
| the lump aspect is dropped back to 0.45 and the strength left alone | RED — the ceiling, from the other side |
| the strength is dropped back to §DS's 0.40 | RED (two tests) |
| the strength is dragged to the top of its slider | **RED — and ONLY in the new window** |
| the strength is moved one ladder step, 0.65 → 0.70 | GREEN — intended: inside the window, and not a defect |
| the cirrus keeps the factor §DS re-based it to | RED |
| the altocumulus keeps the factor §DS re-based it to | RED |
| the generator spells the aspect out for itself while the header moves | RED |
| the erosion field is dead | RED |
| the shader stops reading the layer's Detail Strength | RED |
| **the exclusion threshold goes back to the literal `0.6 * 0.75`** | **GREEN — a real hole, closed** |
| the same break, re-run after the hole was closed | RED |

**THE GREEN ONE IS THE FINDING, AND IT IS A DEFECT OF THE FIXTURE RATHER THAN OF THE ASSERTION.** The
threshold that drops the one lump per cluster the base ramp deliberately reshapes used to be written
`0.6 * 0.75` — 0.45, the aspect of the day, spelled out in a form that named neither the constant nor the
fact that it WAS the constant. Putting that literal back at the new aspect left the suite green **even with
the new assertion against the exported constant in place**, and the reason is the fixture: its Base Ramp
Fraction is the shipped congestus' 0.04, which puts the ramped lump at an aspect of 0.39 — still below a
stale 0.45, so the stale threshold still happened to exclude exactly what it was meant to exclude.

The arm added uses **0.25, which is the cirrus' own authored value** and not a number invented to break a
test: it puts the ramped lump at 0.469, above a stale 0.45, so a threshold that has not moved with the
constant averages that lump in and drags the mean to about 0.70. Re-run, the same sabotage is RED.

> **AND THE FOURTH ROW IS WHY THE WINDOW EXISTS.** Dragging Detail Strength to 1.00 turns the new relation
> red and leaves §DS's own octave ceiling GREEN — the two ran in the same binary and only one of them fired.
> That is the hole named above, demonstrated rather than argued.

---

### ⚠️ THE SHIPPED SKY IS NOT THE FRAME THE DECISION WAS MADE ON, AND HERE IS THE DIFFERENCE

The teamlead judged `Shots/SIL_alt_aspect075_mid_away.png` — §SIL's 0.75 lump at §DS's **old** strength of
0.40, which is the arm that fails the erosion floor. What ships is that lump with the floor paid for. The
two differ by **77.4 % of pixels, max delta 64/255, mean delta 3.2/255 over the pixels that moved**: the
bodies and their proportions are the ones that were approved, and the deeper cut adds scallops and edge and
takes a little off each body. **The approved character is preserved; it is not the same file, and saying so
is the point.**

---

### THE WHOLE SWEEP, IN BOTH CONFIGURATIONS

`CI=true premake5 gmake`, then **every generated makefile built and run with the objects and the binaries
deleted first**, in Debug AND in Release.

| | debug | release |
|---|---|---|
| makefiles generated | **80** | **80** |
| excluded as tools and libraries | **16** | **16** |
| suite makefiles | **64** | **64** |
| suite binaries built | **64** | **64** |
| missing binaries | **none** | **none** |
| `not-a-suite` lines | **none** | **none** |
| `SKIPPED BUT IS A TEST` lines | **none** | **none** |
| suites failed | **none** | **none** |

> **THE SWEEP FOUND THIS TASK'S REGRESSION, EXACTLY AS IT FOUND §SIL's.** The first Debug sweep reported
> `FAIL ComponentReflection` — `VolumetricCloudReflection.DefaultsAreTheOnesTheComponentArguesFor`, which
> pins the component's defaults and per-category census and has nothing to do with clouds in its name. It
> was fixed with the number and its argument rather than by loosening the assertion. **That is twice in two
> phases that the only thing between this calibration and a bad merge was running every suite.**

**The exclusion list is §SIL's** — §RW's with `LatticePeak` in it — and **it is still not the one written in
`DEV_CONTRACT.md` §2.4 item 5a**. That file is the teamlead's and §1.6 says a foreign file is asked for
rather than taken. The change needed is still one word: `LatticePeak` beside
`ImageStat|LineJump|SceneMigrator`.

---

### WHAT THIS TASK DID NOT DO, AND THE TRADES IT IS ASKING TO HAVE ACCEPTED

* **THE SUNWARD ZENITH LOSES ITS HOT SPOT** — mean 0.648 → 0.571, contrast 0.444 → 0.303, p95 0.965 → 0.801.
  Measured, framed and named above. **This is the largest single move in the task and it is a cost.** It is
  the lump's own thickening seen from the one direction where thickness does not pay, and no setting of the
  erosion recovers it.
* **THE CIRRUS IS LESS FIBROUS** — its texture inside the body halves, 0.01306 → 0.00711, and its saturation
  falls 0.272 → 0.180. The AMOUNT of cirrus is preserved by the re-base to a hundredth; its character is
  not, and the cause is the lump's shape rather than the erosion. The path back is the type's own
  `PlacementScale`, which decides a fibre's width, and it is content work with its own frames.
* **THE SKY'S COVER RISES BY 0.0014**, 0.7432 → 0.7446. Named rather than rounded to zero: it is the
  direction of MORE cloud, it is 0.19 % relative, and §SIL's own ladder predicted the slope.
* **THE CUMULONIMBUS' COVERAGE SLIDER STILL LIES BY 0.354**, unchanged to four decimals in both arms. §SIL
  measured its cause — a 6.000 km cell against a 48 km region, with `kPackingCompensation` and the 0.68
  alive exponent both fitted at 3.000 km and carrying no cell dependence — and neither phase has fixed it.
  **It remains the largest single lie left in the Coverage slider**, and it is a calibration with its own
  frames.
* **IT DID NOT RE-DERIVE THE `(1 - Profile)` WEIGHT**, and there is a number here worth recording because it
  moves in the helpful direction for once. §DS's ceiling is that the ray sees a cloud at a profile of 0.694
  and only `1 - 0.694 = 0.306` of the nominal cut reaches the surface the eye looks at. Measured on this
  worktree the surface sits at **0.632 before and 0.576 after**, so the weight there rises from 0.368 to
  **0.424**: a taller lump delivers MORE of the cut to the visible surface. It is still not a shredded
  silhouette, and re-deriving that weight against the optical surface is still a design change with its own
  frames.
* **IT DID NOT RE-SHOOT §RW's TWELVE KNOB FRAMES.** No knob's ends moved; what moved is what a lump is.
* **IT DID NOT TOUCH THE MARCH, THE LIGHTING OR THE COVERAGE MAPPING.** One shader file was read, none was
  edited, and no default outside the two named above changed.
* **IT DID NOT PURSUE §RW2's UNEXPLAINED Z BUMP.** It is still at 5.625 km, its prominence fell 0.0117 →
  0.0114, and at a 4.000 km cell both axes still report LATTICE 0.0000.

---

## PT — the sky can be PAINTED, and the march did not learn a single new instruction, 2026-08-25

The owner asked two things in one breath: *«будет ли у нас типа редактора материалов для облаков как в UE?
чтобы я мог загрузить например текстуры шума для формы облаков итд»*, and separately, about the
arrangement of clouds in the sky, *«есть ли какой-то параметр, чтобы можно было ещё и вручную это
делать?»*.

The first is refused and stays refused — **decision D-5, no cloud material graph**. The second is this
phase. They are not the same request: Unreal's `Layout_CloudGlobalPattern` and `Layout_GlobalCloudMask`
are **data**, and data is the one part of that material we can take without taking the graph. The same
formula this programme has already applied three times (`.decloudtype`, `.dcnv`, `.dcmv`): Unreal's
semantics, our formats.

### The research came first, and its most useful finding was about method

`Docs/Clouds/RESEARCH_LAYOUT_TEXTURES.md`. Three facts from the engine source decided where the painting
enters, and none of them is about the layout textures at all:

| what | where |
|---|---|
| the material is evaluated **once per march step AND at seven further sites on the shadow rays and the shadow map** | `VolumetricCloud.usf:858, 903, 991, 1012, 1092, 1106, 1181, 1317, 1920, 2186` |
| altitude is **spherical**, the horizontal lookup is whatever the graph makes of a raw world position in centimetres | `VolumetricCloudMaterialPixelCommon.ush:50-51, 61-64` |
| UE has **no weather patch, no coverage, no placement** on the component — the whole shape surface is one `TSoftObjectPtr<UMaterialInterface>` | `VolumetricCloudComponent.h:71` |

Epic reads three textures inside the hottest loop of the frame because it has no precomputed field: the
material *is* the field. **We have one.** So the painting joins where placement is already decided — once
per lattice cell, on the CPU, at bake time — and the march is not touched.

> ⚠️ **AND ONE METHOD ERROR, RECORDED BECAUSE IT IS THE REUSABLE PART.** The research reported that
> `m_SimpleVolumetricCloud.graph.txt` was "not in the tree", on the evidence of
> `git log --all -- '*graph.txt'`. The file had been on disk for a week. It is in `.gitignore` **on
> purpose** (line 113): the waiver covers USING Epic's material, not committing it verbatim. The tool
> answered honestly, to a different question. **"Not in the history" and "not on disk" are two claims and
> the second needs a second tool.** All four load-bearing graph claims were then re-verified node by node
> — §8 of the research document — and all four hold, one with a correction that sharpens the reference
> rather than the design (the fourth cloud type is excluded from the `max` **structurally**, by an
> RGB-only mask on the multiply, not by being handled elsewhere).

### Where it enters, and why that is the seam rather than a way around it

`CloudProceduralVolume.cpp` decides a cell's coverage in one place. Before this phase that place read the
slider and folded in the procedural patch field. It now calls **`CloudCellCoverage`**, and that function is
the whole of the design:

* **The painted pattern and the patch field decide the same number, so they are not both applied.** The
  painting is the source when one is bound and turned up; the hash is the source otherwise. Two mechanisms
  setting one value is the second path §1.3 and §4.2 forbid, and this is the first time this subsystem has
  had two candidates for one number.
* **The pattern is applied ZERO-MEAN.** A bright painting must redistribute cloud, not add it, or
  `Coverage` stops meaning the fraction of sky it delivers — and that mapping is what decision **D-20**
  re-authorised every shipped scene against.
* **The mask is deliberately asymmetric.** "Add cloud here, take it away there" is what a mask is for, and
  a symmetric one could not do it. It is safe for D-20 in a way the pattern is not, because a layout with
  no mask table contributes exactly nothing.

Nothing above `SampleCloudField` changed, and no shader file changed at all. **The painting reaches the
march as the contents of the volume the march already fetched once.**

### The one real constraint, and it is expressed as a TYPE rather than as a check

The modelling volume must be exactly periodic over the region — everything past the region is REPEAT
sampling of it, and the wrap seam is measured at **0.950/255 against 1.239/255** between ordinary
neighbours (§RW). A world-anchored painting whose period did not divide the region would put a hard
discontinuity across every region face.

So `Layout Repeats` is a **whole number** of repeats per region and `Layout Rotation` is a count of
**quarter turns**. A square lattice maps onto itself under a quarter turn and under nothing else. Neither
constraint can be spelt wrongly, which is the difference between a property of the type and a validator
somebody has to remember (§2.3.1).

**Epic does neither**, and the divergence is named rather than inherited: `Layout_CloudGlobalScale` is a
float in kilometres (256 by default, and the parameter's own `Desc` says "in km"), and
`MaterialExpressionRotator_1` turns by a free angle. Epic can afford both because it has no baked volume
to keep in step.

### THE PICTURE: the sky follows a shape somebody drew

| file | what it shows |
|---|---|
| `Shots/PT_letter_topdown.png` | **THE SHOW.** A capital D drawn out of cloud, seen from 90 km, and REPEATING exactly with the region — the periodicity relation showing up in a picture as well as in a test |
| `Shots/PT_stripe_mid_away.png` | the same claim at the elevation the game is played at: clear blue on one side of a painted boundary, a wall of cloud on the other |
| `Shots/PT_stripe_horizon_away.png` | the far field, which had to not turn to dither and does not |

The letter reads MIRRORED, and that is the top-down view rather than a defect: looking straight down
reverses one axis.

**`PT_Layout_LetterD` is authored for LEGIBILITY and says so here rather than pretending to be the shipped
sky**: `Cloud Scatter` 0.25 against the shipped 1.0, `Cloud Density` 3.0 against 1.75, `Cloud Size Variety`
0.35 against 0.75. The shipped placement deliberately lets a cloud wander a full cell — that is what §RW
bought to remove the lattice — and a cloud that wanders a full cell cannot stay inside a stroke 4.8 km
wide. So the demonstration tightens the placement, and the interaction is the point: **the painting says
WHERE, and the placement knobs decide how faithfully the clouds keep to it.** The first letter frame, at
the shipped scatter, is legible as a shape and broken as a glyph.

### THE SKY THAT SHIPPED DID NOT MOVE — SIX OF SIX, BYTE FOR BYTE

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, 1280x766, no layout bound. Every one of the six
protocol points is byte-identical to the frame **committed before this task existed**:

| point | md5 | equals |
|---|---|---|
| zenith away `0,0.9,-1` | `73c7806b04c1c71317e4aba52e3f20dc` | `SIL2_after_zenith_away.png` |
| mid away `0,0.45,-1` | `4819e9c0c6dcdfadbf7477bd90a409d7` | `SIL2_after_mid_away.png` |
| horizon away `0,0.12,-1` | `304f4c2b56ea4751f50b6eb7b6351b4e` | `SIL2_after_horizon_away.png` |
| zenith sunward `0,0.9,1` | `ada3c729466065ad749a473698b2f903` | `SIL2_after_zenith_sun.png` |
| mid sunward `0,0.45,1` | `4a2ddc2a6a5bd7637701fd1a3fe7da8b` | `SIL2_after_mid_sun.png` |
| horizon sunward `0,0.12,1` | `bfd06fce1094adaa4e538cebba2f66f7` | `SIL2_after_horizon_sun.png` |

**SIX and not five**, which is a step up from §A0 and §A2, and the reason is structural rather than lucky:
those phases moved 199 pixels because a SHADER FILE changed and the SPIR-V was rescheduled.
`git diff --name-only origin/dev HEAD -- Editor/Resources/Shaders` returns **nothing at all** here.

The six frames are **not committed**: they are byte copies of pictures the repository already holds, which
is the duplicate check §SIL warned about. The identity is the evidence.

### THE PRICE, and the march's own line

`--gpu-profile`, `Clouds_Demo` / `PT_Layout_Cost` (which is `Clouds_Demo` **plus a painting and nothing
else**), camera `0,200,0`, `--look 0,0.45,1`, 400 frames, Debug, interleaved, machine shared:

| run | `Clouds: March` |
|---|---|
| unpainted | 13.961 ms |
| painted | 9.363 ms |
| unpainted | 13.663 ms |
| painted | 9.350 ms |

**The painted march is a third FASTER — 9.35 against 13.66 ms.** That is not an optimisation and must not
be read as one: the stripe empties half the sky, so half the rays leave the layer early. It is §RW's own
finding in the other direction — *the march meeting a different volume*.

The unpainted pair brackets the noise at **0.298 ms**, so the fall is fifteen times it.

> **The absolute numbers are about twice §GT's published 7.589 ms and the difference is the MACHINE, not
> the change.** This ran on a box that was building three configurations at the time. The A/B is
> interleaved on that same box within four minutes, which is what makes the comparison sound and the
> absolute figure not comparable to §GT's.

**And the claim the measurement is only corroborating: the march gained no instruction.** Zero shader
files differ from `dev`, and the six unpainted frames are byte-identical — so the unpainted march provably
performs the identical work. What a painting changes is how much cloud the rays meet.

### The knobs, and the pair that came back IDENTICAL

Ten arms on `PT_Layout_LetterD`, each differing from the shipped scene by ONE field, all at
`0,9000000,0` / `--look 0,-0.995,0.05`.

| knob | range, default | low | high |
|---|---|---|---|
| Layout Pattern Strength | 0..1, **1.0** | `Shots/PT_knob_pattern_low.png` | `Shots/PT_knob_pattern_high.png` |
| Layout Mask Strength | 0..1, **1.0** | = `PT_knob_pattern_high.png` | = `PT_letter_topdown.png` |
| Layout Repeats | 1..16, **1** | = `PT_letter_topdown.png` | `Shots/PT_knob_repeats_high.png` |
| Layout Rotation | 0..3 quarter turns, **0** | = `PT_letter_topdown.png` | `Shots/PT_knob_rotation_high.png` |
| Layout Offset | km, **(0,0)** | = `PT_letter_topdown.png` | `Shots/PT_knob_offset_high.png` |

> ⚠️ **THE PATTERN PAIR WAS BYTE-IDENTICAL ON THE FIRST RUN — both ends `044b9e33` — AND THE REASON IS
> WORTH MORE THAN THE FRAME.** It is not a dead knob. The MASK was at full strength in both arms, and the
> mask alone already drives the letter's interior over 1 and its exterior under 0; the clamp then eats the
> pattern's entire contribution. Shot again with the mask off, the pair separates (`5bd98087` against
> `35afc1d8`) — and the difference is **visible but weak**, which is a property of the construction rather
> than an accident: the pattern is a redistribution about the painting's OWN MEAN, so a sparse figure —
> mean **0.1693** here — can only pull the background down to 0.66 of the slider. Emptying the sky is the
> mask's job. **The two controls interact, and an artist has to be told so.**

**AND `PT_knob_repeats_high.png` NAMES A LIMIT THE VALIDATOR DOES NOT CHECK.** At 4 repeats the letter's
world period is 12 km and its stroke is 1.2 km — narrower than the 3.0 km placement cell — so the glyph
collapses into evenly spaced clumps. It still repeats four times as often, which is what the knob promises
and what the frame shows; what it stops doing is reading as a letter.

`ValidateCloudProceduralLayout` checks one layout TEXEL against the cell, and that is the right bound for
"can the painting distinguish two neighbouring cells". **It is not the bound for legibility, which is the
painting's thinnest STROKE against the cell** — a quantity no validator can measure without knowing what
the artist drew. Reported rather than guarded, because guessing at a stroke width is exactly the kind of
opinion about somebody's picture this format refuses to take elsewhere. The knob's ceiling of 16 is
therefore honest about periods and silent about pictures.

Ten arms produced **five distinct configurations**, so five files are duplicates and are not committed.
Every "low" arm of repeats, rotation and offset is byte-identical to the shipped letter frame; mask-off is
byte-identical to pattern-on-mask-off.

### The relations added, and the breaks that verified them

All in `Desert/Tests/Engine/CloudPlacementSpectrum` unless stated.

| relation | broken by | what the break did |
|---|---|---|
| binding a painting, and binding a DIFFERENT one, make the cached volume stale | dropping the content hash from `CloudProceduralParamsEqual` | RED |
| a field added to the bake's parameters forces a visit to the staleness walk | adding `float SabotageTwo` | **COMPILE ERROR** — "decomposes into 19 elements, but only 18 names were provided" |
| the painting repeats exactly with the region at every rotation and offset | rotating by 45 degrees instead of 90 | RED, 0.414 of a period against a bound of 1e-3 |
| a painted pattern redistributes the sky rather than adding to it | deleting the zero-mean subtraction | RED, sky at Coverage 0.50 went 0.498 -> **0.658** against an unpainted 0.532 |
| exactly one source modulates a cell's coverage | applying the patch field alongside the painting | RED |
| with no painting bound the five layout knobs cannot reach one cloud | letting the offset displace the patch field | RED |
| a v6 file's defaults are a layer with no painting (`SceneCloudLayoutDefault`) | moving Layout Pattern Strength's default to 0.5 | RED |

> ⚠️ **TWO SABOTAGES STAYED GREEN AND BOTH ARE FINDINGS.**
>
> **The first was a defect in my own guard.** It pinned `sizeof(CloudProceduralFieldParams)` against the
> hand-written staleness walk. Adding a float left the size at **136** — it landed in the padding before
> the `shared_ptr` — so the guard could not go red for the one thing it existed to catch. **A size is not
> a field count, and the two agree only until the next field happens to fit in a hole.** Replaced by a
> structured binding, which names all eighteen fields and turns the same sabotage into a compile error.
> Recorded rather than quietly swapped, because a guard that cannot go red is worse than no guard: it also
> stops anyone writing a real one.
>
> **The second disproved a comment I had written.** I claimed that spelling the quarter turn as a float
> `cos`/`sin` matrix would break the periodicity "by a whole texel at 4000 km". Measured out to 12 345 km:
> **1.9e-6 exact against 3.8e-6 through cos/sin**, and the test correctly stayed GREEN. The relation is
> about the ANGLE being a quarter turn, not about how the turn is spelt. The comment is corrected in place.

### The format, the memory and the two shipped paintings

| | |
|---|---|
| `Layout_Stripe.dclayout` | 512 squared, pattern only, **1 048 624 B (1.00 MiB)**, content `1c8566c8`, channel means 0.4981 |
| `Layout_LetterD.dclayout` | 512 squared, pattern **and** mask, **1 310 768 B (1.25 MiB)**, content `4a286994`, channel means 0.1693 |

Against §A0's arithmetic — 20.67 MiB occupied, 8.00 MiB for the procedural volume, 4.00 MiB per sculpted
body — a painting is the cheapest thing in the subsystem, and D-9's 64 MiB is untouched in practice.

### The whole sweep, in both configurations

`build/Bin` and `build/Tests` deleted first, `CI=true premake5 gmake`, then every `*.make` that is not a
third-party library or an aggregate — built, and run if it produced a test binary.

| | Debug | Release |
|---|---|---|
| BUILD-FAIL | none | none |
| FAIL | none | none |

**Four `not-a-suite` lines in each, and all four are our own instruments:** `ImageStat`, `LatticePeak`,
`LineJump`, `SceneMigrator`. A fifth name is a tool this task added — `CloudLayoutBaker` — and it is
excluded in the loop's `case`, which is **list extension, the thing §2.4 item 5a says went stale four
times**. So it was DERIVED by hand instead and the answer reported: it builds, it produces
`build/Bin/Debug/CloudLayoutBaker` and no test binary, so it is a tool.

> ⚠️ **THE FIRST SWEEP FOUND FIVE PROJECTS THAT COULD NOT LINK**, and it is the reason the sweep is run
> over every makefile rather than over the ones whose name resembles the task: `CloudField`,
> `CloudProceduralField`, `CloudAuthored`, `CloudType` and `Tools/LatticePeak` all compile
> `CloudProceduralVolume.cpp`, which now calls into `CloudLayout.cpp`. Four suites and one instrument, none
> of them named after this phase.

**A correction to the order, stated because it is a hole in the method rather than in the result.** Four
comment strings and one tooltip were fixed AFTER the debug sweep and part-way through the release one.
They are inert — a tooltip's text and four comments — but "inert" is a claim, so the four projects that
read the reflection table were rebuilt and re-run in **both** configurations afterwards:
`ComponentReflection` 31/31, `SettingConsumers` 10/10, `SceneCloudLayoutDefault` 3/3,
`CloudPlacementSpectrum` 32/32.

### What this task did NOT do, and why it is here rather than in a commit message

* **`Layout_CloudHeightProfile` is not taken**, in either the 2D or the 1D form, and the teamlead approved
  the refusal. Unreal needs a `f(altitude, pattern value)` table because its placement field is
  two-dimensional and the table is its only vertical structure. Ours is geometry, and `fill` in
  `CloudProceduralVolume.cpp` already carries exactly that dependency — the comment there names D-13. A
  painted table would be a second way to state one thing (§2.3.1) and would fight §SIL2, where the lump's
  aspect and the erosion's strength are one calibration guarded by a test that names both numbers.
* **No scene-schema version, and no migration function.** The phase adds six fields and renames none; an
  absent key is already how the reflected serializer spells "the C++ default", so a v6 to v7 that returned
  zeros would be the stub §1.2 forbids. What replaces it is `SceneCloudLayoutDefault`, which asserts the
  consequence: a v6 payload deserialises into a layer that binds no painting and into lumps identical, one
  by one, to the struct as it was before the fields existed.
* **The thing that BAKES a painting from an image is `Tools/CloudLayoutBaker`, not a window.** The Details
  slot, the picker and the drag-and-drop target exist and are wired; an authoring *window* with a preview
  is not in this phase, and the owner's own words — "load a texture" — are served by the slot.
* **The layout is not per-species-scaled.** `Layout_CloudPerTypeScale` has no counterpart because
  `PlacementScale` and `PlacementAnisotropy` already live on `.decloudtype` (T3). Adding a second scale
  would be two numbers obliged to agree.

---

## CB — the cumulonimbus' slider was never about its cell, it is the ANVIL, 2026-08-25

The teamlead handed this phase the largest lie left in the `Coverage` slider — **the cumulonimbus delivers
0.856 of the sky for a slider of 0.5, +0.354, unchanged through §SIL and §SIL2** — together with a stated
cause: `kPackingCompensation` and the 0.68 alive exponent are both fitted at the 3 km cell and carry no cell
dependence, and the cumulonimbus is the only type on a 6 km cell. The brief asked for the hypothesis to be
checked across all nine genera **first**, and said in as many words that refuting it is worth more than
confirming it.

**IT IS REFUTED, AND ONE PAIR OF RUNS IS ENOUGH TO DO IT.**

### THE CELL IS EXONERATED BY FOUR NUMBERS

`LatticePeak --field --coverage 0.5 --repeats 8`, everything else at the values that ship. The only thing
that changes between the rows is named in the row.

| arm | cell | sky at `Coverage 0.5` | slider out by |
|---|---|---|---|
| cumulonimbus, exactly as it ships | 6.000 km | **0.8561** | **+0.356** |
| cumulonimbus with `AnvilStrength` 0.85 → 0, **same 6 km cell** | 6.000 km | **0.5351** | **+0.035** |
| congestus with `PlacementScale` forced to 2.0, **the cumulonimbus' own cell**, no anvil | 6.000 km | **0.5323** | **+0.032** |
| cumulonimbus with `PlacementScale` forced to 1.0, **the reference 3 km cell**, anvil on | 3.000 km | **0.8334** | **+0.333** |

**The lie follows the anvil and not the cell.** At one and the same 6.000 km cell the error is +0.356 with
the anvil and +0.035 without it; at the reference 3.000 km cell — the very cell both constants were fitted
at — the cumulonimbus still lies by **+0.333**. A cell-size defect cannot survive being put back on the size
it was calibrated at, and this one does.

**And the library already contained the counter-example.** `stratus` is on a **12.000 km** cell, four times
the reference and twice as far from it as the cumulonimbus in ratio, and it reads **0.5098** — dead on. The
row that would have had to be worst is the best in the table.

### AND THE INSTRUMENT WAS CUTTING THE CANOPY OFF, so the shipped lie is BIGGER than either phase recorded

`Tools/LatticePeak --field` built its layer as `TopAltitudeKm - BaseAltitudeKm`. The renderer does not:
`VolumetricCloudRenderer::BuildProceduralParams` calls `Graphic::CloudTypeSetEnvelopeKm`, whose top is
`max(TopAltitudeKm, AnvilAltitudeKm + AnvilThicknessKm)` — because a type's second lobe stands ABOVE its
tower and a shell that stopped at the tower would cut it off.

**For eight of the nine genera the two agree exactly. For the cumulonimbus they are 8.10 km against
10.40 km**, and the slice the instrument was throwing away is the canopy's WIDEST. It is the same
two-places-that-must-agree shape the instrument was built to hunt, one level up, and it is fixed by calling
the shared function rather than by recomputing the shell in the tool.

| the cumulonimbus at `Coverage 0.5` | sky |
|---|---|
| as §SIL and §SIL2 measured it, canopy clipped at 9.00 km | 0.8561 (**+0.356**) |
| the same binary, shell as the renderer builds it | **0.8883** (**+0.388**) |

> **THE NUMBER OF RECORD FOR THE SHIPPED LIE IS +0.388, NOT THE +0.354 TWO PHASES PUBLISHED**, and what
> the old figure was is worth naming rather than crossing out: it is the sky of a cumulonimbus **whose
> canopy has been cut off above 9.00 km** — a real quantity, correctly measured, of a body the engine does
> not draw. **It is §GT's shape exactly**: an instrument that measured the wrong thing accurately, so every
> digit of it was reproducible and none of it was the answer. The instrument would have been right for
> eight of the nine genera and wrong only for the one under test, which is why nothing caught it.

### FORM BY FORM, ALL NINE, AND EIGHT OF THEM DO NOT MOVE AT ALL

`LatticePeak --field --type <the shipped asset> --coverage 0.5`, **8 realisations**, both arms on the
envelope-corrected instrument so the two columns are the same measurement. `Coverage` is 0.5, so a column
that does not read 0.5 is the slider lying by that much.

| genus | cell (km) | sky before | sky after | **slider out by, before → after** |
|---|---|---|---|---|
| cumulus humilis | 1.500 x 1.500 | 0.5197 | 0.5197 | +0.020 → +0.020 |
| cumulus mediocris | 2.400 x 2.400 | 0.5123 | 0.5123 | +0.012 → +0.012 |
| cumulus congestus | 3.000 x 3.000 | 0.5122 | 0.5122 | +0.012 → +0.012 |
| **cumulonimbus** | **6.000 x 6.000** | **0.8883** | **0.5445** | **+0.388 → +0.045** |
| stratocumulus | 1.328 x 0.830 | 0.5228 | 0.5228 | +0.023 → +0.023 |
| stratus | 12.000 x 12.000 | 0.5098 | 0.5098 | +0.010 → +0.010 |
| altocumulus | 1.102 x 0.735 | 0.5521 | 0.5521 | +0.052 → +0.052 |
| cirrus | 5.091 x 0.636 | 0.5124 | 0.5124 | +0.012 → +0.012 |
| lenticular | 1.073 x 5.367 | 0.5077 | 0.5077 | +0.008 → +0.008 |

**Eight of the nine rows are identical to four decimals, and that is not luck — it is the shape of the
change.** `CloudClusterFootprintGain` returns exactly 1.0 for a type with no canopy, so the arithmetic those
eight genera go through is bit-for-bit the arithmetic that shipped. The four genera the brief guards read
**stratocumulus 0.5228, altocumulus 0.5521, cirrus 0.5124, lenticular 0.5077** in BOTH columns.

**AND THE CELL SPANS 0.90 km TO 12.000 km ACROSS THAT TABLE — THIRTEEN TIMES — WITH EVERY ROW BUT ONE
INSIDE 0.052.** The genus on the coarsest cell in the library is the second most accurate row in it.

### THE CELL LADDER, ONE GENUS, THE CELL THE ONLY THING THAT CHANGES

The table above still confounds the cell with the genus, so it is asked again of the shipped congestus at
five settings of its `PlacementScale`, `Coverage 0.5`, 8 realisations, nothing else touched:

| cell | 1.500 km | 2.400 km | 3.000 km | 6.000 km | 12.000 km |
|---|---|---|---|---|---|
| sky delivered | 0.5016 | 0.5052 | 0.5122 | 0.5323 | 0.5026 |
| out by | +0.002 | +0.005 | +0.012 | +0.032 | +0.003 |

**Eight times of cell for a spread of 0.030, and NOT MONOTONE.** There is no cell law to fit: the widest
cell is the second most accurate setting, and what is left is the estimator's own wobble on a region that
holds four coarse cells across. **The cell can account for at most +0.032 of the cumulonimbus' +0.388.**

**And it is derivable that it should be so.** The clusters per unit area go as `1/cell²` and one cluster's
footprint as `cell²`, so the cell cancels out of the expected cover exactly. A cell term added to
`kPackingCompensation` would have been a constant fitted at a second size — the same mistake one level
along — and it would have moved all nine genera to buy 0.03 on one.

### WHAT THE CAUSE ACTUALLY IS, AND IT IS A CLOSED FORM

The Coverage mapping is a statement about the AREA one cluster covers, and the generator already holds that
area still against three of the four things that could move it: the density (`kDensityCompensation`), the
size spread (a draw uniform in AREA) and the anisotropy (§SIL's geometric mean). **The fourth is the type's
own anvil, and nothing compensated it.**

* The canopy is ONE solid ellipse of radii `(1 + 0.8 * AnvilStrength) * R * stretch` by
  `0.9 * (1 + 0.8 * AnvilStrength) * R / stretch`. Its equivalent radius is the geometric mean —
  `(1 + 0.8 * S) * sqrt(0.9)` cluster radii, **1.5938 R** at the shipped 0.85 — and `stretch` cancels out
  of it exactly.
* The tower's footprint is the union of its six lobes in projection. It is a consequence of the layout
  constants alone and is computed by quadrature over them: **0.9594 R** at `TopTaper` 0 and **0.9051 R** at
  1, linear between to 0.2 per cent (0.9363 measured at 0.4 against 0.9377 predicted).
* The two are concentric, so the union is the larger: the canopy's. The gain is their ratio, **1.702** for
  the shipped storm, and the cluster's radius is divided by it.

**Nothing in that is fitted to a sky.** The check that it is the right law is the sabotage and the frames,
not the arithmetic: measured, the canopy multiplies `-ln(1 - cover)` by **2.53** against the **2.90** the
areas predict, and the gap is the placement not being the independent-bodies model that turns an area into
a cover — the same gap §RW measured when it derived a packing law and rejected it.

**IT IS THE CLUSTER THAT SHRINKS AND NOT THE CANOPY**, because `AnvilStrength` is authored as how far the
canopy spreads BEYOND its tower: scaling the canopy alone would redefine the artist's number. The storm's
proportions are exactly what its asset states; what changes is how much sky one storm is worth. Its opaque
core goes **2.8 : 1 to 1.9 : 1** — a narrower tower under the same overhang, which is what a storm is.

### THE LOW END DID NOT MOVE, TO THE LAST DIGIT

`CloudProceduralField.CoverageIsTheFractionOfSkyThatHasCloudInTheColumn`, this worktree, after the change:

    coverage 0.15 -> 0.120  (-0.030)
    coverage 0.24 -> 0.215  (-0.025)
    coverage 0.35 -> 0.318  (-0.032)
    coverage 0.50 -> 0.522  (+0.022)
    coverage 0.75 -> 0.759  (+0.009)

**That is §SIL2's published ladder reproduced digit for digit**, and it is bit-identical rather than merely
close: the fixture authors no anvil, so the gain is exactly 1.0 and not one instruction of the low end's
arithmetic changed.

**RE-DERIVING IT WAS ATTEMPTED AND IS REPORTED AS A FAILURE RATHER THAN OMITTED.** §SIL2 is right that
refitting the slope cannot fix it, so the mapping was derived from scratch instead: with the clusters placed
independently the sky is `1 - exp(-n A)`, and `n A` can be written out in closed form from this file's own
constants **including the term §RW named as the reason its own attempt failed** — that a cluster's radius
rises with how deep inside the threshold its cell fell, so `<(0.6 + 0.4 fill)²>` is carried rather than
assumed constant. Against the measured ladder:

| Coverage | 0.15 | 0.24 | 0.35 | 0.50 | 0.75 |
|---|---|---|---|---|---|
| derived | 0.180 | 0.265 | 0.373 | 0.535 | 0.730 |
| measured | 0.120 | 0.215 | 0.318 | 0.522 | 0.759 |

**It over-predicts the low end by 0.060 and under-predicts the top by 0.029 — worse than the fitted mapping
at every point, and worst exactly where the fitted one is worst.** The independent-bodies model is wrong in
the direction the placement is not independent: at a low alive fraction the clusters that exist are the
several a single cell holds, sitting on one site and overlapping each other, so they cover less sky than
scattered bodies of the same total area would. **The low end is a correlation defect, not a slope defect,
and it is not fixed here.**

### THE SHIPPED SKY DID NOT MOVE — SIX OF SIX, BYTE FOR BYTE

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, both arms. **The repeat floor is zero and was measured
on this worktree**: two runs of the same binary at `mid away` are identical, from the SECOND render onward,
which is §A1's correction reproduced.

| point | md5, before and after |
## PTP — the panel that makes a painting, and the three things it found in the sky it previews, 2026-08-26

§PT shipped the `.dclayout` format, the engine path, the Details slot and `Tools/CloudLayoutBaker`, and
named what it had NOT done in its own closing list: *"the thing that BAKES a painting from an image is
`Tools/CloudLayoutBaker`, not a window"*. The owner had asked about an editor. This phase is the window.

It was started by another developer, whose session ended with the work uncommitted and whose transcript
did not survive; the teamlead committed what was on disk (`5dde690e`) marked **not verified — no frames,
no sweep, no format gate**. What follows is the verification that commit owed, and the three defects that
verification found.

### What was already there, and what it got wrong

The panel existed and worked: a source (a picture, or a `.dclayout` to inspect), a channel mapping, a
top-down MAP of the sky the painting makes rather than a view of the texture, both of §PT's measured
facts reported in words, and a bake that calls `Assets::MakeCloudLayoutFromImage` — the tool's own
function. Those decisions are right and are kept.

**Three things it said were not true of the sky it was previewing**, all found by reading
`VolumetricCloudRenderer` beside it rather than by looking at the panel:

| what the panel did | what the sky does | the symptom |
|---|---|---|
| labelled its four controls "Slot 0..3" | the four **Cloud Type** slots are COMPACTED into species — empty slots skipped, a repeated type dropped — and the painting's channels are indexed by SPECIES | a layer whose only type sits in `Cloud Type 3` is driven by the painting's **RED** channel. Nothing anywhere said so; the symptom is a channel an artist swears they painted that does nothing |
| used the constant **21 km** for the weather patch tile | reads `PatchTileSize` off the component | 21 km is the component's DEFAULT, so the map agreed in every scene that had never touched the field and disagreed silently in every scene that had. The patch is what decides a cell's coverage whenever the painting is not the source |
| gave all four species ONE square cell taken from the layer's **finest** type, anisotropy dropped | each species is placed on its own lattice — the layer's, times that type's Placement Scale, stretched by its Placement Anisotropy | a coarse species' map drawn at a fine species' resolution, and the most permissive legibility bound in the layer quoted for every channel: *"every stroke clears the cell"* about a 1.2 km stroke on a 4 km cell |

The third one is the interesting one, because **`ValidateCloudProceduralLayout` is right to take the
finest and the panel was wrong to copy it.** The validator asks whether one layout TEXEL can tell two
neighbouring cells apart, which fails first for the species with the SMALLEST cells. Legibility asks
whether a STROKE survives the lattice, which fails first for the species with the LARGEST. The two bounds
run in opposite directions, and that is exactly why they must not share a number.
`BuildCloudLayoutPreview` now takes the mapped slot's own cell; the validator is untouched.

`ECS::ResolveCloudSpecies` is where the compaction now lives — one statement, called by both the renderer
and the panel, because a panel that compacted the slots for itself would name a different type the day
the rule moved.

### AND A FOURTH, WHICH IS A CONVENTION NOBODY HAD WRITTEN DOWN

The layout's **v axis runs north** and an image's first row is its **top**, so a picture placed in the
world stands on its head relative to a north-up map — which is why the panel's two panes differ by a
vertical flip and why `PTP_LetterP`'s scene carries a `LayoutOffset` of `(-20.20, -24.00)` km: half a
period, to slide the figure into the middle of the region. None of that was stated anywhere. The panel
says it under the panes now, and
`CloudPlacementSpectrum.ThePaintingsFirstRowSitsAtTheOriginAndItsRowsRunNorth` pins it: the world origin
is texel (0,0) exactly, and a band on rows 1..3 is found NORTH of it.

### THE PICTURES

| file | what it shows |
|---|---|
| `Shots/PTP_letter_topdown.png` | a letter of cloud from 90 km — the loop closed: a picture, a `.dclayout`, a slot, a sky |
| `Shots/PTP_letter_x4_topdown.png` | the same painting at **4 repeats**: the glyph has collapsed into evenly spaced clumps |
| `Shots/PTP_channels_red_topdown.png` | one picture, `--channels 0,1,2,3`: a **north-south** wall of cloud |
| `Shots/PTP_channels_green_topdown.png` | **THE SAME PICTURE**, `--channels 1,0,2,3`: the wall turns 90 degrees |
| `Shots/PTP_panel_letter.png` | the panel on that sky |
| `Shots/PTP_panel_nomask.png` | the same painting with the mask off |
| `Shots/PTP_panel_x4.png` | the same painting at 4 repeats |

**The channel pair is the both-ends proof of the convention this phase exists to make visible.** Two
scenes that differ in nothing but which source channel feeds species 0; the sky turns a quarter turn.
`PTP_Channels_Bands.png` is 64 squared, red painted over half the columns and green over half the rows —
each channel's mean is exactly 0.5, so a pattern at full strength empties everything it does not paint
and the boundary is a wall rather than two shades of overcast. The two layouts are 16 432 B, content
`4daa0a7c` and `e4778ea5`.

### THE PANEL'S OWN VERDICTS, AT BOTH ENDS OF BOTH

Three scenes, one painting, and the two facts §PT could only state in a protocol now stated to the artist
in the tool:

| scene | Layout Mask Strength | Layout Repeats | what the panel says about the PATTERN | what it says about the STROKES |
|---|---|---|---|---|
| `PTP_Layout_LetterP` | 1.0 | 1 | ⚠ *"does NOTHING here: both ends of it give the same sky, cell for cell"* — **100%** of cells pinned by the clamp | ✓ *"every stroke clears the cell"* — thinnest tenth **5.81 km**, median 6.09, cell 3.00 |
| `PTP_Layout_LetterP_nomask` | 0.0 | 1 | ✓ *"moves 98% of the cells across its range (252 of 256)"* — **0%** pinned | ✓ same strokes |
| `PTP_Layout_LetterP_x4` | 1.0 | 4 | ⚠ dead, same cause | ⚠ ***"74% of what you drew on this channel is NARROWER THAN ONE CLOUD CELL"*** — thinnest tenth **1.45 km**, median 1.52, cell 3.00 |

The x4 row is the one to read beside `PTP_letter_x4_topdown.png`: the panel's warning and the sky's
clumps are the same fact, and until now only §PT's protocol held it. One texel is 0.094 km at one repeat
and 0.023 km at four — **every texel resolves the cell in both**, which is precisely why the validator
that checks the texel cannot see this.

### THE BAKE IS THE TOOL'S BAKE — MEASURED, NOT ASSERTED

`Tools/CloudLayoutBaker/Source/main.cpp` promises in its own header that §PTP *"checks by baking one
picture both ways and comparing the files byte for byte"*. Done:

```
CloudLayoutBaker --image PTP_LetterP.png --channels 0,1,2,3 --mask   ->  dcf3fae42956e08a83f38df5693bba9a
Editor/Resources/Assets/Clouds/Layouts/PTP_LetterP.dclayout          ->  dcf3fae42956e08a83f38df5693bba9a
```

Both paths are `MakeCloudLayoutFromImage` followed by `EncodeCloudLayout` and a write — the panel's
through `CloudLayoutAsset::Save`, the tool's through `<fstream>`. 1 310 768 bytes, content `35a66984`,
identical.

> ⚠️ **AND THE HONEST LIMIT OF THAT: THE BUTTON ITSELF WAS NOT PRESSED.** Synthetic input does not work on
> this machine (no assistive access for System Events), so no control in this panel can be exercised
> without a hand on the mouse. Everything here was driven through the SCENE — `--open-panel` puts the tool
> on screen and the open scene decides what it reads — and through the engine functions the controls call.
> What is therefore NOT verified by this task: that pressing **Bake** writes a file, that the channel
> combos rebuild the layout when clicked, and that the Sky span and Pane size sliders redraw. They are
> three lines each and they are read in the diff; they are not measured. Naming it here because the next
> person should not have to rediscover that the wall exists.

### THE SHIPPED SKY DID NOT MOVE — SIX OF SIX, BYTE FOR BYTE

`Clouds_Demo`, camera `0,200,0`, `--shot-frames 90`, 1280x766, three elevations and both azimuths. Every
md5 equals §PT's table, which equals the frames committed before §PT existed:

| point | md5 |
|---|---|
| zenith away `0,0.9,-1` | `73c7806b04c1c71317e4aba52e3f20dc` |
| mid away `0,0.45,-1` | `4819e9c0c6dcdfadbf7477bd90a409d7` |
| horizon away `0,0.12,-1` | `304f4c2b56ea4751f50b6eb7b6351b4e` |
| zenith sunward `0,0.9,1` | `ada3c729466065ad749a473698b2f903` |
| mid sunward `0,0.45,1` | `4a2ddc2a6a5bd7637701fd1a3fe7da8b` |
| horizon sunward `0,0.12,1` | `bfd06fce1094adaa4e538cebba2f66f7` |

**All six are the same bytes in both arms**, so the four things the brief says must not be lost — the sky's
cover of 0.7446, `LATTICE 0.0000` along the wind, the erosion's 1.10x over the march's floor and the
congestus' 1.8 : 1 core — cannot have moved: they are properties of a file that is identical. The genus
sweep says the same from the other side: the congestus reads **1.8 : 1** and **0.5122** in both columns.

**THE HASHES WERE CHECKED FOR DUPLICATES AND SIX PAIRS COLLIDE — exactly those six, and no others.** Of the
28 frames, the only repeated hashes are the six `Clouds_Demo` before/after pairs. The twelve cumulonimbus
frames and the four hero-scene frames are sixteen distinct files.

### ⚠️ THE PRICE, NAMED: THREE SCENES USE THE CUMULONIMBUS AND ALL THREE MOVE PAST A TENTH

This is decision **D-20's condition NOT met**, it is a trade, and it is the teamlead's rather than this
task's. Every scene in the project that names `Cumulonimbus.decloudtype`, measured in both arms at its own
authored `Coverage`, 8 realisations:

| scene | authored `Coverage` | sky before | sky after | the slider was out by | it is out by |
|---|---|---|---|---|---|
| `Clouds_HeroTrio` | 0.075 | 0.2489 | **0.1030** | **+0.174** | **+0.028** |
| `Clouds_HeroMass` | 0.209 | 0.5375 | **0.2441** | **+0.329** | **+0.035** |
| `SIL_Cumulonimbus` | 0.762 | 0.9856 | **0.7644** | **+0.224** | **+0.002** |

**Every one of the three moves by more than a tenth, and that IS the fix rather than a side effect** — the
mapping those three scenes were authorised under is the one that was lying. `Shots/CB_before_cb_mid_away.png`
against `Shots/CB_after_cb_mid_away.png` is what 0.9856 of the sky looks like against 0.7644: a formless
grey murk with the camera inside the overcast, against storms with blue between them.

**The number of storms does not change and only their width does**, which is worth stating because it
decides which repair is the cheaper one. The alive fraction is untouched, so `Clouds_HeroTrio` still has
its trio and `Clouds_HeroMass` still has its mass; each body is 1/1.702 as wide. **Re-authoring the three
scenes to preserve their old sky would therefore change what they are FOR** — `HeroTrio` would need about
0.213 instead of 0.075 and would then be a field rather than three bodies.

The work was stopped here and the decision asked for.

### THE RULING: THE SCENES ARE NOT RE-AUTHORED, AND D-20 IS SATISFIED RATHER THAN WAIVED

**Decided by the teamlead on the two frames, 2026-08-26: ship as measured, re-author nothing.** The three
scene files are unchanged by this task. The argument is recorded here because a later phase will find three
scenes whose sky moved and no diff explaining it:

1. **The old sky was not a differently-scaled sky, it was a WRONG one.** A scene called `HeroTrio`, authored
   at `Coverage 0.075`, rendered as 0.249 of closed overcast. Nobody asked for that; the slider lied.
   Restoring the old picture would be restoring the lie.
2. **Re-authoring would destroy the scene's intent.** `HeroTrio` would need about 0.213, at which point it
   is a field and not a trio. When returning to the old appearance requires contradicting the scene's own
   name, the old appearance was the error.
3. **D-20 was written against SILENT DRIFT, not against repairing a known lie.** Its condition reads "the
   mapping holds inside a tenth, therefore nothing needs re-authoring". Here the mapping did not drift on
   its own — it is being FIXED, and after the fix eight genera of nine do not move at all while the ninth
   stops lying. That is the outcome D-20 exists to protect.
4. **And the composition survives.** The alive fraction is untouched, so WHERE the storms stand does not
   move and HOW MANY there are does not change; only their width does — which is the one quantity that
   was wrong.

### EIGHT SABOTAGES, AND ONE STAYED GREEN — the hole is closed

Every one applied to the generator, the suite's **objects AND binary deleted**, rebuilt, run, reverted.

| break | result |
|---|---|
| the compensation is deleted from the cluster's radius | RED |
| `kAnvilSpreadPerStrength` is changed in the EMISSION only | RED |
| the gain uses the canopy's LONG radius instead of its equivalent one | RED |
| `kAnvilAcrossOverAlong` is changed in the EMISSION only | RED |
| the tower's footprint becomes its outer REACH (1.096) instead of the union's equivalent radius | RED |
| the gain's floor at one is removed | RED |
| the thickness half of the emission's own guard is dropped from the gain | RED |
| **the taper term is dropped and the tower's footprint becomes one constant** | **GREEN — a real hole, closed** |

**THE GREEN, AND WHAT IT SAYS ABOUT THE OTHER THREE TESTS.** Every relation written up to that point holds
`TopTaper` FIXED, and an error common to both arms of a ratio cancels — so a wrong tower footprint was
invisible while it was wrong by the same amount everywhere. That is an untested number in the middle of a
calibration.

What is asserted now is that **the quadrature predicts the real bake**, and it is a strong statement
because the two are completely independent: with no canopy the gain is exactly 1 and neither constant
reaches a single lump, so the bake measures the layout while the constants merely claim to describe it.
Read through `-ln(1 - cover)`, whose constant of proportionality cancels in a ratio of two tapers:

    tower footprint, taper 1.0 over taper 0.4 — the constants say 0.9317, the sky says 0.9340

**A quarter of one per cent**, which is what makes the two numbers a derivation rather than a fit. Re-run,
the sabotage is RED, and it is seven times outside the window.

**AND THE STALE-OBJECT TRAP CAUGHT THIS TASK TOO — THE FIFTH TIME IN THIS PROGRAMME**, arriving from §RW's
own direction. The first run of the
new relation reported "the constants say 1.0000" against a source that plainly said otherwise: the sabotage
script had reverted the SOURCE, and the suite's incremental build reused the sabotaged
`CloudProceduralVolume.o`. Every number above is from a suite whose objects were deleted first.

### THE FRAMES

All at `--shot-frames 90`, camera `0,200,0`, both azimuths at all three elevations. **28 files, and the only
repeated hashes are the six `Clouds_Demo` pairs** — which is the result rather than a duplicate.

| file | what it shows |
|---|---|
| `Shots/CB_before_cb_mid_away.png` / `CB_after_cb_mid_away.png` | **THE SHOW.** Before: a formless grey murk with the camera inside a 0.986 overcast — no body, no edge, no sky. After: storms with blue between them at the 0.764 the slider asked for |
| `Shots/CB_before_cb_zenith_away.png` / `CB_after_cb_zenith_away.png` | the zenith, which a closed sky hides completely |
| `Shots/CB_before_cb_horizon_away.png` / `CB_after_cb_horizon_away.png` | the horizon, where the far field is the test |
| `Shots/CB_before_cb_*_sun.png` / `CB_after_cb_*_sun.png` | the three sunward points — the canopy's silver lining and the sun disc through a hole, which is what the storm had to not lose |
| `Shots/CB_before_{zenith,mid,horizon}_{away,sun}.png` / `CB_after_*` | **the six protocol points on `Clouds_Demo`, which are the SAME BYTES in both arms** |
| `Shots/CB_before_heroTrio_mid_away.png` / `CB_after_heroTrio_mid_away.png` | `Clouds_HeroTrio`, one of the three scenes the trade is about |
| `Shots/CB_before_heroMass_mid_away.png` / `CB_after_heroMass_mid_away.png` | `Clouds_HeroMass`, the other |

### WHAT THIS TASK DID NOT DO

* **IT DID NOT RE-AUTHOR THE THREE STORM SCENES, AND THAT IS A RULING RATHER THAN AN OMISSION** — asked
  for, argued and decided above. Their sky moves; the slider now means it in all three, out by
  0.028 / 0.035 / 0.002 against the 0.174 / 0.329 / 0.224 it was out by.
* **IT DID NOT GIVE `kPackingCompensation` A CELL TERM**, which is what the brief asked for, and the reason
  is the measurement above rather than difficulty: the cell moves the slider by at most 0.032 over an
  eightfold range and not monotonically, so a cell term would be a second constant fitted at a second size,
  and it would move all nine genera to buy 0.03 on one.
* **IT DID NOT FIX THE LOW END.** It is unchanged to the last digit, the re-derivation was attempted and
  the numbers are above: the independent-bodies model is worse than the fitted mapping at every point on
  the ladder. The defect is that clusters sharing a cell are correlated, which is a change to the
  PLACEMENT rather than to the mapping.
* **IT DID NOT RE-SHOOT THE PROTOCOL AGAINST THE MERGED `dev`.** The before/after pair is `7459012a` on
  both arms, which is what makes it an A/B; `dev` has since landed the material-parameter upload path, and
  a frame taken on it is a different measurement rather than the other half of this one.
* **IT DID NOT TOUCH THE MARCH, THE EROSION OR THE LIGHTING.** No shader was read or edited, and no
  component default moved.
* **IT DID TOUCH `Tools/LatticePeak`, WHICH IS NOT THIS TASK'S FILE**, and the change is four lines: the
  layer comes from `Graphic::CloudTypeSetEnvelopeKm` instead of `Top - Base`. It is reported here rather
  than taken quietly, and the argument for making it is that the alternative was to calibrate the sky
  against an instrument that could not see the body being calibrated.

### THE WHOLE SWEEP, WITH THE OBJECTS AND THE BINARIES DELETED FIRST

`CI=true premake5 gmake` on the merged tree, `build/Bin/Tests/<config>` and
`build/Tests/Intermediates/<config>` **removed entirely** before either run, then every generated makefile
built and run.

| | Debug | Release |
|---|---|---|
| makefiles generated | **84** | **84** |
| excluded as tools, libraries and aggregates | **14** | **14** |
| makefiles the loop considered | **70** | **70** |
| suite binaries built | **67** | **67** |
| `not-a-suite` lines | **3** | **3** |
| `BUILD-FAIL` | **none** | **none** |
| suites failed | **NONE** | **NONE** |

**The count balances in both: 70 considered, 3 named as tools, 67 suites, 67 binaries, nothing
unexplained, and the two configurations agree line for line.**

**AND THE THREE `not-a-suite` NAMES, READ RATHER THAN ASSUMED:** `ImageStat`, `LineJump`, `SceneMigrator`.
All three are this programme's own instruments — they land in `build/Bin/<config>/` and link no gtest —
which is what the loop is supposed to report about them. **None of them is a suite that failed to build.**

> The exclusion list this run used is deliberately SHORTER than §RW's: `ImageStat`, `LineJump` and
> `SceneMigrator` were left OUT of it so that the loop would have to name them. It did. That is the
> derived-rather-than-listed behaviour §2.4 item 5a asks for, exercised rather than trusted: a real suite
> that stopped building would have arrived in the same three lines and been impossible to miss.

**THE RELEASE ARM NEEDED ITS ENGINE STACK BUILT FIRST, AND THE FIRST ATTEMPT IS RECORDED BECAUSE IT LOOKED
LIKE A CATASTROPHE.** Run straight after the Debug arm, Release reported `BUILD-FAIL` on all seventy and
built zero binaries. Nothing was wrong with any suite: `libDesert.a` and `libCommon.a` had never been built
in Release on this worktree, and a suite makefile does not build its own dependencies. The Debug arm had
concealed the same gap by having those libraries already on disk from the frames. **A sweep that reports
seventy build failures is reporting one.**
## PR — the protocol scene had two owners and 44 of its 51 cloud parameters were not in it, 2026-08-25

Every phase in this file measures itself at the same six points on the same scene. That makes
`Clouds_Demo.desce` a **measuring instrument**, and this section is about the instrument rather than about
the sky. It was written because the file had already been recorded as having drifted, and a drifting ruler
puts every cross-phase comparison in this document in question.

Two things are established below with git and with the renderer rather than with argument: **what moved and
when**, and **which recorded numbers it invalidates**. Then the scene is frozen — as a SECOND file, because
one name cannot carry two jobs — and the six points are re-measured on the frozen one and published as the
base every later phase is to be read against.

### The scene file moved eight times, and every move is a content move

`git log --follow` on `Editor/Resources/Assets/Scenes/Clouds_Demo.desce`, from the commit that created it
(`622a01a6`, the rebuild) to `dev` at `7459012a`. The file is one line of JSON, so a numstat of `1 1` says
nothing; each row below is the diff of the parsed tree.

| commit | date | what changed IN THE FILE |
|---|---|---|
| `622a01a6` | 08-19 | created, by renaming `Sky_PhysicalShowcase.desce`. `Coverage` 0.24, layer 3.0–8.0 km, `CloudType` 0.55, `CloudTypeVariance` 0.5 |
| `c040080f` | 08-19 | `SceneVersion` 1→2, **`Tonemapper: 0` added** — the ACES change, which moves every pixel of every frame |
| `771088b5` | 08-19 | `SceneVersion` 2→3, no content |
| `68fcc34e` | 08-19 | **`LayerBottomAltitude` 300000, `LayerThickness` 500000, `CloudType` 0.55, `CloudTypeVariance` 0.5 all DELETED**, replaced by `Species: 2` |
| `ae485906` | 08-19 | `Species: 2` → `CloudType: ".../Cumulus_Congestus.decloudtype"` |
| `68facb2c` | 08-19 | `CloudType` → `CloudType1` |
| `f3c8b24a` | 08-20 | **`Exposure` 0.22 → 0.26, `BloomThreshold` 2.5 → 1.0** |
| `c97e43a4` | 08-24 | **`Coverage` 0.24 → 0.762** |

Three of those are not book-keeping. The tonemapper changes the mapping from radiance to pixels; the
exposure changes it again; and the coverage triples. The fourth, `68fcc34e`, is the largest and the
quietest: it did not change a number, it **deleted the two numbers that said where the cloud layer is**.
Before it the scene stated a layer from 3.0 km to 8.0 km. After it the layer comes from the type asset, and
`Cumulus_Congestus.decloudtype` says 2.2 km to 5.8 km. **The deck dropped 800 m and lost 1.4 km of
thickness, and no line of the scene file records that.**

### The larger channel is not the file at all: 44 of the 51 cloud parameters were never in it

`Clouds_Demo.desce` writes **seven** keys into `VolumetricCloud`. The reflected component has **fifty-one**
fields. `DeserializeReflected` is explicit about what happens to the rest —
`// missing key — keep the field's default value` — so forty-four of the layer's parameters are not
properties of the protocol scene at all. They are properties of whatever `VolumetricCloudComponent.hpp`
said on the day somebody pressed render.

It is not only the clouds:

| component | reflected fields | written in the file | taken from the C++ default |
|---|---|---|---|
| `VolumetricCloud` | 51 | 7 | **44** |
| `SkyAtmosphere` | 47 | 15 | **32** |
| `SceneSettings` | 60 | 51 | **9**, and one of them is `CloudQualityTier` |
| `ExponentialHeightFog` | 14 | 14 | 0 |

`CloudQualityTier` deserves its own line: §QT measured three tiers and the protocol scene names none of
them. Every six-point table in this document was shot at **whatever the default tier was**, which today is
`High` and is a C++ literal.

And those defaults were moved, deliberately and repeatedly, by the phases that then measured themselves
through them. Extracted by walking `VolumetricCloudComponent.hpp` across every commit that touched it:

| commit | phase | default that moved, in a field the scene does NOT pin |
|---|---|---|
| `771088b5` | T2 | `WeatherSeed`, `WeatherOctaves`, `DetailSeed`, `DetailOctaves` **deleted** |
| `f3c8b24a` | OE-FIX | `LightMarchSamples` 6 → **32** |
| `17d4a4db` | CS | `CastShadows` **added, default true** |
| `0589eb52` | Э5 | `RegionSize` and `Seed` **added** |
| `4ba9992c` | DS | `DetailTileSize` 4 km → **1 km**; `DetailStrength` 0.10 → **0.40** |
| `efc76135` | RW | `PlacementDensity` **2.5**, `PlacementScatter`, `PlacementSizeVariety`, `PatchTileSize`, `PatchStrength` **added** |
| `5ea347cb` | RW2 | `PlacementDensity` 2.5 → **1.75** |
| `a21848c6` | SIL2 | `DetailStrength` 0.40 → **0.65** |
| `155fcc38` | PT | five `Layout*` fields **added** (inert with no painting bound — §PT proved it on the frame) |

**So the scene drifted on twelve commits and its file changed on eight of them, and the two sets barely
overlap.** An engineer who diffed `Clouds_Demo.desce` between two phases and found nothing would have
concluded, correctly and uselessly, that the scene had not changed.

### What is invalidated, by name

Not "possibly affected". These are the recorded numbers whose subject is provably not today's subject:

| where | the number | what it was measured on |
|---|---|---|
| §T-ACES, "the frame" table | mean/p05/p50/p95/contrast/sat, 6 rows | scene at `622a01a6`→`c040080f`: layer 3.0–8.0 km, analytic cloud-type scalar, Reinhard→ACES |
| §T-ACES, the eleven-scene table | `Clouds_Demo` 0.369 / 0.319→0.386 / 0.300→0.442 | same |
| §OE, the knock-out table | linear zenith radiances, 16.81 etc. | scene at `68facb2c`: `LightMarchSamples` 6, `Exposure` 0.22, pre-Э5 producer |
| §OE-FIX, six points, **before** column | 6 rows | same |
| §OE-FIX, "the exposure" | 0.2567 / 0.2632 → 0.26 | same |
| §CS, the sky-band figures | `mean 0.635 / p05 0.503 / p95 0.780 / contrast 0.276` | scene at `f3c8b24a`, pre-Э5 producer |
| §QT, the tier table | 17.99 / 14.24 / 8.61 ms | scene at `17d4a4db`, pre-Э5 producer; already corrected in method by §GT-3 |
| §QT, "the sky does not change" | 0 of 980 480 pixels, 553/547 px at horizon | same |
| the LineJump norm table | rows max 0.006 norm / 0.010 threshold / 0.022 defect | **frames shot 08-20**, rectangles `2 2 1101 480` and `2 2 1278 552`, pre-Э5 sky |
| the teamlead's baseline at `2804b096` | 6 rows | different resolution (1103x668) AND different rectangle (`0 0 1103 480`) — self-flagged in this file |
| §A0 | `+3.7 to +5.1 ms` for one hero cloud | pre-Э5 producer |
| §A2+A3, the price table | **1.39x, +7.33 ms, +0.92 ms/instance** | scene `ZZ_Perf<n>`, **which was never committed**; pre-Э5 producer. Part 2 below |
| §A2+A3, the catalogue | ten genera, aspect/comps/detail/pocket | pre-DS erosion, pre-SIL lump — the voxels are not these voxels |

**The rectangle drifted too, and by one pixel of height.** §OE-FIX and the LineJump norm are on
`0 0 1280 552` / `2 2 1278 552`; everything from §DS onward is on `0 0 1280 551` / `2 2 1278 551`. §Э5's
table quotes §OE-FIX's numbers as its own "before" column while declaring a 551-tall band. One pixel row of
1280 is 0.23 % of the rectangle and nothing in those tables turns on it, but it is the same defect class as
everything else here — two numbers obliged to agree, and nothing checking.

### The chain from OE-FIX onward is INTACT, and that is measured rather than assumed

The corollary matters as much as the damage. Every phase since §OE-FIX opened its six-point table by
re-shooting the previous phase's "after" as its own "before". If the scene had drifted **between** phases,
those columns would disagree. They do not:

| the "after" of | its zenith-away mean / contrast / sat | the "before" of | agrees |
|---|---|---|---|
| §OE-FIX | 0.528 / 0.400 / 0.128 | §Э5 | ✔ |
| §Э5 | 0.570 / 0.449 / 0.135 | §DS | ✔ |
| §DS | 0.549 / 0.450 / 0.164 | §RW | ✔ |
| §RW | 0.600 / 0.251 / 0.068 | §RW2 (the "2.5" column) | ✔ |
| §RW2 | 0.616 / 0.354 / 0.067 | §SIL | ✔ |
| §SIL | 0.598 / 0.386 / 0.079 | §SIL2 | ✔ |
| §SIL2 | 0.570 / 0.415 / 0.098 | §PT (byte for byte, six md5s) | ✔ |

**So the drift is real, it is large, and it is entirely accounted for by the changes each phase declared.**
What the chain does NOT survive is a comparison that jumps over it — quoting §OE-FIX's contrast beside
§SIL2's, or reading the LineJump norm of 0.006 (taken on the 08-20 sky, on a 552-tall rectangle) as the
norm for a §SIL2 frame. §DS and §SIL2 both do exactly that: each quotes "the 0.010 that means something to
look at" against a sky that threshold was never measured on.

This tree reproduces the end of that chain **exactly**: six renders of `Clouds_Demo` at the six points give
the six md5s §PT published, digit for digit. The noise floor is zero, and the first render in this fresh
worktree was discarded per §A1's correction.

### The freeze: `Clouds_Protocol.desce`, and why it is a second file

⚠️ **The demo scene has an owner who is allowed to change it. The protocol scene must not change at all.**
Today those are one file, which is two meanings on one name — what §4 of the contract forbids for data
formats, applied to a measuring instrument. `Clouds_Demo.desce` is the artist's scene and stays the
artist's scene, `c97e43a4` and all.

**`Editor/Resources/Assets/Scenes/Clouds_Protocol.desce` is the ruler.** It is `Clouds_Demo` as `dev` ships
it at `7459012a`, with one difference that is the whole point:

**Every reflected field of every reflected component is written out explicitly** — 44 added to
`VolumetricCloud`, 32 to `SkyAtmosphere`, 9 to `SceneSettings` (including `CloudQualityTier: 2`, `High`,
which the protocol has been implicitly assuming since §QT). A copy of the file would have drifted exactly
as the original did, because the drift was never in the file. **A scene that states all fifty-one of its
cloud parameters cannot be moved by a change to a C++ default**, which closes the channel that carried
eight of the twelve moves above.

It does not close the other channel, and saying so is the point: a change to the SHADER, to the producer in
`CloudProceduralVolume.cpp`, or to a constant that is not a component field —
`kCloudLumpVerticalOverHorizontal`, which §SIL moved from 0.45 to 0.75 — still moves this scene, exactly as
it moves every scene. **That is what phases are FOR, and it is what the after→before chain above already
handles.** The freeze removes the SILENT channel, not the deliberate one.

**The freeze is verified, not asserted.** `Clouds_Protocol.desce` renders the six protocol points
**byte for byte identical** to `Clouds_Demo.desce` on the same binary — which simultaneously proves that
the 85 values written into it are the values the defaults were producing, and that nothing was mistyped:

| point | md5 of `Clouds_Protocol` | equals |
|---|---|---|
| zenith away `0,0.9,-1` | `73c7806b04c1c71317e4aba52e3f20dc` | `Clouds_Demo` and `SIL2_after_zenith_away.png` |
| mid away `0,0.45,-1` | `4819e9c0c6dcdfadbf7477bd90a409d7` | `Clouds_Demo` and `SIL2_after_mid_away.png` |
| horizon away `0,0.12,-1` | `304f4c2b56ea4751f50b6eb7b6351b4e` | `Clouds_Demo` and `SIL2_after_horizon_away.png` |
| zenith sunward `0,0.9,1` | `ada3c729466065ad749a473698b2f903` | `Clouds_Demo` and `SIL2_after_zenith_sun.png` |
| mid sunward `0,0.45,1` | `4a2ddc2a6a5bd7637701fd1a3fe7da8b` | `Clouds_Demo` and `SIL2_after_mid_sun.png` |
| horizon sunward `0,0.12,1` | `bfd06fce1094adaa4e538cebba2f66f7` | `Clouds_Demo` and `SIL2_after_horizon_sun.png` |

**No `PR_` frame is committed for these six.** They are byte copies of six pictures the repository already
holds, and a duplicate would be the thing §SIL warned about. The md5s above are the evidence.

### THE NEW BASE — six points on the frozen scene

`Clouds_Protocol.desce`, camera `0,200,0`, `--look` as named, `--shot-frames 90`, 1280x766, Debug,
MoltenVK. `ImageStat` over **`0 0 1280 551`**, `LineJump` over **`2 2 1278 551`**. Binary built from
`7459012a`.

**This table replaces `Clouds_Demo` as the thing a later phase re-shoots as its "before". The numbers in it
are identical to §SIL2's "after" column — that is the acceptance criterion, not a coincidence.**

> ⚠️ **SUPERSEDED AS THE "BEFORE" BY §Р12 (2026-08-31).** The freeze did what it promised — no C++
> default has moved this scene since — but Р12 moved the scene ITSELF, deliberately and through the
> channel this section names as the legitimate one: `SkyOcclusionVolume` `false` → `true` and
> `AmbientOcclusionStrength` `0.5` → `1.0`, written into the file. The six rows below are still the
> correct base for anything measured before 2026-08-31 and are what §Р12 re-shoots as its own "before";
> **a phase starting now re-shoots §Р12's "after" table instead.** The chain is not broken, it has one
> more link.

| point | mean | p05 | p50 | p95 | contrast | sat |
|---|---|---|---|---|---|---|
| zenith away `0,0.9,-1` | 0.570 | 0.319 | 0.558 | 0.734 | 0.415 | 0.098 |
| mid away `0,0.45,-1` | 0.534 | 0.324 | 0.546 | 0.718 | 0.395 | 0.174 |
| horizon away `0,0.12,-1` | 0.606 | 0.518 | 0.598 | 0.724 | 0.205 | 0.085 |
| zenith sunward `0,0.9,1` | 0.571 | 0.498 | 0.542 | 0.801 | 0.303 | 0.084 |
| mid sunward `0,0.45,1` | 0.560 | 0.464 | 0.545 | 0.719 | 0.254 | 0.117 |
| horizon sunward `0,0.12,1` | 0.590 | 0.494 | 0.573 | 0.743 | 0.248 | 0.113 |

And `LineJump` on the same six, so that the norm this document quotes has a version taken on THIS sky and
THIS rectangle rather than on the 08-20 sky and a 552-tall one:

| point | rows max | @y | rows mean | cols max | @x |
|---|---|---|---|---|---|
| zenith away | 0.00205 | 41 | 0.00049 | 0.00361 | 1151 |
| mid away | 0.00357 | 363 | 0.00073 | 0.00496 | 425 |
| horizon away | **0.09684** | **540** | 0.00169 | 0.00547 | 499 |
| zenith sunward | 0.00138 | 117 | 0.00042 | 0.00141 | 1110 |
| mid sunward | 0.00288 | 403 | 0.00050 | 0.00346 | 815 |
| horizon sunward | **0.09482** | **540** | 0.00169 | 0.00354 | 609 |

**The norm on the sky that ships today is a row maximum of 0.0014–0.0036, not 0.006.** The old figure was
taken on a sky with a different producer, a different erosion and a different rectangle, and it is a
factor of two loose against this one. The two horizon rows at `y 540` are the checker floor's own edge —
§DS records the same 0.098 at the same row — and are geometry, not a band in the sky.

### Correction 4 — the hero clouds' 1.39x, which §GT filed and did not re-measure

This is the fourth of the four decisions `Docs/GPU_TIMESTAMPS.md` opens with, and the one §GT left alone.
It follows §GT's form exactly: what the old number was measuring is named, and the digit is corrected
rather than quietly replaced.

**What the old number was, and the thing about it that is worse than being wrong.** §A2+A3 records
**1.39x, +7.33 ms, +0.92 ms per instance** for eight hero clouds against none, from a frame-count slope
`(t900 - t300) / 600`. The scene is named in that section as **`ZZ_Perf<n>`** — and `ZZ_Perf<n>` is not in
the repository and never was (`git log --all --diff-filter=A -- '*ZZ_Perf*'` returns nothing). **The number
could not be reproduced by anyone, ever, because its subject was deleted.** §GT reattributed it to
`Clouds_HeroMass`, which is a fair reading — that scene was added by the same commit and carries exactly
eight hero entities of three distinct bodies, three of them enabled — but a reattribution is not a
rectangle.

So the legs are committed this time. **`PR_Hero0.desce`, `PR_Hero3.desce`, `PR_Hero8.desce`** are
`Clouds_HeroMass` with every reflected field written out (§PR above) and with `HeroCloud.Enabled` set on
the first 0, 3 and 8 of the eight entities. They differ from each other **in nothing else**, and
`Desert/Tests/Engine/CloudProtocolScene` asserts that by normalising the flag away and comparing the rest
whole. `PR_Hero3` renders **byte-identical** to the shipped `Clouds_HeroMass.desce` at the framing below.

**THE RECTANGLE**, stated with the number rather than beside it:

> `PR_Hero{0,3,8}.desce`, camera `0,200,0`, `--look 0,0.18,-1` (§A2's own framing, so the two are
> comparable), 1280x766, `--shot-frames 400`, **Debug**, MoltenVK, `CloudQualityTier: High`, binary from
> `7459012a`. Coverage 0.209, one cumulonimbus type, `SuppressProceduralField` true on every hero entity.
> Ten interleaved passes; inside each pass all three instance counts are run once, in both instrument
> modes. **The machine was shared and the load average went from 24 to 101 during the session.**

#### The march's own line, which is what §GT asked for

| pass | `Clouds: March` n=0 | n=3 | n=8 | n8 − n0 |
|---|---|---|---|---|
| 1 | 6.116 | 11.983 † | 12.013 | **+5.897** |
| 3 | 5.997 | 8.881 | 11.668 | **+5.671** |
| 4 | 6.092 | 8.481 | 11.717 | **+5.625** |
| 6 | 4.788 | 6.499 | 10.734 | **+5.946** |

† that leg's whole GPU frame read 48.6 ms against 18–26 for its neighbours; it is left in the table and out
of the arithmetic, and the reason is the machine.

**Eight hero clouds cost the march +5.78 ms — 5.63 to 5.95 across four interleaved passes, a spread of
2.8 %.** The cloud shadow map costs a further **+0.88 ms** (0.31 / 0.47 / 1.30 / 1.56 — noisy, but positive
in all four), and the temporal resolve costs **nothing measurable** (−0.035 / −0.020 / +0.014 / +0.168,
which is zero inside its own spread). §A2's account of where the money goes was never itemised at all; this
says **the march is where it is, the shadow map takes a fifth of what the march takes, and the resolve is
free.**

#### And the whole frame, on the denominator §GT says a budget is set against

The per-pass marks cost ~1.24 ms (§GT), so a whole-frame figure taken from a fully-marked run is not the
frame a player gets. `--gpu-profile --gpu-profile-frame-only` is two timestamps and §GT measured it as
free, so it is the honest denominator — and it was run as its own leg inside every pass:

| pass | GPU frame n=0 | n=3 | n=8 | n8 − n0 | ratio |
|---|---|---|---|---|---|
| 1 | 16.551 | 20.083 | 20.613 | +4.062 | 1.245 |
| 2 | 18.057 | 18.448 | 22.194 | +4.137 | 1.229 |
| 3 | 17.288 | 19.584 | 22.160 | +4.872 | 1.282 |
| 4 | 16.711 | 18.605 | 21.625 | +4.914 | 1.294 |
| 5 | 16.011 | 18.415 | 20.671 | +4.660 | 1.291 |
| 6 | 13.966 | 17.401 | 19.536 | +5.570 | 1.399 |

**+4.77 ms and 1.286x, against the recorded +7.33 ms and 1.39x.** The absolute figure is **35 % lower** than
the record. The ratio is not: **1.39 is the top of the six-pass range** — pass 6 reproduces it to three
digits — and that is the whole lesson rather than a vindication.

**What the old number measured.** A ratio of two whole frames. Its numerator and its denominator are both
dominated by everything that is *not* a hero cloud, so its value moves with the baseline and not with the
change. The table above proves that on one machine in one hour: the *same* eight clouds, measured six
times, give ratios from 1.229 to 1.399 — a 14 % swing — while the **absolute** cost they add moves by 4.06
to 5.57 ms and the **march's own line** moves by 5.63 to 5.95 ms. The tighter the instrument, the tighter
the number. §GT said only the absolute cost transfers between trees; here it does not even transfer between
*passes* in ratio form.

#### The demonstration this correction is actually worth, and it cost nothing to take

Four more passes (7–10) ran while another agent's editor was on the same GPU and the load average was
above 100. They are reported rather than dropped, because of what they show:

| quantity | over the six quiet passes | over all ten |
|---|---|---|
| `Clouds: March`, n8 − n0 | +5.63 to +5.95 ms | **+4.47 to +6.47 ms** — a factor of 1.45, always positive |
| whole GPU frame (frame-only), n8 − n0 | +4.06 to +5.57 ms | **−5.11 to +8.70 ms** — a factor of ∞, and the SIGN CHANGES TWICE |

**On a loaded machine the whole-frame delta says that adding eight clouds made the frame five milliseconds
faster.** The march's own line never once does. That is the argument for §GT's instrument stated as a
measurement instead of as a principle, and it is why the three earlier corrections were needed: every one
of the four decisions in `Docs/GPU_TIMESTAMPS.md` was taken with the top instrument in that table.

#### The conclusion §A2 drew SURVIVES, and now it is itemised

§A2's structural claim was that eight instances of three bodies cost about what three instances of three
bodies do, per instance, because the atlas is one descriptor and one fetch site either way. On the march's
own line, per instance:

| step | pass 3 | pass 4 | pass 6 |
|---|---|---|---|
| 0 → 3 (atlas appears, 3 bodies) | +0.96 ms | +0.80 ms | +0.57 ms |
| 3 → 8 (five more instances, SAME 3 bodies) | +0.56 ms | +0.65 ms | +0.85 ms |

**0.56 to 0.96 ms per instance on either side of the step where the atlas is built** — the two ranges
overlap, so the instance count and not the body count is what is being paid for, exactly as §A2 argued. The
recorded +0.92 ms/instance was a whole-frame figure and lands at the top of that band; **+0.72 ms/instance
on the march** is the figure to carry forward.

**What is NOT claimed.** These are Debug numbers on a shared machine and they are not comparable with
§GT's `Clouds_Demo` itemisation — different scene, different camera, different session. §PT made the same
statement for the same reason. What transfers is the absolute delta and the per-instance figure; the
ratio does not, and that is the finding.

#### Nothing this correction touches reverses a decision

Checked rather than assumed, because a wrong digit under an accepted decision is a reason to re-open it:

* **The demand-built atlas over a fixed eight-slab one** rests on memory arithmetic (12.00 MiB against
  32.00 MiB, recomputed in §A0) and not on this figure. Untouched.
* **"Eight instances cost 1.39x and not 8x"** — the claim the number was quoted for is *sub-linearity*,
  and sub-linearity is confirmed: 0.72 ms per instance against a march that a single near-field body cost
  3.7–5.1 ms in §A0. The digit changes; the decision does not.
* **§QT's tier ladder** was already re-checked by §GT-3 and its conclusion confirmed by the itemisation.

**Two things ARE weaker than they read, and both are recorded here rather than left to be discovered:**

1. **The `LineJump` threshold of 0.010 is about three times the noise floor of the sky it now guards.** It
   was measured on the 08-20 sky over a 552-tall rectangle; on today's sky over `2 2 1278 551` the six
   protocol points read **0.0014–0.0036** (§PR's new base above, and §SIL2's own table agrees). §DS and
   §SIL2 both pass "no banding" against 0.010. The guard is loose, not wrong — a band is one row twenty
   times the noise — but "under 0.010" on a frame whose rows sit at 0.002 is a much weaker statement than
   it sounds, and the number to compare against from now on is the one in §PR.
2. **§A2+A3's catalogue of ten genera** — aspect, components, detail, pocket, all read off the baked
   voxels — was measured before §DS moved the erosion tile from 4 km to 1 km and its strength from 0.10 to
   0.40, and before §SIL raised the lump from 0.45 to 0.75. Those are the numbers that decide what a baked
   body looks like. The catalogue is not wrong about the genera it describes; it is a description of
   voxels that no longer exist, and re-running `Desert/Tests/Engine/CloudCatalogue` (which prints the table
   itself) is a cheap task for whoever needs it next.

### The frames

Two, and they are the two ends of the cost A/B rather than decoration — a reader can see what +5.78 ms of
march buys. Checked against every PNG in `Shots/` by md5 before being committed: neither is a duplicate.

| file | what it shows |
|---|---|
| `Shots/PR_hero0_none.png` | `PR_Hero0`, all eight hero entities disabled — the procedural cumulonimbus field alone |
| `Shots/PR_hero8_eight.png` | `PR_Hero8`, all eight enabled — the same sky with the sculpted anvil, its stem, the arch and the congestus standing in it |

The six protocol points are **not** committed as `PR_` frames: they are byte-identical to
`SIL2_after_*.png`, which the repository already holds, and the md5 table in §PR is the evidence. *(While
checking, 37 of the 344 PNGs in `Shots/` were found to share a hash with another file in the same
directory. That predates this task and none of them is mine; it is recorded here because the next person to
run a duplicate check will otherwise think they caused it.)*

### The six points held across the `dev` merge

`dev` moved to `14ce7610` (MAT stage 1) while this task was in flight. That merge adds five new graph
shaders and touches no cloud file — but §PT's own finding is that a shader file arriving can reschedule
SPIR-V and move pixels, so it was checked rather than reasoned about. After the merge and a full rebuild,
`Clouds_Protocol.desce` renders **all six protocol points byte for byte identical** to the table above. The
new base survives the merge and is stated against `14ce7610`.

### The whole sweep, in both configurations

Object files and binaries deleted first (`rm -rf build/Tests build/Bin/Tests`), then the contract's own
loop, in a resumable form that records a verdict per project rather than only the failures.

| | projects walked | suites | passed | failed | build failures |
|---|---|---|---|---|---|
| Debug | 73 | 68 | **68** | 0 | 0 |
| Release | 73 | 68 | **68** | 0 | 0 |

**The five `not-a-suite` names, read rather than counted** — identical in both configurations, and every one
of them is a directory under `Tools/`: `CloudLayoutBaker`, `ImageStat`, `LatticePeak`, `LineJump`,
`SceneMigrator`. The first is §PT's baker, the other four are this document's own instruments. None is a
suite that failed to build.

Release needed `make Desert config=release` first: the test projects link `build/Bin/Release/libReflectCpp.a`
and the loop's first eight projects reported `BUILD-FAIL` for no other reason. That is a property of the
loop rather than of the tree, and it is written down because the next person will hit it.

### The relations added, and the breaks that verified them

`Desert/Tests/Engine/CloudProtocolScene`, four assertions, each sabotaged and each RED:

| assertion | sabotage | result |
|---|---|---|
| every reflected field of every reflected component is written explicitly | delete `VolumetricCloud.DetailStrength` from `Clouds_Protocol.desce` | **RED** |
| the settings block is written in full, including `CloudQualityTier` | delete `Settings.CloudQualityTier` | **RED** |
| nothing migrates on load | stamp the scene at `SceneVersion 5` | **RED** |
| the three cost legs differ only in `HeroCloud.Enabled` | move `PR_Hero8`'s `Coverage` to 0.5 | **RED** |

All four scenes were restored and the suite is green again; `git status` is clean of them.

**The sabotage that matters most is the first, and it is worth saying what it stands for.** The failure this
suite exists to catch is somebody adding a *fifty-second* field to `VolumetricCloudData` — at which point
the protocol scene silently has a default in it again. Adding a field and deleting a key put the file and
the reflection table into the *same* state, and the assertion walks `type->Fields` against the file's keys,
so it is one code path reached from two directions. The cheap direction was sabotaged; the expensive one
(edit the component, rebuild the engine) is the same test.

### What this task did NOT do, and why it is here rather than in a commit message

* **`Clouds_Demo.desce` was not touched, and that is the decision rather than an omission.** It stays the
  artist's scene, drift and all. Freezing it would have taken a file its owner is entitled to change and
  quietly made it read-only; copying it into a second file with the same seven keys would have frozen
  nothing, because the drift was never in the keys the file had.
* **The eleven older sections were not rewritten.** Every correction above is a note in §PR naming what the
  old number measured, as §GT does, and §GT's own paragraph on the hero clouds was updated in place only
  because it said "not re-measured" and now it has been.
* **The ten-genus catalogue was not re-measured.** It predates §DS's erosion and §SIL's lump, so it
  describes voxels that no longer exist. `Desert/Tests/Engine/CloudCatalogue` prints the table itself, so
  regenerating it is a short task — but it is a *task*, with frames of its own, and doing it here would
  have put two subjects in one section.
* **The `LineJump` threshold was not moved.** §PR publishes the norm measured on today's sky
  (0.0014–0.0036) beside the old 0.006/0.010; changing the threshold the other sections are written
  against is a decision for the owner of those sections, not a side effect of a protocol freeze.
* **`Desert/Tests/Engine/SceneTonemapMigration`'s `kRepositoryScenes` list was not extended.** It names
  eleven scenes and the repository ships forty; it is another file's list and another task's call
  (§1.6). It is named here because "listed by name rather than discovered, so adding one without
  converting it is a red test" is no longer true of it.
That matters more here than it looks: the species resolution MOVED, out of the renderer and into
`Engine/ECS`. Six identical frames say the compaction that came back out is the compaction that went in.
Not committed — byte copies of pictures the repository already holds.

**The noise floor was measured rather than quoted.** Three renders of `PTP_letter_topdown`, including the
first ever run of the editor in this worktree, are one md5 (`d28673ec793789c60b66c1217747cb58`) — and the
same file is byte-identical to the one the previous developer rendered with a different binary, which git
noticed by recording the move as a rename.

### THE RELATIONS, AND THE EIGHT SABOTAGES

Seven in `CloudPlacementSpectrum`, one in `SceneCloudLayoutDefault`.

| relation | broken by | result |
|---|---|---|
| the painting's first row is at the world origin and its rows run north | negating v in `CloudLayoutUv` | RED |
| the map is the bake's own coverage, cell for cell | displacing the map's u by 0.4 of a cell | **GREEN — see below**, then RED |
| the map is sampled on the MAPPED species' cell | taking `Species[0]`'s cell instead | RED |
| the pattern slider is dead exactly when the mask has pinned every cell | measuring the two "ends" at the same strength | RED |
| a bar N texels wide measures N | stroke width = max of the two axes instead of min | RED |
| a bar straddling the edge is ONE stroke | starting the walk at texel 0 | **GREEN — a finding, see below** |
| " | dropping the modulo so the run stops at the line's end | RED |
| the type slots compact into species | keeping the duplicate | RED |

> ⚠️ **THE MAP TEST COULD NOT FAIL, AND THE FIXTURE WAS WHY.** Displacing the map's u by four tenths of a
> cell left all 256 cells identical. The stripe fixture varies along ONE axis, so at a quarter turn it does
> not depend on u at all — and even at zero turns, with cell centres 3 km apart and one hard edge in the
> whole period, no cell centre ever crossed it. A fixture with flat places has hiding places. Replaced by a
> RIPPLE — one sine along u plus one along v, exactly periodic — and run at both rotations; the same
> sabotage is red at once.

> ⚠️ **AND A COMMENT IN THE STROKE WALK WAS FALSE.** It claimed that starting at texel 0 rather than at an
> unpainted one would cut a straddling run in two and halve its measured width. It does not: the wrapping
> run is walked LAST and its write overwrites the half-run left at index 0. Measured, corrected in place,
> and the search kept — for the single-write invariant it buys, not for a correctness it does not.

> ⚠️ **THE SABOTAGE HARNESS ITSELF WAS THE BIGGEST FINDING.** `make` compares mtimes at ONE-SECOND
> granularity, so a patch → build → run → restore cycle that fits inside one second leaves the SABOTAGED
> object linked into the "restored" tree. One sabotage read green because of it, and the poison then stayed
> in the binary for the next run — the object file and the source carried the identical timestamp, and
> `make` said "up to date". This is §2.4's "a suite reported PASSED with an old binary" in a new costume,
> and it is the second time in this programme that a green result was an artefact of the build rather than
> of the code. **Every sabotage above was re-run with the object deleted before and after the patch.**

### The whole sweep, in both configurations

`build/Bin`, `build/Tests` and `build/Intermediates` deleted first, then `CI=true premake5 gmake`, then
every `*.make` that is not a third-party library or an aggregate — built, and run if it produced a test
binary.

| | Debug | Release |
|---|---|---|
| BUILD-FAIL | none | none |
| FAIL | **`SettingConsumers`**, once — see below | none |
| not-a-suite | `CloudLayoutBaker`, `LatticePeak` | `CloudLayoutBaker`, `LatticePeak` |
| suites built and run | 66 | 66 |

Release never existed in this worktree before this task, so its whole tree is fresh by construction;
Debug's `build/Bin`, `build/Tests` and `build/Intermediates` were deleted first.

**THE ONE FAILURE IS THE SWEEP EARNING ITS COST, and it was not this task's code.** `SettingConsumers`
went red on `WeatherTileSize`: commit `4f63559c` — the previous developer's, and the one the teamlead
committed unswept — moved "four cells to a tile" into `ECS::CloudLayerLatticeKm` so that the panel and the
renderer could not spell the ratio differently, and with it went the last textual mention of the field in
`VolumetricCloudRenderer.cpp`, which the consumer table still named as its reader. `git show` on the three
commits says exactly when it happened: one mention at `7459012a`, none at `4f63559c`. The row now names
`Engine/ECS/VolumetricCloudComponent.hpp`, which is where the field is read. Fixed, re-run 10/10 in Debug,
green in Release whose sweep ran after the fix, and then **all sixty-six Debug binaries were run again from
the same tree** — no failures, so the green is the whole set's and not one suite's.

The two `not-a-suite` names are both tools — `CloudLayoutBaker` builds `build/Bin/<cfg>/CloudLayoutBaker`
and no test binary, `LatticePeak` likewise — and this is the same answer §PT derived by hand for
`CloudLayoutBaker` rather than extending the skip list.

The §2.4 item 5a check — is anything in `build/Bin/Tests` a binary the skip list would have skipped —
returns nothing in both configurations, so the list has not gone stale again.

### What this task did NOT do

* **The bake button was not pressed** — see the box above. It is the one claim in this document that rests
  on reading a diff rather than on a measurement, and it is named rather than dressed up.
* **The v-axis convention was NOT changed.** The map being the picture upside down is a real cost, and
  flipping the sampler would have been a one-line change — and would have moved every sky in the
  repository that binds a painting, including §PT's own frames, for a matter of taste. It is DOCUMENTED and
  pinned by a test instead.
* **The panel still does not paint.** It points at a picture; the drawing happens in a paint program. A
  brush in this window was never in scope and there is no evidence anybody wants one.
* **The "Open" picker lists REGISTERED paintings, not the folder.** With a scene that binds one it lists
  it; on a scene with no cloud layer at all it says "(no paintings on disk)" while five `.dclayout` files
  sit in `Resources/Assets/Clouds/Layouts`. That is not this panel's invention — `CloudNoiseVolumePanel`
  says "(no volumes on disk)" from the identical `FindAllByType` call, and fixing one of the two alone
  would fork a convention the whole folder shares. It belongs to whoever owns asset discovery, and it is
  written down here rather than left for the next person to find in a screenshot.
## HV — four checks of Unreal's built, measured and refused, and the one we already have turns out to be inert over open sky, 2026-08-26

Задача: у UE во временной реконструкции облаков четыре проверки истории, которых нет у нас
(`REVIEW_622a01a6.md` Ц1). Исследование — `Docs/Clouds/RESEARCH_HISTORY_VALIDATION.md`, по срезу
`EpicGames/UnrealEngine` `release` `71fe36aa`, сверенному побайтово с зафиксированной выгрузкой.

**Результат: ни одна из четырёх не принята, и логика прохода не изменена ни на строку.** Изменён только
комментарий шапки `CloudTemporalResolve.shader`, который две фазы подряд описывал долг неверно. Шесть
точек протокола после правки **побайтово равны** отгруженным (ниже).

### Первое, что меняет постановку: четыре проверки никогда не работают вместе

`VolumetricRenderTarget.usf:398` и `:515` делят тело реконструкции по
`PERMUTATION_CLOUD_MIN_AND_MAX_DEPTH`. Семейство min/max — в `#if`; дилатация и цветовой box — в `#else`.
Ветвь выбирает C++: `VolumetricCloudRendering.cpp:348-356` требует режим 0 **и** compute, что ровно и есть
наш проход. **Дилатация и box у эталона в нашей конфигурации не компилируются вообще.**

И вторая поправка к нашей же записи: `MinMaxViewDepthKm` — глубина **СЦЕНЫ**, не облака. Её пишет марш из
`SceneDepthMinAndMaxTexture` (`VolumetricCloud.usf:594-606`), это диапазон Z-буфера непрозрачной геометрии
по блоку под одним текселем трассировки. Заголовок нашего шейдера и Ц1 называли её «глубиной облака».

### Приборы

`Tools/ImageDiff` — построен этой задачей, потому что сравнить два кадра было нечем: `ImageStat` меряет
распределение ОДНОГО кадра, `LineJump` — межстрочные скачки ОДНОГО. Кроме счётчиков он даёт **`coherence`**
= средняя |ошибка яркости| / средняя |первая разность того же поля|. Независимые отсчёты дают меньше
единицы, гладкое поле — сколько угодно больше. Это единственная ось, отделяющая ПРИЗРАК (историю оставили,
когда надо было выбросить) от КРУПЫ (историю выбросили, когда надо было оставить), а перенос любой из
четырёх проверок — это ровно обмен одного на другое.

Прибор проверен не только синтетикой: контрольный прогон E1b, где история выбрасывается принудительно,
дал `coherence` 0.81–1.39 (крупа), а ошибка движения против неподвижного кадра — 5.6–41.0 (гладкая).
Двенадцать тестов, `Desert/Tests/Engine/ImageDiffMath`; **десять диверсий, две остались зелёными**, обе
дыры закрыты (см. коммит инструмента).

### Съёмка: чего headless-кадр не может, сказано прямо

⚠️ **Облака в headless-кадре НЕ ДВИЖУТСЯ, и это не настройка сцены.** `Scene::OnUpdate`
(`Engine/Core/Scene.cpp:322-323`) отдаёт системам нулевой шаг, пока сцена не в Play; `AdvanceWind`
(`VolumetricCloudECSSystem.hpp:203-213`) поэтому не накапливает ничего, хотя `WindSpeed` по умолчанию
3000 ед/с. Измерено, а не выведено: кадр на 90 и на 900 кадрах различается на 12.9 % пикселей при max 10
из 255 — это шум сходимости, а не снос на 810 м, который дал бы дрейф за 27 секунд.

Флага «стартовать в Play» в `Sandbox.hpp` нет. Поэтому **ветра в этих измерениях нет, и все они — о
движении КАМЕРЫ**, что и есть предмет всех четырёх проверок Epic: ни одна из них не про снос тома, все
четыре про смещение камеры и непрозрачную геометрию.

Что камера умеет — `--camera-to` / `--look-to`. Кадр N снимается из позы N, последний ложится точно на
конечную позу, поэтому **у каждого пути движения есть эталон: тот же ракурс, снятый неподвижно и сошедшийся
за 90 кадров.** Разность — ошибка реконструкции под движением, и ничего больше.

**Пол шума — ноль, и под движением тоже.** Три прогона неподвижного кадра и три прогона 180-градусной
панорамы дают по одному md5 каждый.

### ⚠️ НА КАКОЙ СЦЕНЕ ЧТО СНЯТО

Первый проход этих измерений был сделан на `Clouds_Demo` — до того, как §PR завёл эталонную сцену.
**Все заглавные числа пересняты на `Clouds_Protocol.desce`** и воспроизвелись **до последней цифры**:
E1 — те же 0/0/0/0 на четырёх путях движения и те же 2 683 и 2 598 на двух горизонтах; своды порога — те
же 1.6022 / 1.5248 / 1.4159 / 1.0878; коробка — те же 6.350 % и 21 из 255.

Причина совпадения названа, а не оставлена приятной случайностью: **сегодня две сцены рендерятся
побайтово одинаково** (0 различающихся из 706 560 на середине и на горизонте). То есть умолчания кода
сейчас равны выписанным в эталон значениям — и ровно это §PR и говорит: демо-сцена несёт 7 полей из 51,
остальные берёт из кода, поэтому её небо двигали двенадцать коммитов, а файл — восемь. Числа были верны,
но верны **по совпадению момента**; на эталонной сцене они верны по построению.

**Исключение, названное явно: `Clouds_Showcase`.** Всё, что ниже сказано про геометрию — E2 и ворота
`:402` — снято на ней, потому что это единственная сцена в репозитории, где перед облаками стоит
непрозрачная геометрия. Ни `Clouds_Demo`, ни `Clouds_Protocol` в кадрах протокола геометрии не содержат,
и на эталонной сцене ворота дают **ровно ноль** на обеих снятых точках — им там нечего менять.
Showcase эталоном не заморожена, и её числа поэтому слабее остальных.

### E1 — проверка разокклюзии, которая У НАС УЖЕ ЕСТЬ, над открытым небом НЕ СТРЕЛЯЕТ

`Clouds_Protocol`, геометрии в кадре нет. Порог поднят до 10⁹ км, то есть проверка снята полностью:

| точка | различающихся из 706 560 | max |
|---|---|---|
| pan 180° | **0** | 0 |
| yaw 90° | **0** | 0 |
| полёт 30 км | **0** | 0 |
| подъём в слой | **0** | 0 |
| зенит/середина от солнца и на солнце | **0** | 0 |
| горизонт от солнца | 2 683 (0.380 %) | 1 |
| горизонт на солнце | 2 598 (0.368 %) | 1 |

**Контроль (E1b), без которого этот ноль читать нельзя.** Ноль имеет два объяснения: проверка не стреляет,
либо история вообще не используется и никакая проверка на ней не важна. Обратная правка — проверка всегда
отвергает:

| точка | различающихся | max | coherence |
|---|---|---|---|
| pan 180° | 124 549 (17.6 %) | 11 | 0.810 |
| середина от солнца | 146 311 (20.7 %) | 17 | 0.935 |
| горизонт от солнца | 344 887 (48.8 %) | 25 | 1.387 |

История несущая, и её потеря — крупа (`coherence` < 1.4). Ноль реален.

**Насколько реален (E1d): порог как линейка.** 180° за 90 кадров, против арма «никогда не отвергает»:

| порог, км | различающихся | max | coherence |
|---|---|---|---|
| 2.0 (отгруженный) | **0** | 0 | — |
| 1.0 | **0** | 0 | — |
| 0.5 | **0** | 0 | — |
| 0.1 | 90 619 (12.8 %) | 11 | 0.702 |
| 0.01 | 121 376 (17.2 %) | 11 | 0.797 |
| 0.001 | 121 376 (17.2 %) | 11 | 0.797 |

Расхождение расстояний до сцены над открытым небом — **не более 0.5 км при пороге 2 км**. Оно не
исчезает: при 0.01 км отвергается 17.2 % кадра, и это крупа. То есть механизм расхождения существует
(гид пишет пикселю неба расстояние до ПЛОСКОСТИ дальнего отсечения, `50 км / cos θ`, и репроекция меняет
θ), но он вчетверо ниже порога. Предсказание, что при 6°/кадр он порог превысит, **проверено и не
подтвердилось** — разбор ошибки в §9 исследования.

### E2 — и ГДЕ она стреляет: на силуэтах непрозрачной геометрии

`Clouds_Showcase`, пять отдельно стоящих блоков на 100–950 м — это и есть «cloud over trees and small
details» из `:412`. Проверка снята:

| точка | различающихся | max | coherence |
|---|---|---|---|
| неподвижно | 1 988 (0.281 %) | 113 | 1.577 |
| панорама ±19° | 8 977 (1.271 %) | 97 | 1.599 |
| полёт 8 км вглубь | 0 | 0 | — |

**И она полезна.** Ошибка панорамы против неподвижного кадра той же позы:

| арм | весь прямоугольник | нижняя полоса, где геометрия |
|---|---|---|
| проверка включена (отгружено) | **6.7142** | **19.5092** |
| ворота UE (ниже) | 6.7189 | 19.5321 |
| проверка снята | 6.7403 | 19.6380 |

### Проверка №2 — семейство min/max: ВОРОТА реализованы и оказались вредны

Из всего семейства канала не требуют только ворота `:402-403`: разокклюзия применяется, лишь если
что-то в игре ближе `MinimumDistanceKmToDisableDisoclusion` (5 км, `VolumetricRenderTarget.cpp:68`).
Мы перенесли сам тест (`:416`/`:423` в симметричной форме) без его ворот.

| сцена, точка | различающихся | max |
|---|---|---|
| `Clouds_Protocol` горизонт | **0** | 0 |
| `Clouds_Protocol` pan 180° | **0** | 0 |
| `Clouds_Showcase` неподвижно | **0** | 0 |
| `Clouds_Showcase` панорама | 3 434 (0.486 %) | 27 |

**На эталонной сцене ворота МЁРТВЫ** — нечему срабатывать, потому что расстояние до сцены там всюду
дальнее отсечение, то есть далеко за 5 км. Единственное место, где они вообще что-то меняют, — сцена с
геометрией, и там **направление хуже**: 6.7189 против 6.7142 на всём кадре и 19.5321 против 19.5092 на
полосе с геометрией. Ворота выключают тест ровно там, где он приносил пользу.

То есть у ворот два разных приговора и оба против: на эталонной сцене — мёртвая настройка, на сцене с
геометрией — работающая и вредная. **НЕ ПРИНЯТЫ.**

Остальное семейство: `:408-414` — недостижимый код у самого Epic (цепочка вывода в исследовании §3);
`:480` **не измерено**, потому что требует канала min/max, которого марш не пишет, и это сказано прямо, а
не заполнено правдоподобным.

### Проверка №3 — дилатация: её ПОРОГ отбирает те же пиксели, что наш

Дилатация живёт в ветви, которую наш режим не компилирует, и её опорной величиной служит полуразрешённая
глубина сцены, которой резолв не получает. Что можно измерить, не строя её, — это её СПУСКОВОЙ КРЮЧОК:
относительный порог в 10 % (`:543`), единственный относительный во всём проходе. Он подставлен вместо
нашего абсолютного:

| сцена, точка | различающихся | max |
|---|---|---|
| `Clouds_Demo` pan 180° | **0** | 0 |
| `Clouds_Demo` горизонт | **0** | 0 |
| `Clouds_Showcase` панорама | 1 038 (0.147 %) | 56 |
| `Clouds_Showcase` неподвижно | 3 | 1 |

Две формы отбирают практически одно множество. Новизна дилатации не в том, КОГО она отвергает, а в том,
чем заменяет — соседом вместо сэмпла этого кадра, — и это **не измерено**: опорная глубина отсутствует.
**НЕ ПРИНЯТА.**

### Проверка №4 — цветовой box: платит кадром и покупает одну траекторию из четырёх

Реализована по `:570-603`: коробка по восьми соседям трассировки этого кадра плюс собственный сэмпл,
история зажимается покомпонентно. У Epic она выключена по умолчанию (`:58`), её `if (bApply)`
закомментирован (`:593`) и над ней висит живой `TODO` (`:569`).

Что она меняет в отгруженном кадре:

| точка | различающихся | max | coherence |
|---|---|---|---|
| зенит от солнца | 2 155 (0.305 %) | 2 | 0.581 |
| середина от солнца | 3 811 (0.539 %) | 11 | 0.708 |
| **горизонт от солнца** | **44 870 (6.350 %)** | **21** | 1.096 |
| pan 180° | 1 695 (0.240 %) | 1 | 0.554 |

Что она покупает — ошибка против неподвижного кадра той же позы:

| путь | отгружено | с коробкой |
|---|---|---|
| pan 180° | 5.9484 | 5.9483 |
| yaw 90° | 3.8163 | 3.8162 |
| полёт 30 км | 21.3559 | **21.5508**, coherence 41.0 → **50.0** |
| подъём в слой | 1.6022 | **1.2268** |

Две траектории не сдвинулись, одна стала **хуже** и притом ГЛАЖЕ — зажим тянет историю к
четвертьразрешённой окрестности, которая под быстрым переносом сама плохая оценка. Выигрыш один: подъём
в слой, где дешевле помогает проверка №1. **НЕ ПРИНЯТА.**

### Проверка №1 — минимальная дистанция репроекции: артефакт РЕАЛЕН, и всё равно не принята

Единственная из четырёх, за которой стоит измеренный артефакт. `:299`: репроекция запрещена, если фронт
облака ближе порога, потому что весь механизм приближает слой ОДНОЙ фронтальной поверхностью и вблизи
это приближение разваливается. Порог как линейка, каждый арм под контролем контрольной суммы шейдера:

| порог | зенит от солнца | середина | pan 180° | ошибка подъёма против эталона | bias | coherence |
|---|---|---|---|---|---|---|
| отгружено (нет проверки) | — | — | — | 1.6022 | +1.7434 | 5.582 |
| 0.5 км | 0 | 0 | 0 | 1.6022 | +1.7434 | 5.582 |
| **1.0 км** | **0** | **0** | **0** | 1.5248 | +1.6276 | 5.322 |
| **2.0 км** | **0** | **0** | **0** | **1.4159** | **+1.2439** | 5.256 |
| 4.0 км (совет Epic) | 70 759 (10.0 %) | 10 762 (1.5 %) | 15 105 (2.1 %) | **1.0878** | +0.6494 | 4.266 |

При 2 км **все отгруженные точки побайтово целы**, а ошибка подъёма в слой падает на 11.6 %, смещение —
на 28.6 %. При 4 км выигрыш втрое больше и ценой десятой части зенитного кадра.

> ⚠️ **Почему это всё равно НЕ принято, и причина измерена, а не выведена.** Порог нельзя потратить как
> константу. Верхняя граница «бесплатного» здесь — 2 км, и она не случайна: база слоя
> `Cumulus_Congestus` — **2.2 км**, а камера стоит на 2 м, так что ближайшее облако в зените находится в
> 2.198 км, и любой порог выше этого отбирает у неподвижного кадра правильную историю задаром. Но в
> отгруженной библиотеке **девять типов, и базы у них от 0.15 до 8.0 км**: у пяти из девяти база ниже
> 2 км. Константа, безопасная для всех, — это 0.15 км, а **0.5 км уже не меняет НИЧЕГО** (строка выше:
> 1.6022 против 1.6022, побайтово). То есть глобально безопасное значение **измеримо мертво**, а
> работающее — вредно для половины библиотеки. Контракт §1.3 запрещает мёртвые настройки.
>
> Epic ставит эту cvar в **0.0f** (`VolumetricRenderTarget.cpp:63`) и в тексте называет ту же цену:
> «clouds will look noisier when closer to that distance».
>
> **Что сделало бы её принимаемой**, названо здесь как предложение, а не как `TODO` в коде: порог,
> ВЫВЕДЕННЫЙ из базы слоя, а не константа. Это один `float` в `CloudResolveParams` (блок вырастет со 152
> до 156 байт, `static_assert` поймает) и одна строка в `VolumetricCloudRenderer`. Решение за тимлидом;
> в эту задачу такое расширение параметрического блока не входило.

### Цена

`--gpu-profile`, камера `0,200,0`, `--look 0,0.45,1`, 400 кадров, Debug, **перемежённо**. Колонка —
`gpu ms` (заголовок таблицы профайлера: `cpu ms | x | gpu ms | gpu self | x`). Инструментированное
против инструментированного: таймстемпы стоят сами по себе около 1.244 мс кадра и включены во всех
строках, поэтому строки сравнимы друг с другом и **ни с одним неинструментированным числом**.

Четыре перемежённых прогона, `Clouds_Demo`, машина сравнительно свободна:

| арм | четыре прогона | среднее | диапазон |
|---|---|---|---|
| отгружено | 0.171 0.186 0.178 0.180 | **0.179** | 0.171–0.186 |
| цветовой box | 0.200 0.206 0.207 0.196 | **0.202** | 0.196–0.207 |
| мин. дистанция 2 км | 0.169 0.208 0.183 0.160 | 0.180 | 0.160–0.208 |

**Диапазоны отгруженного и коробки не пересекаются** (0.186 < 0.196), поэтому +0.024 мс (+13 %) —
разрешённая величина, а не шум. Диапазон минимальной дистанции целиком накрывает отгруженный, и её
среднее совпадает с ним в третьем знаке: одно сравнение не стоит ничего измеримого.

Три перемежённых прогона на `Clouds_Protocol`, **машина под нагрузкой соседней сборки** (`load average`
14.8), приведены ради честности, а не ради точности:

| арм | три прогона |
|---|---|
| отгружено | 0.164 0.161 **0.306** |
| цветовой box | 0.189 0.209 0.232 |
| мин. дистанция 2 км | 0.192 0.197 0.185 |

Третий проход отгруженного — 0.306 при 0.164 и 0.161 в двух предыдущих — это машина, а не шейдер, и
дальше вся таблица плывёт вверх. **Она подтверждает знак для коробки и НЕ разрешает минимальную
дистанцию**: там, где первая таблица дала совпадение в третьем знаке, эта даёт 0.194 против 0.163, и две
таблицы противоречат друг другу. Правильный вывод — не среднее из них, а то, что 0.01–0.03 мс на проходе
в 0.17 мс лежит на границе разрешения этого стенда, и на нагруженной машине за этой границей.

**Отгруженный проход не изменился по построению**: единственный тронутый им файл — шапка шейдера, и
`git diff | grep -v '^[+-] *//'` по нему пуст, то есть ни одной некомментарной строки. Проверено
измерением, а не оставлено логикой: шесть точек, снятых шейдером `dev` и шейдером этой ветки (две разные
контрольные суммы — `55b4775f…` и `ad59012e…`), различаются **0 пикселями из 706 560** на каждой.

§GT'шные 0.502 мс относятся к другой машине и другой загрузке; воспроизвести их здесь нечем, и ни одно
число этой таблицы с ними не сравнивается.

### Шесть точек: ШЕСТЬ из шести, побайтово

`Clouds_Protocol`, камера `0,200,0`, `--shot-frames 90`, 1280×766. Правка — комментарий шапки резолва:

| точка | md5 | равно |
|---|---|---|
| зенит от солнца `0,0.9,-1` | `73c7806b04c1c71317e4aba52e3f20dc` | `SIL2_after_zenith_away.png` |
| середина от солнца `0,0.45,-1` | `4819e9c0c6dcdfadbf7477bd90a409d7` | `SIL2_after_mid_away.png` |
| горизонт от солнца `0,0.12,-1` | `304f4c2b56ea4751f50b6eb7b6351b4e` | `SIL2_after_horizon_away.png` |
| зенит на солнце `0,0.9,1` | `ada3c729466065ad749a473698b2f903` | `SIL2_after_zenith_sun.png` |
| середина на солнце `0,0.45,1` | `4a2ddc2a6a5bd7637701fd1a3fe7da8b` | `SIL2_after_mid_sun.png` |
| горизонт на солнце `0,0.12,1` | `bfd06fce1094adaa4e538cebba2f66f7` | `SIL2_after_horizon_sun.png` |

Те же шесть хешей были сняты **до** правки, на чистом дереве, и совпали с этой таблицей — то есть база
воспроизведена прежде, чем что-либо измерялось, и всё измеренное приписывается арму, а не дрейфу.
Кадры не коммитятся: это побайтовые копии уже лежащих в репозитории, и тождество само есть свидетельство.

И то же самое, но уже как ПРЯМОЙ A/B, а не как совпадение с таблицей: шесть точек сняты дважды на одном
и том же дереве — один раз с шапкой резолва из `dev` (`55b4775f33b3b3491731e3f6a8057704`), один раз с
шапкой этой ветки (`ad59012e6b37d01b3a9d4ecea0fe9876`). Контрольные суммы разные, картинки — **0
различающихся пикселей из 706 560 на каждой из шести**.

### Кадры

| файл | что показывает |
|---|---|
| `Shots/HV_dive_shipped.png` | подъём в слой как есть — «до» для проверки №1 |
| `Shots/HV_dive_mindist2km.png` | он же с порогом 2 км: 1.6022 → 1.4159 и ни одного тронутого отгруженного кадра |
| `Shots/HV_dive_mindist4km.png` | он же при совете Epic: 1.0878, ценой 10 % зенитного кадра |
| `Shots/HV_dive_still.png` | эталон — та же поза неподвижно, 90 кадров |
| `Shots/HV_zenith_mindist4km.png` | цена 4 км на неподвижном кадре: 70 759 пикселей там, где репроекция точна по построению |
| `Shots/HV_show_pan_shipped.png` | `Clouds_Showcase`, панорама: где проверка разокклюзии живёт |
| `Shots/HV_show_pan_nodisocc.png` | она же снятая — 1.271 % и max 97 на силуэтах блоков |
| `Shots/HV_horizon_box.png` | цена коробки: 6.35 % горизонтального кадра при max 21 |

Шесть из восьми пересняты на `Clouds_Protocol` и вышли **побайтово теми же**, что были сняты на
`Clouds_Demo`, — то же следствие того же тождества сцен. Два кадра `HV_show_pan_*` остаются на
`Clouds_Showcase`: это единственная сцена с непрозрачной геометрией перед облаками.

**Смотреть, а не только мерить.** `HV_dive_shipped.png` против `HV_dive_still.png` — дефект виден
глазом: над полосой горизонта справа от центра лежит **прямоугольная блочная заплата** застрявшей
истории, и слева от центра вторая, помельче; на неподвижном эталоне переход в этом месте гладкий.
`HV_dive_mindist4km.png` этих заплат не содержит вовсе. Это и есть «до» и «после» проверки №1 — в
кадре, а не только в числе 1.6022 против 1.0878.

### Диверсии

По инструменту — десять арм, разобраны в коммите `Tools/ImageDiff`; две остались зелёными, обе дыры
закрыты, и одна из них (сдвиг строки, применённый дважды) — ровно тот класс, что портит любую
прямоугольную выборку.

По самим измерениям диверсия устроена иначе, потому что проверять надо не функцию, а то, что кадр снят тем
шейдером, которым подписан. **Половина выводов этой задачи — нули, а ноль есть в точности то, что выдаёт
неприменившаяся правка.** Два случая произошли на самом деле:

1. **Регулярное выражение с пробелами внутри скобок** (`abs( x )` против `abs(x)`) не совпало ни разу, а
   `perl -pi` при несовпадении завершается успешно. Целый прогон ворот `:402` и целый прогон минимальной
   дистанции отчитались идеальными нулями, снятыми **неизменённым** шейдером.
2. **Чужой `pkill` по глобальному шаблону** убил пакет посреди работы. `trap EXIT` в bash при сигнале не
   выполняется, поэтому правка осталась в файле; пакет при этом выглядел законченным (13 кадров из 18 и
   сообщение о завершении). Сосед честно предупредил, и предупреждение окупилось в тот же час.

Отсюда `.scratch/patch.py` (правка по ДОСЛОВНОМУ якорю, промах — ненулевой код возврата) и
`.scratch/armed.sh` (контрольная сумма шейдера берётся до арма и сверяется перед и после КАЖДОГО кадра;
несовпадение — ошибка, а не число). Пойман и третий случай, уже своим: свод `e4b` отчитался, что при
0.5 км и при 2.0 км кадр в точности равен отгруженному, а при 1.0 км — нет. Три проверки вложены, так что
«равно, не равно, равно» арифметически невозможно; пересъёмка под контролем дала монотонный ряд из
таблицы №1 выше, где 2.0 км даёт 1.4159, а не 1.6022.

⚠️ **И четвёртый случай, который стенд диверсий обязан был закрыть с самого начала.** `make` сравнивает
время файлов **с точностью до секунды**, а цикл «правка → сборка → откат» проходит быстрее. Испорченный
объектник тогда остаётся слинкованным в «откаченном» дереве, и яд доживает до следующего арма, где даёт
зелёный, ничего не значащий. Первая версия стенда именно так и отчиталась: восемь арм из десяти вернули
чужие имена упавших тестов, а S2 повторил результат S1. Стенд теперь **удаляет и объектник, и бинарник**
перед каждой сборкой и **проверяет, что правка вообще изменила файл** (сравнением хешей до и после);
без второго из этих двух промах регулярного выражения выглядит как арм, а не как ошибка.

### Своды

Обе конфигурации, после `rm -rf build/Bin build/Tests build/Intermediates` и **до** сборки —
`CI=true premake5 gmake2`, в этом порядке: перегенерация посреди сборки молча теряет линковку.

| конфигурация | `BUILD-FAIL` | `FAIL` | сюит собрано |
|---|---|---|---|
| Debug | нет | нет | 69 |
| Release | нет | нет | 69 |

Строки `not-a-suite` — шестнадцать, одинаковые в обеих конфигурациях, и все шестнадцать прочитаны:

```
CloudLayoutBaker  CloudVolumeBaker  DShaderTool  DesertHeaderTool  FbxMeshSplitter
ImageDiff  ImageStat  LatticePeak  LineJump  PakTool  ProjectHub  SceneMigrator
Common  Desert  Runtime  Editor
```

Первые двенадцать — наши инструменты (`ImageDiff` среди них новый, эта задача его и завела), последние
четыре — три статические библиотеки и приложение. Настоящей сюиты среди них нет. В `case` цикла не
осталось ни одного нашего имени: только чужие библиотеки и два агрегата, как теперь и требует §2.4 п.5а.

### Что эта задача НЕ сделала

* **Ветра в кадрах нет**, и причина — движковая, а не сценная (см. выше). Бриф просил облака в движении;
  headless-путь этого не умеет, и добавление флага «стартовать в Play» — правка `Editor/Sandbox.hpp`,
  чужой файл и отдельная задача. Все четыре проверки Epic при этом про движение камеры и геометрии, так
  что предмет измерен; чего не измерено — поведение истории при сносе тома, для которого репроекции по
  одной фронтальной поверхности не существует в принципе, ни у нас, ни у Epic.
* **`:480` и ОТВЕТ дилатации не измерены.** Обоим нужен канал, которого марш не пишет; строить канал
  ради проверки, чей спусковой крючок (измерено выше) отбирает то же множество, что уже действующая
  проверка, — это цена в `Clouds: March` против неизмеренной пользы.
* **Параметрический блок резолва не расширен.** Вывод порога проверки №1 из базы слоя назван предложением
  с оценкой (один `float`, 152 → 156 байт) и оставлен тимлиду.

---

## PLAY — гейм-время в безголовой съёмке, и первый артефакт, который виден только когда движется МИР, 2026-08-26

Задача: §HV установил замером, что `Scene::OnUpdate` (`Engine/Core/Scene.cpp:322-323`) отдаёт системам
нулевой шаг вне Play, поэтому **каждый `--shot` за всю программу — снимок застывшего мира**. Движение
камеры подделывалось съёмкой из разных поз и это работало; всё, чей механизм в том, что мир движется под
временным буфером, в проверочный кадр не попадало ни разу.

**Результат: флаг `--play` есть, он детерминирован, умолчание не сдвинулось ни на бит — и он сразу нашёл
артефакт, которого мы не видели.**

### Флаг

`--play` (`Editor/Sandbox.hpp`, `Editor/Core/ShotOptions.hpp`) гоняет захват как сессию Play — через тот же
`OnScenePlay()`, что и кнопка тулбара, а не через второе определение Play. Два решения делают его прибором,
а не демонстрацией:

* **Фиксированный шаг 1/60 с** вместо настенных часов, на ВЕСЬ апдейт слоя (`ShotOptions::FrameSeconds`).
  Настенный шаг делает длительность симуляции функцией того, как быстро машина рисовала; два прогона одной
  команды разложили бы ветер по-разному. Поэтому **длительность задаётся `--shot-frames`, а не отдельным
  аргументом**: N кадров — это N/60 секунд мира, «снять на 30-й секунде» = `--play --shot-frames 1800`.
  Аргумента у флага нет намеренно — иначе два места решали бы, сколько кадров рисовать.
* **Активная камера пиннится перед входом в Play.** Play отдаёт вид `CameraComponent`'у сцены
  (`Scene::UpdateActiveCameraSource`), и в любой сцене с камерой `--camera`/`--look` были бы МОЛЧА
  проигнорированы: постановка позы ищет `EditorCamera` и просто не нашла бы её.

Шаг сделан константой, а не флагом, ещё и потому, что ручка приглашает к обратному: перемотать 30 секунд
большими шагами дёшево и ровно этим убить измеряемое — резолв репроецирует между СОСЕДНИМИ кадрами, и шаг
в десятую секунды отвергает историю каждый кадр.

### Умолчание: шесть точек из шести, побайтово

`Clouds_Protocol.desce`, камера `0,200,0`, `--shot-frames 90`, 1280×766, Debug. Бинарник `dev` и бинарник
этой ветки без флага:

| точка | md5 | равно `dev` | равно таблице §HV |
|---|---|---|---|
| зенит от солнца `0,0.9,-1` | `73c7806b04c1c71317e4aba52e3f20dc` | да | да |
| середина от солнца `0,0.45,-1` | `4819e9c0c6dcdfadbf7477bd90a409d7` | да | да |
| горизонт от солнца `0,0.12,-1` | `304f4c2b56ea4751f50b6eb7b6351b4e` | да | да |
| зенит на солнце `0,0.9,1` | `ada3c729466065ad749a473698b2f903` | да | да |
| середина на солнце `0,0.45,1` | `4a2ddc2a6a5bd7637701fd1a3fe7da8b` | да | да |
| горизонт на солнце `0,0.12,1` | `bfd06fce1094adaa4e538cebba2f66f7` | да | да |

⚠️ **Первый рендер в свежем worktree соврал и здесь.** Самый первый кадр в этом дереве дал
`2e6d765a2c556cace0e3f15117a33b8e` на «середине от солнца» вместо `4819e9c0…`; все последующие прогоны, тем
же бинарником, дают табличное значение. Прогретое дерево — часть измерения, а не гигиена.

### Детерминизм: не «должно», а замерено, и подтверждено обратной правкой

| прогон | различающихся из 980 480 |
|---|---|
| `--play --shot-frames 90`, два прогона, шесть точек | **0** на каждой из шести |
| `--play --shot-frames 1800` (30 с), два прогона, середина | **0** |
| то же, но шаг взят из настенных часов (арм S8) | **757 475 (77.3 %)**, max 117 |

Строка S8 — единственное, что делает первые две осмысленными: без фиксированного шага два прогона одной
команды расходятся на три четверти кадра.

### Флаг работает: N=0 против N=30 секунд, при ОДНОМ числе кадров

1800 кадров в обеих руках, чтобы различалась только «идёт ли время», а не сходимость:

| точка | различающихся из 980 480 | max | mean | coherence |
|---|---|---|---|---|
| зенит от солнца | 878 776 (89.6 %) | 149 | 14.96 | 43.4 |
| зенит на солнце | 967 614 (98.7 %) | 117 | 10.25 | 31.3 |
| середина от солнца | 880 247 (89.8 %) | 125 | 13.06 | 28.4 |
| середина на солнце | 925 240 (94.4 %) | 117 | 11.76 | 23.0 |
| горизонт от солнца | 946 840 (96.6 %) | 114 | 6.82 | 11.8 |
| горизонт на солнце | 862 166 (87.9 %) | 112 | 6.98 | 11.9 |

`coherence` 12–43 — это гладкое поле, то есть СНОС, а не рябь. Объяснение сходится с числами: `WindSpeed`
3000 ед/с × 30 с = 90 000 ед = 900 м переноса. Солнце на месте (`DriveSunFromTimeOfDay: false`), диск в
обоих кадрах в одном пикселе, так что единственное, что двигалось, — ветер.

### ⚠️ Находка: реконструкция считает, что между кадрами стоит МИР, а не только камера

Приём, который делает это измеримым без нового инструмента: **одно и то же состояние мира, достигнутое с
разной скоростью.** Смещение ветра — это произведение скорости на время, поэтому 3000 ед/с × 30 с и
30 000 ед/с × 3 с дают ОДНО состояние тома. Всё, чем отличаются эти два кадра, — отклик временного буфера
на СКОРОСТЬ движения.

| та же точка мира, достигнутая | различающихся | max | mean | coherence |
|---|---|---|---|---|
| 1× за 30 с против 10× за 3 с (900 м) | 484 430 (49.4 %) | 33 | **0.428** | 2.30 |
| 10× за 30 с против 100× за 3 с (9 000 м) | 884 017 (90.2 %) | 103 | **2.486** | 5.95 |

Ошибка растёт примерно в шесть раз на каждый десяток скорости. **На отгруженной скорости ветра она ниже
пола шума этой программы** (0.43 уровня из 255 против единицы, которой программа мерит), и кадр
`p1800_mid_away` глазом чист. **На сотне она видна глазом**: `PLAY_wind100x_mid.png` против
`PLAY_wind10x_mid.png` (то же состояние мира) — детали размыты вдоль ветра, а по силуэту облако/небо в
правом верхнем углу идёт **лесенка-шахматка** шириной около четырёх пикселей.

Четыре пикселя — это ровно тексель четвертьразрешённой трассы (`320×192` при кадре `1280×766`, лог:
«одна дрожащая субпиксельная позиция за кадр»), и механизм отсюда читается прямо: репроекция сдвигает
историю по КАМЕРЕ, а не по тому. При неподвижной камере она точна по построению — и собирает в один
пиксель четыре субпиксельных сэмпла, взятых в ЧЕТЫРЁХ РАЗНЫХ состояниях мира. Это не ошибка репроекции,
это отсутствующая у неё вторая ось.

**Что это значит для отказов §HV.** Проверка №4 (цветовой box) зажимает историю окрестностью сэмплов
ЭТОГО кадра — то есть она и есть защита ровно от этого. §HV отверг её на доказательствах, которые, как там
и сказано, могли покрыть только движение камеры и геометрии. Теперь движение тома на столе, и ответ пока
**тот же**: на отгруженной скорости артефакт в 0.43 уровня, а коробка стоит 6.35 % горизонтального кадра
при max 21 и +13 % времени прохода. **Что изменит ответ:** ветер на порядок выше умолчания в авторской
сцене либо более быстрая эволюция тома по любой другой причине. Мерить это надо тем же приёмом — двумя
скоростями к одному состоянию.

### Что начинает двигаться, когда время идёт

Названо поимённо, потому что бриф просил именно это:

| система | что делает при `--play` | в сценах репозитория |
|---|---|---|
| `VolumetricCloudECSSystem::AdvanceWind` | копит `m_WindOffset` — предмет задачи | да |
| `TimeOfDayECSSystem` | двигает солнце, **только если** `DriveSunFromTimeOfDay` | нигде не включено |
| `PhysicsECSSystem` | `m_World->Step(ts)` — Jolt | **нет ни одной сцены с физикой** |
| `AnimationECSSystem` | проигрывает скелетную анимацию | нет ни одной сцены со скином |
| `ScriptSystem` | Lua `OnUpdate` | `MainMenu.desce` |
| `AudioECSSystem`, `LocomotionSystem` | звук, локомоция | нет |

Проверено на сцене без физики и без анимации: `Fog_Showcase.desce` (у неё есть `CameraComponent`, то есть
она же проверяет пин) под `--play` даёт кадр **побайтово равный** кадру без флага — `43b504eb…` в обоих.
То есть флаг не «что-то шевелит на всякий случай»; он двигает ровно то, чему есть чем двигаться.

⚠️ **И побочная находка про ДО этой задачи.** `AnimationECSSystem` (`:27-32`) при нулевом гейм-шаге
подставляет `std::chrono::steady_clock` — настенные часы. То есть **любая сцена с играющей скелетной
анимацией никогда не была воспроизводима в `--shot`**, и это свойство отгруженного пути, а не нового
флага. Под `--play` гейм-шаг ненулевой, ветка с часами не берётся, и такая сцена становится
воспроизводимой впервые. Сцен со скином в репозитории нет, поэтому ни одна таблица программы этим не
задета.

### Диверсии

Девять арм, каждая со сборкой **после удаления объектника И бинарника** и с проверкой хешом, что правка
вообще легла в файл (дословный якорь, промах = ненулевой код возврата). Ни одна не осталась зелёной.

| арм | что сломано | чем поймано |
|---|---|---|
| S1 | `Play` по умолчанию `true` | `GameplayTimeIsOffUnlessAskedFor` |
| S2 | `PlayActive()` не смотрит на `Active()` | `PlayNeedsACaptureToMeanAnything` |
| S3 | шаг берётся из часов | `UnderPlayTheStepIsFixedAndIgnoresTheWallClock` |
| S4 | шаг 1/30 вместо 1/60 | он же + `SimulatedSecondsIsFramesTimesTheStep` |
| S5 | `SimulatedSeconds` без стража `PlayActive` | две сюитные проверки умолчания |
| S6 | отрицательные кадры не зажаты | `SimulatedSecondsIsFramesTimesTheStep` |
| S7 | `Play` начинает значить `HasMotion` | `PlayDoesNotDisturbTheCameraPath` |
| **S8** | фиксированный шаг убран из слоя | **кадром**: два прогона расходятся на 77.3 % |
| **S9** | пин камеры убран | **кадром**: `Fog_Showcase` снята камерой сцены, а не `--camera` |

S8 и S9 юнит-тестом не ловятся в принципе и пойманы кадрами: пара `PLAY_S9_pin.png` /
`PLAY_S9_nopin.png` — это два РАЗНЫХ ракурса одной сцены, и второй молча игнорирует командную строку.

### Кадры

| файл | что показывает |
|---|---|
| `Shots/PLAY_frozen_mid.png` | середина от солнца, 1800 кадров, время НЕ идёт — «до» |
| `Shots/PLAY_played_mid.png` | она же с `--play`: 30 секунд мира, облака снесло на 900 м |
| `Shots/PLAY_wind10x_mid.png` | то же состояние мира, достигнутое вдесятеро быстрее — эталон резкости |
| `Shots/PLAY_wind100x_mid.png` | оно же на сотне: размытие вдоль ветра и лесенка по силуэту |
| `Shots/PLAY_S9_pin.png` | `Fog_Showcase` под `--play` с пином — поза из `--camera` |
| `Shots/PLAY_S9_nopin.png` | она же без пина — камера сцены, командная строка проигнорирована |

### Как воспроизвести

```
# 30 секунд мира, эталонная сцена, середина от солнца
Editor --project Desert.deproj --scene Resources/Assets/Scenes/Clouds_Protocol.desce \
       --shot out.png --shot-frames 1800 --camera 0,200,0 --look 0,0.45,-1 --play

# то же состояние мира на порядок быстрее — прибор для артефакта выше.
# Сцены со скоростями делались во ВРЕМЕННОЙ копии и в репозиторий не клались:
#   sed 's/"WindSpeed":3000.0}}/"WindSpeed":30000.0}}/'  Clouds_Protocol.desce > Protocol_Wind10x.desce
#   sed 's/"WindSpeed":3000.0}}/"WindSpeed":300000.0}}/' Clouds_Protocol.desce > Protocol_Wind100x.desce
# 10x за 1800 кадров и 100x за 180 кадров — одно состояние; расхождение и есть отклик на скорость.
```

### Своды

`rm -rf build/Bin build/Tests build/Intermediates`, затем `CI=true premake5 gmake2`, затем обе цели
(`Desert`, `Editor`), затем цикл §2.4 п.5а. Порядок именно такой: перегенерация посреди сборки молча
теряет линковку, а цикл, запущенный по чистому дереву ДО целей, падает на каждой сюите с «No rule to make
target `libCommon.a`» — библиотеки для них строят цели, а не сюиты.

| конфигурация | `BUILD-FAIL` | `FAIL` | сюит собрано |
|---|---|---|---|
| Debug | нет | нет | 69 |
| Release | нет | нет | 69 |

Строки `not-a-suite` — шестнадцать, одинаковые в обеих конфигурациях, и все шестнадцать прочитаны:

```
CloudLayoutBaker  CloudVolumeBaker  DShaderTool  DesertHeaderTool  FbxMeshSplitter
ImageDiff  ImageStat  LatticePeak  LineJump  PakTool  ProjectHub  SceneMigrator
Common  Desert  Runtime  Editor
```

Двенадцать наших инструментов и четыре агрегата; настоящей сюиты среди них нет. Список совпадает с
§HV — новых имён задача не завела, `ShotPath` собирается и проходит в обеих конфигурациях.

### Что эта задача НЕ сделала

* **Цветовой box против сноса тома не измерен.** Он отгружен отвергнутым; строить его заново, чтобы
  проверить против артефакта, который на отгруженной скорости ветра лежит в 0.43 уровня, — решение
  тимлида, а не разработчика. Приём измерения и его цена названы выше полностью.
* **Скорость шага не вынесена в аргумент.** Названо как отказ, а не как недоделка: ручка здесь означала бы
  возможность перемотать время большими шагами, а это ровно то, что уничтожает измеряемое.
* **Сцен с физикой и анимацией в репозитории нет**, поэтому «физика под `--play` не ломается» проверено
  тем, что ей нечего ломать, и это сказано прямо, а не заполнено правдоподобным.

---

## NV — один том шума на слой: запись в долгах была почти верна, а поле мертво в трёх слотах из четырёх, 2026-08-27

Долг записан в `PLAN_CLOUD_TYPES.md` §«Что программа НЕ дала» так: **«Один том шума на весь слой, из
первого слота: cirrus рядом с cumulus эродируется томом кумулуса.»** Бриф пересказал это как «из слота
1». Первое, что сделала задача, — установила фактическое положение дел по исходникам, а не подтвердила
запись.

### Что установлено чтением, до единого кадра

**Сколько томов слой может нести.** Четыре. Том шума — поле ВИДА, а не слоя: `Assets::CloudTypeData::
NoiseVolume` (`Engine/Assets/CloudTypeData.hpp`), путь относительно корня ассетов, авторится выпадающим
списком в панели Cloud Type. Слой несёт четыре слота вида (`CloudType1..4`), значит может назвать до
четырёх томов, вплоть до четырёх различных.

**Сколько реально использует.** Ровно один. `VolumetricCloudRenderer::EnsureNoiseVolume` разрешает
единственный `m_NoiseVolume`, и он садится на единственный `sampler3D` — `u_CloudNoise`, binding 3 — в
каждом из двух облачных проходов (`CloudRaymarch.shader`, `CloudShadowMap.shader`). Потребитель
`CloudTypeService::GetNoiseVolume` в дереве ровно один: строка 1022 рендерера.

**Из какого слота, и осмысленно ли это.** **Не «из слота 1».** Из ПЕРВОГО НЕПУСТОГО слота: в
`EnsureNoiseVolume` цепочка `if ( firstType == Null() ) firstType = m_Data.CloudTypeN` по четырём полям.
Это тот же слот, который `ECS::ResolveCloudSpecies` сжимает в вид номер НОЛЬ, так что операционально
правило звучит «том вида-0». И это не случайность, пережившая рефакторинг: ограничение названо в коде
явно, заглавными, на двенадцати строках комментария (`VolumetricCloudRenderer.cpp:1004`), вместе с
ценой снятия. Случайность в другом месте — **правило «первый непустой» записано в дереве ДВАЖДЫ**, в
`ECS::ResolveCloudSpecies` и отдельной цепочкой здесь, и совпадают они только потому, что обе пока
говорят одно и то же.

**Что происходит при нескольких видах с разными томами.** Побеждает первый непустой слот, и побеждает
МОЛЧА: три остальных тома читаются с диска, живут в `CloudTypeService`, и ни один не биндится. Ни строки
в лог.

**Есть ли смысл в томе на вид.** Есть, и он измерим. Со времён Э5 том шума — это ЭРОЗИЯ и больше ничего:
`CLOUD_SAMPLE_NOISE` имеет ровно одну точку вызова во всём дереве шейдеров (`CloudField.glslh:463`, в
`CloudSampleDensity`), а форма тела приходит из печёного `CloudProceduralVolume`, который тома шума не
касается вовсе. Различие видов ВНУТРИ одного тома уже есть по двум осям — `DetailCharacter` выбирает
между парами wispy и billowy, `DetailFactor` задаёт глубину реза. Отдельный том добавляет третье, чего
эти две оси выразить не могут: **другой набор периодов решётки**, то есть МАСШТАБ кромки. Библиотека
этим пользуется: `Cirrus.decloudtype` — единственный из девяти — называет `CloudNoise_FineWisp.dcnv`
(периоды 4/8/6/12 против 2/4/3/6 у тома по умолчанию, вдвое мельче по каждому каналу), а подсказка самой
панели Cloud Type обещает художнику: «a cirrus cut from a cumulonimbus' noise is not a cirrus».

⇒ Поле есть, авторится, документировано как свойство вида — и исполняется для одного слота из четырёх.
Это §1.3 контракта («мёртвых настроек не бывает»), и запись в долгах верна по существу и неточна в
формулировке: не «из слота 1», а «из первого непустого», то есть «том вида-0».

### Побочная находка: устаревшее обоснование внутри `Cirrus.decloudtype`

Заметки шипнутого ассета утверждают: «since the coverage field moved onto the billowy Alligator pair,
**the volume a type names decides its PLACEMENT CELLS as well as its edge**, and CloudNoise_FineWisp
halves both», и на этой связи обоснован выбор `PlacementScale 0.6`. Связи больше нет: Э5 перенёс
размещение на решётку комков, `BakeCloudProceduralVolume` тома шума не читает (ни одного упоминания в
`CloudProceduralVolume.{hpp,cpp}`, кроме исторических), и единственная точка вызова `CLOUD_SAMPLE_NOISE`
— эрозия. Названо здесь как найденное, а не как исправленное: перекалибровка `PlacementScale` — не эта
задача.


### Дефект показан кадром: одно поле, живое в слоте 1 и мёртвое в слоте 2

Опыт устроен так, чтобы менялась РОВНО ОДНА вещь. Две сцены, обе — копия `Clouds_Protocol.desce`, в
которой изменены только слоты видов:

| сцена | слот 1 | слот 2 |
|---|---|---|
| `NV_CumulusFirst` | `Cumulus_Congestus` (тома не называет) | `NV_Cirrus` |
| `NV_CirrusFirst` | `NV_Cirrus` | `Cumulus_Congestus` |

`NV_Cirrus.decloudtype` — побайтовая копия шипнутого `Cirrus.decloudtype`, у которой между двумя
прогонами меняется РОВНО ОДИН ключ: в первом `"NoiseVolume": "Clouds/CloudNoise_FineWisp.dcnv"`, во
втором ключа нет вовсе (том по умолчанию). Ни один шипнутый ассет не тронут.

Двенадцать кадров на прогон: три высоты × два азимута, `--camera 0,200,0`, `--shot-frames 90`,
1280x766, **все — одним и тем же бинарником**, собранным до единой правки исходников.

**Пол повтора измерен, а не предположен**: та же команда, выполненная дважды на зенитной точке
`NV_CirrusFirst`, даёт **0 расходящихся пикселей из 980 480, max 0**. Все числа ниже поэтому точны.

| точка | цирус в слоте 2 (`NV_CumulusFirst`) | цирус в слоте 1 (`NV_CirrusFirst`) |
|---|---|---|
| зенит от солнца | **0 / 980 480, max 0** | 711 533 / 980 480 (**72.57 %**), max **61**, coherence 2.861 |
| середина от солнца | **0 / 980 480, max 0** | 684 289 (**69.79 %**), max 24, coherence 1.849 |
| горизонт от солнца | **0 / 980 480, max 0** | 498 428 (**50.84 %**), max 21, coherence 1.666 |
| зенит к солнцу | **0 / 980 480, max 0** | 668 635 (**68.20 %**), max 32, coherence 1.829 |
| середина к солнцу | **0 / 980 480, max 0** | 709 991 (**72.41 %**), max 32, coherence 2.090 |
| горизонт к солнцу | **0 / 980 480, max 0** | 495 248 (**50.51 %**), max 24, coherence 1.779 |

Хеши зенитной точки, на дубли: `A_fine` и `A_default` — **один и тот же файл**
(`66ba8d6b83c8b921e7689c325dc2132c` дважды), `C_fine` `b9a3f84dabbb388cee156ce45a4f8c07` против
`C_default` `eb777b319a77a5d6860a2c968f0e4090` — разные.

> **Один и тот же ключ одного и того же файла: в первом слоте он двигает от половины до трёх четвертей
> кадра, во втором — ноль пикселей из 980 480, во всех шести точках.** Это не «почти не влияет» и не
> «влияет слабо»: это ровно та мёртвая настройка, которую §1.3 контракта запрещает.

Coherence 1.67–2.86 говорит, чем именно является разница в живом слоте: полем СВЯЗНЫМ, а не рябью.
Мельче том — короче волокна кромки, и перераспределяется материал, а не дрожит выборка.

**Каким прибором проверялось, что цирус вообще есть в обеих сценах**: обе несут оба вида, и это видно
на кадре — кучевые башни у зенита, тонкие волокнистые полосы ниже. Если бы во второй сцене цируса не
было, нулевая разница ничего бы не доказывала.

### Почему запись в долгах промахнулась именно в этом слове

«Из слота 1» и «из первого непустого слота» — не одно и то же ровно тогда, когда слот 1 пуст. Кода,
который берёт «слот 1», в дереве нет; есть цепочка по четырём полям. Практическая разница в том, кого
винить: при пустом первом слоте побеждает не слот 1, а слот 2 — и художник, у которого цирус лежит в
слоте 2 при пустом первом, ВИДИТ свой том и делает вывод, что всё работает. Именно так долг и дожил до
конца программы.

### Что починено, и почему именно так

Том шума на вид теперь доходит до марша из ЛЮБОГО слота. Цена — **четыре `sampler3D` на каждом из двух
облачных проходов вместо одного** и **один `vec4` в блоке параметров** (`CloudGpuPayload::SpeciesNoise`,
блок 252 → 268 байт).

**Почему не атлас.** Фаза Э4-A2 упёрлась в ту же стену — отражение этого движка отказывает массиву
дескрипторов дословно («arrays of descriptors are not supported — declare separate bindings») — и ответила
АТЛАСОМ: восемь тел, сложенных по глубине, одна выборка, арифметика вместо ветвления. Здесь это неверный
ответ, и причина ровно одна: **координата эрозии не ограничена и опирается на REPEAT самого сэмплера.**
Тела в атласе адресуются зажатой координатой и по построению не достают до соседа; шум обязан замыкаться
по всем трём осям, а в атласе под каждым замыканием окажутся тексели соседнего тома — это шов в небе на
каждом периоде эрозии, а не экономия.

**Памяти это не стоит НИЧЕГО, и этот факт определил форму решения.** `Assets::AssetPreloader` загружает на
устройство КАЖДЫЙ `.dcnv` проекта при старте, независимо от того, что называет сцена
(`PreloadCloudNoiseVolumes`, «loaded eagerly, unlike meshes»). Оба шипнутых тома лежали в памяти всё это
время; не хватало только дескриптора. Поэтому бюджет D-9 (64 МиБ) не двигается вовсе, и §A2's «8.00 МиБ
noise volume» остаётся одной строкой, а не четырьмя.

**Обычное небо не подорожало, и это проверено числом, а не обещанием.**
`Graphic::ResolveCloudNoiseVolumes` СХЛОПЫВАЕТ четыре хэндла: слой, чьи виды называют один том на всех —
а восемь из девяти шипнутых видов не называют никакого — отправляет `{0,0,0,0}`, цепочка сравнений в
марше однородна по волне, и выборка одна, как и была. Без схлопывания четырёхвидовое небо на ОДНОМ томе
платило бы за расхождение волны на каждой выборке поля.

**Герой-облако по-прежнему эродируется томом вида 0.** Это не выбор, а запись того, что было: до этой
фазы слой биндил один том — первого заполненного слота, то есть вида 0 — и скульптурное тело резалось им
же. Названо в коде и закреплено тестом.

### Дефект закрыт — тот же опыт, тот же прибор, новый бинарник

Всё двенадцать кадров — **одним** бинарником, собранным после правки; тот же `NV_Cirrus.decloudtype`,
тот же один ключ между прогонами.

| точка | цирус в слоте 2 ДО | цирус в слоте 2 ПОСЛЕ |
|---|---|---|
| зенит от солнца | 0 / 980 480 | **218 656 (22.30 %)**, max 13, coherence 2.214 |
| середина от солнца | 0 / 980 480 | **242 831 (24.77 %)**, max 9, coherence 1.781 |
| горизонт от солнца | 0 / 980 480 | **85 770 (8.75 %)**, max 7, coherence 0.767 |
| зенит к солнцу | 0 / 980 480 | **96 907 (9.88 %)**, max 11, coherence 1.240 |
| середина к солнцу | 0 / 980 480 | **154 534 (15.76 %)**, max 14, coherence 1.537 |
| горизонт к солнцу | 0 / 980 480 | **97 778 (9.97 %)**, max 13, coherence 1.346 |

Глазом на середине от солнца: волокнистые полосы цируса слева вверху из гладких длинных размывов
становятся короткими текстурными прядями. Амплитуда невелика (max 7–14 из 255), и это не недоделка — это
то, чем цирус является: `DensityFactor 0.35`, `ExtinctionFactor 0.25`, четверть оптической плотности
кумулуса. Двигать он может только свою собственную четверть.

### Небо, которому нечего было терять, не потеряло НИ ОДНОГО БАЙТА

Сильнее всего это говорит не таблица разниц, а таблица нулей. Та же сцена `NV_CumulusFirst`, у которой
`NV_Cirrus` НЕ называет тома, — то есть оба вида на томе по умолчанию, `ResolveCloudNoiseVolumes` даёт
один слот и `{0,0,0,0}`:

| точка | старый бинарник против нового |
|---|---|
| все шесть | **0 / 980 480, max 0** |

Это при том, что между двумя бинарниками: в блоке параметров прибавился `vec4` (сместились смещения
`Aerial`), на каждом проходе прибавилось по три сэмплера, в точке выборки появилась четырёхсторонняя
развилка, и весь SPIR-V пересобран. §A0 измерял, что одна только пересборка SPIR-V сдвигала 199 пикселей
из 980 480 на один уровень; здесь не сдвинулось ничего.

### Эталонная сцена: шесть хешей из шести совпали с ЗАПИСАННЫМИ

`Clouds_Protocol.desce`, `--camera 0,200,0`, `--shot-frames 90`, 1280x766, три высоты × два азимута,
новый бинарник:

| точка | md5 | записано в §PT |
|---|---|---|
| зенит от солнца `0,0.9,-1` | `73c7806b04c1c71317e4aba52e3f20dc` | совпадает |
| середина от солнца `0,0.45,-1` | `4819e9c0c6dcdfadbf7477bd90a409d7` | совпадает |
| горизонт от солнца `0,0.12,-1` | `304f4c2b56ea4751f50b6eb7b6351b4e` | совпадает |
| зенит к солнцу `0,0.9,1` | `ada3c729466065ad749a473698b2f903` | совпадает |
| середина к солнцу `0,0.45,1` | `4a2ddc2a6a5bd7637701fd1a3fe7da8b` | совпадает |
| горизонт к солнцу `0,0.12,1` | `bfd06fce1094adaa4e538cebba2f66f7` | совпадает |

**Шесть хешей различны между собой** — дублей нет, это шесть разных кадров, а не один, снятый шесть раз.

⚠️ **Оговорка к этой таблице, потому что она выглядит слишком удачной.** §PT записал эти шесть хешей для
`Clouds_Demo`, а сняты они здесь на `Clouds_Protocol`. Это не ошибка и не совпадение: блок
`VolumetricCloud` протокольной сцены выписан целиком (51 поле), а демо-сцены — разреженно (7 полей плюс
умолчания компонента), и разрешаются они в одни и те же настройки. То есть эти шесть хешей — эталон,
снятый ДРУГИМ бинарником в другой день и другой фазе, и новый бинарник воспроизводит его побайтно.

### Пять чисел, которые нельзя было потерять

| | сданное | измерено сейчас | прибор |
|---|---|---|---|
| покрытие неба | 0.7446 | **0.7459** | `LatticePeak --field`, 32 реализации, конфигурация `Clouds_Demo` |
| решётка вдоль ветра | 0.0000 | **LATTICE 0.0000 по обеим осям** | там же |
| запас эрозии | ≥1.10× над полом марша | **1.11×** (138.7 м против 125 м) | `CloudField`, печатает сама |
| ядро congestus | 1.8 : 1 | **1.8 : 1** | `LatticePeak --field --type Cumulus_Congestus --coverage 0.5` |
| покрытие по родам при `Coverage 0.5` | внутри 0.06 | **внутри 0.056** (худший — altocumulus 0.5560) | то же, девять родов |

Покрытие неба читается 0.7459 против записанных 0.7446 — расхождение 0.0013, и оно НЕ этой задачи: ни
одна строка изменённого кода не достаёт до печки. `LatticePeak` линкует `BakeCloudProceduralVolume` и
`CloudTypeShape`; изменены блок параметров, биндинги и точка выборки шума в шейдере — ничего из этого в
прибор не входит. Сама §RW2 публикует для одной величины 0.7431, 0.7432 и 0.7446 в трёх местах, так что
0.0013 — ширина её собственного разброса по флагам вызова, а не сдвиг.

Девять родов при `Coverage 0.5`:

```
Cumulus_Humilis    0.5261  core 5.4 : 1     Stratus       0.5065  core 38.4 : 1
Cumulus_Mediocris  0.5172  core 3.7 : 1     Altocumulus   0.5560  core  2.2 : 1
Cumulus_Congestus  0.5130  core 1.8 : 1     Cirrus        0.5084  core  1.4 : 1
Cumulonimbus       0.5471  core 1.9 : 1     Lenticular    0.5047  core  3.1 : 1
Stratocumulus      0.5213  core 1.8 : 1
```

Восемь из девяти совпадают с таблицей §SIL2 до четвёртого знака. Кумулонимбус читается 0.5471 против
записанных там 0.8543 — но §PT ниже той таблицы уже переизмерил его (`SIL_Cumulonimbus` 0.9856 → 0.7644 при
`Coverage 0.762`), то есть род переавторили после §SIL2, и 0.8543 — устаревшая строка, а не расхождение.
По самому критерию — «внутри 0.06» — сегодняшние 0.5471 проходят, а записанные 0.8543 не прошли бы.

### Диверсии: пять, все с УДАЛЕНИЕМ объектника и бинарника

`make` сравнивает время с точностью до секунды, поэтому каждая диверсия сносит
`build/Intermediates/Debug/Debug/<сюита>` и `build/Bin/Tests/Debug/<сюита>` перед сборкой. Зелёная
поломка здесь была бы дырой в методе, а не удачей.

| # | что сломано | должна поймать | результат |
|---|---|---|---|
| 1 | `result.NoiseSlot = params.SpeciesNoise[slot]` → `= 0` (победитель всегда читает том 0) | `CloudField` | **RED** — `TheWinningSpeciesEdgeIsCutFromItsOwnNoiseVolume` |
| 2 | скульптурное тело читает том 1 вместо тома вида 0 | `CloudAuthored` | **RED** — `TheSculptedBodyIsErodedByTheFirstSpeciesVolume` |
| 3 | резолвер перестаёт схлопывать одинаковые тома | `ComponentReflection` | **RED** — `TheNoiseVolumesAreDeduplicatedAndEverySlotStaysBindable` |
| 4 | у марша убран `Uniform(12) sampler3D u_CloudNoise3` и его ветвь | `ShaderCacheKey` | **RED** — `TheCloudMarchDeclaresThirteenDescriptorsInSetZero` |
| 5 | из GLSL-блока убран `u_CloudSpeciesNoise`, C++-структура не тронута | `ShaderCacheKey` | **RED** — `TheCloudParameterBlockIsTheSameNumberOfBytesOnBothSidesOfTheWire` |

Пятая — про дыру, которую эта задача НАШЛА и закрыла по дороге. `static_assert`-ы в `CloudPayload.hpp`
закрепляют смещения C++-структуры друг против друга и не достают до GLSL-блока, который эти байты читает;
две стороны правятся в разных файлах и на разных языках. Член, добавленный с одной стороны, сдвигает все
последующие, и это не ошибка, а «облака как-то не так настроены». Новый тест берёт у SPIR-V ОБЪЯВЛЕННЫЙ
РАЗМЕР storage-блока и сравнивает его с `sizeof( CloudGpuPayload )` — на обоих проходах, потому что блок
у них один. Размер, а не смещения: `VulkanShaderReflection` заполняет смещения членов для uniform-буферов
и только размер для storage-буферов, а расширять его — файл другой задачи.

### Что рассмотрено и ОТКЛОНЕНО: масштаб эрозии на вид

Асимметрия, которую эта фаза НЕ сняла и называет прямо. Том решает, какая у кромки СТАТИСТИКА; в мир его
переводит `DetailTileKm` — и это свойство СЛОЯ, одно на все четыре вида. То есть «насколько крупная у
этого вида кромка» разложено на две половины, живущие в разных местах.

Для шипнутой пары это ровно эквивалентно: `CloudNoise_FineWisp` — это `CloudNoise_Default` с ВДВОЕ
меньшими периодами по всем четырём каналам (4/8/6/12 против 2/4/3/6), так что множитель тайла на вид дал
бы то же самое. В общем случае — нет: том может отличаться seed'ом, силой curl и непропорциональными
периодами, и ни одно из трёх множителем тайла не выражается.

**Ручка не добавлена.** Она описывала бы то, что том уже описывает, и делала бы это хуже — двумя числами
вместо одного файла, который художник видит и печёт. Добавить её значило бы дать два способа сказать одно
и то же и гарантировать, что однажды они разойдутся. **Что изменит ответ:** если появится вид, которому
нужен масштаб кромки, отличный от соседей, но ТА ЖЕ статистика — тогда печь второй том ради одного числа
станет дороже ручки, и её стоит вернуть.

### Чего эта задача НЕ проверила кадром

**Слоты 2 и 3 марша не гнались тремя РАЗНЫМИ томами в кадре, потому что проект шипит два `.dcnv`.**
Проверено иначе, и это сказано как есть: слот 1 (binding 10) гнался кадром — это та самая рука, которая
пошла с 0 на 22 %; слоты 2 и 3 закреплены отражением SPIR-V (`ShaderCacheKey`, тринадцать дескрипторов,
каждый по номеру) и резолвером (`ComponentReflection`, три различных тома дают `DistinctCount 3`).
Ниже — что было сделано, чтобы закрыть и это.

### Рецепт опыта, чтобы его можно было повторить

Ни сцены, ни типы опыта в репозиторий не кладутся: это три файла, которые делаются из шипнутых за
секунду, а сцена, которую никто не сопровождает, гниёт быстрее, чем рецепт. Рецепт целиком:

```bash
# 1. Тип-подопытный: побайтовая копия шипнутого цируса, у которой между прогонами меняется
#    РОВНО ОДИН ключ.
python3 - <<'PY'
import json, collections, os
T = "Editor/Resources/Assets/Clouds/Types"
t = json.load(open(os.path.join(T, "Cirrus.decloudtype")), object_pairs_hook=collections.OrderedDict)
t["DisplayName"] = "NV Cirrus"; t["Notes"] = "Experiment asset. Not shipped."
t["NoiseVolume"] = "Clouds/CloudNoise_FineWisp.dcnv"   # второй прогон: del t["NoiseVolume"]
json.dump(t, open(os.path.join(T, "NV_Cirrus.decloudtype"), "w"), indent=4)
PY

# 2. Две сцены — копия Clouds_Protocol.desce, в которой изменены ТОЛЬКО слоты:
#      NV_CumulusFirst : CloudType1 = Cumulus_Congestus, CloudType2 = NV_Cirrus
#      NV_CirrusFirst  : CloudType1 = NV_Cirrus,         CloudType2 = Cumulus_Congestus

# 3. Шесть точек на сцену, ОДНИМ бинарником:
#      --camera 0,200,0 --shot-frames 90  и  --look из {0,0.9,-1  0,0.45,-1  0,0.12,-1
#                                                       0,0.9,1   0,0.45,1   0,0.12,1}

# 4. Мера:
#      ImageDiff <a.png> <b.png> 0 0 1280 766
```

Третий том — для проверки слота 2 марша — делается ещё дешевле: `cp CloudNoise_FineWisp.dcnv
NV_FineWispCopy.dcnv`. Байты те же, ХЭНДЛ другой (он выводится из пути,
`Desert/Tests/Engine/CloudNoiseVolumeHandle`), поэтому резолвер видит три РАЗЛИЧНЫХ тома и третий вид
садится на binding 11 — а кадр обязан совпасть с кадром, где тот же вид называет `CloudNoise_FineWisp`
напрямую и садится на binding 10. Совпал — значит binding 11 реально прочитан; разошёлся — значит нет.

### Слот 2 марша, проверенный кадром через побайтовую копию тома

Проект шипит два `.dcnv`, поэтому трёх РАЗНЫХ томов взять неоткуда — но три разных ХЭНДЛА взять можно, и
резолвер работает с хэндлами. `cp CloudNoise_FineWisp.dcnv NV_FineWispCopy.dcnv` даёт файл с теми же
байтами (`b4d1a8b7fa151066ce08ecb4563c6f52` у обоих) и другим хэндлом, потому что хэндл выводится из пути.

Сцена `NV_ThreeVolumes` — снова копия `Clouds_Protocol.desce`, три слота:

| слот | вид | том | binding |
|---|---|---|---|
| 1 | `Cumulus_Congestus` | нет (по умолчанию) | 3 |
| 2 | `NV_Cirrus` | `CloudNoise_FineWisp` | 10 |
| 3 | `NV_Alto` | `NV_FineWispCopy` → **11** / `CloudNoise_FineWisp` → 10 | **11** против 10 |

Между прогонами меняется только том третьего вида, и обе версии — одни и те же байты. Значит марш,
который binding 11 действительно читает, обязан выдать ТОТ ЖЕ КАДР; марш, у которого этот дескриптор
не записан или прочитан не тот, — не обязан.

СНЯТО RELEASE-БИНАРНИКОМ, оба прогона одним и тем же, и это сказано прямо: колонка самодостаточна
(сравниваются два её собственных кадра), а Debug-бинарника на этот момент в дереве не было — он был
снесён ради чистого свода. Первый рендер в этой конфигурации выброшен, как и положено.

| прогон | том третьего вида | слот | зенит | середина | горизонт |
|---|---|---|---|---|---|
| «copy» против «shared» | `NV_FineWispCopy` (binding 11) против `CloudNoise_FineWisp` (binding 10) | 2 против 1 | **0 / 980 480** | **0 / 980 480** | **0 / 980 480** |
| «copy» против «none» | `NV_FineWispCopy` (binding 11) против ничего (binding 3) | 2 против 0 | **17.16 % / 5.45 %** | **15.31 % / 9.40 %** | **4.81 % / 6.14 %** |

Три руки, а не две, и третья обязательна. Пара «copy против shared» одна не отличает «слот 2 читается
верно» от «индекс вообще не читается и всё берётся из слота 0»: при втором обе руки дали бы
альтокумулусу том по умолчанию и всё равно совпали бы. Третья рука — та, от которой обе обязаны
отличаться, и она отличается на 4.8–17.2 % пикселей при max 3–6 из 255.

Вместе это закрывает обе поломки: дескриптор 11 действительно записан и прочитан (иначе первая строка
разошлась бы), и индекс действительно консультируется (иначе не разошлась бы вторая). Чего эти кадры НЕ
доказывают: что при запросе слота 2 читается именно binding 11, а не 10 — в руке «copy» они несут
одинаковые байты. Это закрывает `ShaderCacheKey`, который проверяет каждый номер по отдельности против
`Graphic::kCloudNoiseBindings`.

### Своды в обеих конфигурациях

`rm -rf build/Bin build/Tests build/Intermediates`, затем `CI=true premake5 gmake`, затем обе цели, затем
цикл. Оба свода — от снесённых объектников И бинарников.

| конфигурация | `BUILD-FAIL` | `FAIL` | `NO-BINARY` | сюит собрано и пройдено |
|---|---|---|---|---|
| Debug | нет | нет | нет | **71** |
| Release | нет | нет | нет | **71** |

⚠️ **Своя же ошибка в отчётности, названная, потому что она чуть не съела сюиту.** Debug-свод был снят
через `| tail -90` при 89 makefile'ах, и первая строка вывода — `AnimGraph`, первая по алфавиту — была
срезана моим же конвейером. То есть свод показывал 70 строк `PASS`, а не 71, и срезана могла быть с тем
же успехом строка `FAIL`. Release-свод, снятый целиком, показывает `PASS AnimGraph`; списки сюит в двух
конфигурациях совпадают в точности, кроме этой строки. `AnimGraph` пересобран и прогнан в Debug
отдельно — **6 тестов, PASSED**. Свод в Debug: 71 из 71.

Строк `not-a-suite` — восемнадцать, одинаковые в обеих конфигурациях, и все восемнадцать прочитаны:

```
BuildAllTests  RunAllTests                                        — два агрегата
Common  Desert  Runtime  Editor                                   — наши библиотеки и приложение
CloudLayoutBaker  CloudVolumeBaker  DShaderTool  DesertHeaderTool
FbxMeshSplitter  ImageDiff  ImageStat  LatticePeak  LineJump
PakTool  ProjectHub  SceneMigrator                                — двенадцать наших инструментов
```

Настоящей сюиты среди них нет, и это проверено, а не заявлено: свод после цикла перебирает всё, что
СОБРАЛОСЬ в `build/Bin/Tests/<config>`, и печатает `SKIPPED BUT IS A TEST` для любого имени, которое
одновременно в списке пропуска и является тестовым бинарником. Ни одной такой строки нет ни в Debug, ни
в Release. Список против §PT вырос на две строки — `ImageDiff` и `LatticePeak`, инструменты, появившиеся
после того свода, — и это ровно та ситуация, в которой §7 верификации велит перепроверять список.

Сторонние библиотеки (`GLFW`, `ImGui`, `Jolt`, `Lua`, …) в цикл не попадают вовсе: их makefile'ы лежат в
`ThirdParty/`, а цикл идёт по `*.make` верхнего уровня. Их имена в `case` остаются как страховка на
случай, если генератор когда-нибудь начнёт класть их рядом.

---

## §VP — the vertical profile: what a sample count buys, and why not a texture

**Р2, 2026-08-28.** The task was the owner's one remaining UE capability gap: authored vertical shape.
Decision D-22 had required a curve; the owner relaxed that and asked the measurement to choose between a
curve and a four-channel painted texture, since Р1's canvas had made the texture path cheap. It was
measured. The curve won, for a reason D-22 never gave.

### The consumer resolves six points, and that is the whole ceiling

The vertical profile is read by the lump-stack layout in `CloudProceduralVolume.cpp` and by nothing else.
The stack is `kBlobsPerCluster` = 6 lobes at fixed heights `t = ((step + 0.5) / 6) ^ 1.7`:

    t = 0.0146, 0.0947, 0.2258, 0.4000, 0.6132, 0.8625

So the entire difference a storage resolution can make is the piecewise-linear error **at those six
heights**. That is computable rather than arguable. Worst case over every taper the shipped library uses
(0.35 … 0.55), expressed as the lump-radius error it causes on the shipped congestus' 2.16 km cluster:

| N samples | 4 | 6 | 8 | 12 | 16 | 24 | 32 | 64 | 256 |
|---|---|---|---|---|---|---|---|---|---|
| max rel. error | 0.322 % | 0.103 % | 0.043 % | 0.025 % | 0.0085 % | 0.0031 % | 0.0024 % | 0.0007 % | 0 |
| lump radius | 6.96 m | 2.22 m | 0.92 m | 0.53 m | **0.18 m** | 0.07 m | 0.05 m | 0.01 m | 0 |

The march's finest resolvable chord is **125 m** (`CloudFinestResolvableChordKm`, 256 max steps). Sixteen
samples are wrong by a seven-hundredth of the smallest thing the march can see. **Two hundred and fifty-six
samples buy 0.18 m of cloud.** A painted raster's extra rows are not detail; they are storage nothing reads.

### And the channels were already spent

Unreal packs four types into R/G/B/A because its profile is ONE texture shared by the whole material —
"each channel describes the profile shape and relative altitude of a different cloud type". Ours is not
shared: a type IS a file (`.decloudtype`, D-11). Four channels would carry four copies of one type's
profile, three of them dead in the sense of DEV_CONTRACT.md §1.3. The channel-per-species arrangement is
already expressed, one level up, by `.dclayout` — where the channel is the SLOT.

### Why sixteen and not eight

Fidelity was spent at four samples, so the sample count is set by the **authoring** grid instead: the
finest feature an artist can place is `band / 15`, and the thing that draws it is six lobes over the same
band.

| type | band | grid at N=16 | lobe spacing | headroom |
|---|---|---|---|---|
| Stratus | 0.40 km | 27 m | 67 m | 2.5x |
| Cumulus_Mediocris | 1.00 km | 67 m | 167 m | 2.5x |
| Cumulus_Congestus | 3.60 km | 240 m | 600 m | 2.5x |
| Cumulonimbus | 8.10 km | 540 m | 1350 m | 2.5x |

Eight would put the congestus' grid at 514 m against a 600 m lobe spacing — authoring at exactly the
granularity of the thing drawing it, with no headroom for a corner landing between two lobes. Thirty-two
is five times finer than the stack and is the unreachable storage refused above.

**What would change the answer.** Raise `kBlobsPerCluster`, or make the lobe count depend on the band, and
the consumer stops being six-tap. A stack of sixty lobes reads a curve sixty times and this table has to be
re-measured, not re-quoted.

### A second refusal, inside the first: the footprint quadrature was not re-derived

`CloudClusterTowerFootprintRadii` used to be a line through two quadrature constants in `TopTaper`
(0.9594 R and 0.9051 R, §CB above). With the taper gone, the obvious move was to re-run that quadrature
over the authored curve. **It was built and measured first.** An independent grid quadrature over this
file's own layout reproduces the two committed constants to **0.2 per cent** — 0.9614 / 0.9040 against
0.9594 / 0.9051 — which is what makes generalising legitimate at all. What it also has is **realisation
noise**: the answer moves **0.5 per cent** between one set of wobble draws and another (0.9614 at 16
realisations, 0.9531 at 32, 0.9548 at 48). A runtime quadrature would make a calibrated constant depend on
an arbitrary seed count, and the committed constants were measured once, carefully, and are better numbers
than anything computable per bake.

So the curve is mapped onto the calibrated line by its **mean lobe half-width at the stack's own six
heights**, extrapolated rather than clamped outside the old law's range. The mapping is the identity for
any curve that re-expresses a taper, which is what keeps the version-3 library pricing exactly as version 2
did. **Revisit if the lobe count or the disc law changes** — that is what would make the constants stale.

### What the sign error cost, and what caught it

The first version of that mapping guarded the division with `std::max( span, 1e-6f )`. The span is
`atFullTaper - atNoTaper`, which is **negative** — a taper narrows the mean half-width — so the guard
returned `1e-6` every single time and the equivalent taper came out scaled by about -96 000. It was found
in one run by `TheTowersFootprintConstantsSayWhatTheLayoutActuallyDoes`, which predicted a footprint ratio
of **6.2466** against a sky that said **0.9341**, and dragged three further tests red behind it because the
gain had collapsed to 1 for every type. That test's own header says it exists because a sabotage found the
hole; it has now caught a live one. The guard is on the magnitude.

### The frames

`Clouds_Protocol`, one seed (1), one camera (`0,200,0`), 90 frames, `--play`. The only thing that differs
between the two is the sixteen numbers in `Cumulus_Congestus.decloudtype`.

| file | what it is |
|---|---|
| `Shots/P2_profile_deck_mid.png` | the deck preset — the same width base to top |
| `Shots/P2_profile_tower_mid.png` | the tower preset — pinched base, swelling upper half |
| `Shots/P2_dome_deck.png` | whole dome, deck, 8 azimuths x 5 elevations |
| `Shots/P2_dome_tower.png` | whole dome, tower, the same 40 rays |

**The noise floor of this scene is exactly zero** — the same command run twice gave 0 differing bytes of
980 480 pixels, so every number below is signal. Deck against tower at the mid angle: **90.20 % of pixels
differ, max delta 122/255, mean delta over the differing pixels 22.25**. The tower's bodies read as necked
turrets with bulbous caps; the deck's read as flat continuous masses. Neither sheet has an empty zenith, a
band, or an angle at which the difference disappears.

Both sweeps were shot with the SAME binary, built before either of them, so the two sheets differ only by
the asset. The first render in this worktree was discarded per verification §7.

### The silent field-shift, which is the thing to remember from this task

`CloudTypeShape` is an aggregate, and five fixtures across two suites built one by POSITION:

    constexpr CloudTypeShape kSheet{ 0.15f, 0.55f, 0.88f, 0.12f, 0.35f, 0.0f, 0.0f, ... };

The fifth slot was `TopTaper`. When the profile curve took its place, brace elision fed `0.35f` into
`Profile.HalfWidth[0]` and shifted **every number after it one slot up the array** — altitudes became
widths, widths became altitudes. It **compiled**, in a `constexpr` context, with no warning, and produced
shapes that drew no cloud at all: three tests in `CloudField` and one in `ComponentReflection` went red
with `cloudyBelow = 0`, `bothPresent = 0`, `checked = 0`.

Two things worth carrying forward:

* **Searching for the removed field's NAME does not find these.** Not one of the five fixtures contains the
  string `TopTaper` — the taper is just a number in a row. The search that finds them is for the TYPE.
* **They are now named field by field**, and `CloudProfileFromTaper` is `constexpr` precisely so that a
  `constexpr` fixture can name the profile instead of positioning it.

The suites caught it, which is the system working. What is worth noting is that they caught it by
measuring the SKY — "no cloud below the ceiling", "two species never overlap" — and not by any assertion
about the struct.

---

## Р12 — the sky-light occlusion volume is switched ON in the shipped sky, 2026-08-31

Decision **D-26** held `SkyOcclusionVolume` off in every scene until Р9 reported. Р9 closed as a refusal,
so the second re-authoring the owner was waiting to avoid is not coming and the hold has nothing left to
hold. This section is the measurement that goes with turning it on, and it is a re-authoring: **the
appearance of every cloud scene in the repository changes.**

**The argument is about the instrument and not about one sky.** Every measurement this programme takes is
taken against the SHIPPED configuration, and with the flag off that configuration carried, in every frame
anybody measured, the largest single discrepancy `DIAGNOSIS_CARTOON.md` ranks — an ambient sky term with
no geometric occluder. Р11's census is about nine hundred captures taken against exactly that. It is a tax
on everything downstream, not a one-off loss.

### What moved, and why it is two channels rather than one

| where | from | to |
|---|---|---|
| `ECS/VolumetricCloudComponent.hpp` — `SkyOcclusionVolume` | `false` | **`true`** |
| `ECS/VolumetricCloudComponent.hpp` — `AmbientOcclusionStrength` | `0.5f` | **`1.0f`** |
| `Clouds_Protocol.desce`, `PR_Hero0/3/8.desce` — both keys | `false` / `0.5` | **`true` / `1.0`** |

**Both channels are required and §PR is why.** Thirty-six scenes carry a `VolumetricCloud`; thirty-two of
them write between 3 and 16 of its 52 fields and take the rest from the C++ default, so the default is the
only thing that reaches them. The other four — the protocol scene and the three hero cost legs — write
**all 52 fields explicitly**, precisely so that no C++ default can move them (§PR: "a scene that states
all fifty-one of its cloud parameters cannot be moved by a change to a C++ default"). Changing only the
default would therefore have left the measuring instrument on the old sky, which is the one scene where
that matters most. Changing only the scenes would have left the other thirty-two, and every scene authored
tomorrow, behind.

**The fourteen `.desce` with no volumetric cloud are untouched and carry no new key.** A scene with no
cloud has no business carrying the flag, and none of them does.

### Why 1.0 and not the 0.5 UE carries

Not because twice as much occlusion was wanted. Р7 found the volume's UPPER-HEMISPHERE transmittance being
multiplied into a FULL-SPHERE mean radiance and replaced the product with a composition, so the term went
from `1 - s(1 - T)` to `1 - (s/2)(1 - T)`: **every strength now buys what half of it used to.** 1.0 after
the fix is arithmetically the term Р4 measured at 0.5 before it.

That is a claim about arithmetic, and it is verified here on the FRAME rather than taken. Р4's
`VOLUME at 0.5` column, re-shot at `AmbientOcclusionStrength` 1.0 on today's binary, reproduces **to three
decimals at all six protocol points**:

| point | reference | Р4's `VOLUME at 0.5` (pre-fix) | Р12 measured at 1.0 (post-fix) |
|---|---|---|---|
| zenith away | 0.479 | 0.395 | **0.395** |
| mid away | 0.479 | 0.383 | **0.383** |
| horizon away | 0.438 | 0.282 | **0.282** |
| zenith sun | 0.479 | 0.340 | **0.340** |
| mid sun | 0.479 | 0.275 | **0.275** |
| horizon sun | 0.438 | 0.303 | **0.303** |

Half strength would buy half of that. It would also not answer the one risk this term still carries — the
SLAB approximation, which Р4 named and Р7 left standing as an unmeasured second-order error biased dark —
because that bias scales with the strength like everything else does. What removed the visible consequence
of it, the brown deck at full strength, was Р7's composition and not a smaller dial.

### The instrument and its floor

`Clouds_Protocol.desce`, camera `0,200,0`, `--shot-frames 90 --play`, Debug, MoltenVK, 1280x766.
`ImageStat` over `0 0 1280 551`, `ImageDiff` over the whole frame.

**The floor was measured, not assumed**, and the first render in this fresh worktree was discarded per
§A1's correction — which was not a formality:

| repeat | differing | max |
|---|---|---|
| `mid_away`, same command twice | **0** / 980 480 | 0 |
| `zenith_away`, same command twice | **0** / 980 480 | 0 |
| the DISCARDED first render vs the settled one | **4** / 980 480 | **1** |

Four pixels is nothing, and it is also exactly the size of thing an A/B of 9/255 would have absorbed
without anybody noticing.

**And every frame was checked against its own log by a line that names the CONFIGURATION rather than the
scene.** `desert-engine-verify` records that the log prints `SceneName`, not the path, so two `.desce`
copied from one original are indistinguishable in it — and here both legs are the same file, edited
between them, so the name is identical by construction and could not have distinguished them at all. The
check used instead is the renderer's own allocation line, `[Clouds] Sky-light occlusion volume 128x16x128
RGBA16F (2.00 MiB) — 48 km across the world at 375 m per texel, 16 altitude slices over the shell`, which
is printed only when the pass is dispatched: **0 of 40 tiles in the OFF leg, 40 of 40 in the ON leg.** The
two control scenes used below WERE copies, and were given distinct `SceneName`s — `P12_vol_s05` and
`P12_vol_s00` — for the reason the skill gives.

### The six protocol points — the new base

This table replaces §PR's as the thing a later phase re-shoots as its "before". The `before` column
reproduces Р0's and Р4's `shipped` column exactly at all six points, so the chain from §PR through Р0 is
intact and this is one more link in it rather than a break.

| point | contrast before | contrast after | reference | gap before | gap after | closed |
|---|---|---|---|---|---|---|
| zenith away `0,0.9,-1` | 0.417 | 0.395 | 0.479 | −0.062 | −0.084 | **worse** |
| mid away `0,0.45,-1` | 0.395 | 0.383 | 0.479 | −0.084 | −0.096 | **worse** |
| horizon away `0,0.12,-1` | 0.202 | **0.282** | 0.438 | −0.236 | −0.156 | **34 %** |
| zenith sun `0,0.9,1` | 0.296 | **0.340** | 0.479 | −0.183 | −0.139 | **24 %** |
| mid sun `0,0.45,1` | 0.254 | **0.275** | 0.479 | −0.225 | −0.204 | **9 %** |
| horizon sun `0,0.12,1` | 0.248 | **0.303** | 0.438 | −0.190 | −0.135 | **29 %** |

and the full statistics, before → after:

| point | mean | p05 | p50 | p95 | sat | mean Δ/255 | bias |
|---|---|---|---|---|---|---|---|
| zenith away | 0.569 → 0.519 | 0.317 → 0.317 | 0.557 → 0.495 | 0.734 → 0.712 | 0.098 → 0.086 | 8.94 | −11.47 |
| mid away | 0.533 → 0.490 | 0.324 → 0.321 | 0.546 → 0.476 | 0.719 → 0.704 | 0.176 → 0.165 | 9.43 | −12.12 |
| horizon away | 0.606 → 0.553 | **0.521 → 0.433** | 0.598 → 0.535 | 0.723 → 0.715 | 0.085 → 0.073 | 7.57 | −9.76 |
| zenith sun | 0.569 → 0.506 | **0.497 → 0.424** | 0.542 → 0.475 | 0.794 → 0.763 | 0.084 → 0.070 | 12.08 | −15.57 |
| mid sun | 0.560 → 0.506 | **0.465 → 0.429** | 0.545 → 0.480 | 0.719 → 0.703 | 0.117 → 0.102 | 10.60 | −13.65 |
| horizon sun | 0.590 → 0.539 | **0.494 → 0.431** | 0.573 → 0.511 | 0.743 → 0.734 | 0.113 → 0.103 | 7.18 | −9.27 |

### The whole dome, and the reading of it that matters

`Shots/P12_dome_occoff.png` and `Shots/P12_dome_occon.png` — 40 tiles each, 8 azimuths x 5 elevations,
`--shot-frames 90 --play`.

Whole-frame `ImageDiff` per tile: **every one of the forty moved**, 55.4 % to 100 % of pixels, mean Δ 2.54
to 12.54 of 255, max 20 to 42, and **the bias is negative at all forty** (−3.24 to −16.21). The term
darkens, everywhere, which is what an occluder does.

**Contrast (`p95 − p05`) falls at 34 of the 40 tiles and rises at 6. That statistic is misleading here and
the mechanism is exact.** Where a tile contains clear sky, `p05` IS the sky — and this term touches only
cloud, so `p05` cannot move while `p95`, the sunlit cloud top, falls a little; `p95 − p05` can then only
shrink. Where a tile is filled with deck, `p05` is cloud, and it falls hard. Checked against the tiles
rather than asserted: at the losing tiles `p05` is identical to three decimals before and after, and **the
six tiles that gain contrast are exactly the six whose `p05` drops** — `AZ 135 / EL 25, 45, 65`,
`AZ 180 / EL 25, 45` and `AZ 225 / EL 45`, all of them the sunward sector at mid elevation where the frame
is deck rather than sky. The largest is `AZ 135 / EL 45`, 0.283 → 0.327.

So the honest summary is **not** "contrast improves". It is: *the term puts a dark end into frames that had
none, and where a frame had no dark end to gain it simply darkens the cloud a little.* The six protocol
points, which are the only angles with a UE reference to be right or wrong against, close 34 %, 29 %, 24 %
and 9 % of the gap at four of six and lose 0.022 and 0.012 of contrast at the two away-azimuth points
whose `p05` is blue sky.

### And the frames, because no statistic here decides it

`Shots/P12_el25_pair.png` and `Shots/P12_el45_pair.png` — the two mid elevations this programme's defects
have hidden at, off on the left and on on the right.

At `EL 25` the change is not subtle. The OFF frame is the "flat white cut-out" the component's own tooltip
describes: lobes lit as brightly underneath as on top, and the deck at `AZ 135` a featureless pale wash.
The ON frame gives every lobe a grey underside and the deck a shaded ceiling. At `EL 45 / AZ 000` — one of
the two points whose contrast statistic went DOWN — the flat white mass across the middle of the frame
gains a modelled body while the lobe tops stay bright. **The tile whose number got worse is one of the
tiles that looks most improved**, which is why this section leads with the frames and not with the table.

**Nothing in the sweep looks worse.** The failure this was watched for — Р4's brown deck at full strength —
is not present and could not be: it was the pre-fix term, and Р7 removed it. Saturation falls a little
everywhere (0.098 → 0.086 at the zenith away, 0.117 → 0.102 at mid sun), which is what removing sky light
from a shaded region does, and no point goes warm.

### The price — measured by Р4, not re-measured here

0.410 ms of the pass's own GPU self time and **2.00 MiB per view, FIXED rather than resolution-scaled**
(128x16x128 over the field's own region). Against D-9's budgets: 20.5 % of the 2 ms and 3.1 % of the
64 MB. Re-measuring it was explicitly out of scope; what IS verified here is that the pass really is
dispatched — the allocation line above, in 40 of 40 tiles, quoting the 2.00 MiB itself.

### What this makes stale, and what was NOT done about it

Every figure in `DIAGNOSIS_CARTOON.md`, `CONTROL_CENSUS.md` and this file that was taken from a rendered
frame of a cloud scene was taken against the occlusion-off sky. **None of them has been adjusted.** Each
document now carries a banner at its head saying so, and the figures that are stale in a stronger sense
than "a moved baseline" are flagged where they stand:

* `CONTROL_CENSUS.md` rows **36** and **37** — row 36 swept `AmbientOcclusionStrength` against the PROFILE
  term, which the shipped flag no longer selects, so it is not a larger or smaller version of the shipped
  measurement but a different quantity; row 37's `on` leg was at strength 0.5. Both are re-measured at the
  row.
* `DIAGNOSIS_CARTOON.md` ranking **#1** and **#2** and the three knock-out tables of §3. #1 and #2 are the
  finding this task acts on.
* §PR's `THE NEW BASE` table in this file, superseded by the six points above.
* `P8_DOME_AND_THE_LOW_DECK.md` §4.2, which was not in the brief's list and needed two corrections of its
  own — see there.

### Two things found while doing this, neither of them about the occlusion

**`P8_DOME_AND_THE_LOW_DECK.md` §4.2 says "six scenes even mention the field".** It was four, at that
document's own commit and today:
`git grep -l SkyOcclusionVolume 8d4ae29c -- Editor/Resources/Assets/Scenes` returns `Clouds_Protocol` and
`PR_Hero0/3/8`. Its substantive claim — that no scene switched it on — was correct.

**The same §4.2's four rows were taken at strength 1.0, and the document does not say so.** No strength
appears anywhere in the section and the shipped default at the time was 0.5, so it reads as "only the flag
was flipped". Re-measuring the same A/B here at `AZ 000 / EL 25` gives mean Δ **9.3561** / bias **−12.0336**
against its 9.36 / −12.03, while the same A/B at strength 0.5 gives **1.74** / **−1.49**. Its rows are
therefore about the configuration that now ships, and its conclusion — "not a clean win and it is not
free" — applies to Р12 directly.
