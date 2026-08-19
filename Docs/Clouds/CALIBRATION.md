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
