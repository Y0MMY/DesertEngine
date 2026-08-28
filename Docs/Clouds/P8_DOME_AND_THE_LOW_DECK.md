# Р8 — the whole dome, and why the deck reads as low

Two deliverables, and the contract's §0 is why they are one task: an instrument nobody uses is
unfinished work, so the sweep is delivered *together with* the question it was built to answer.

1. **`DomeSheet` + `scripts/MacOS/DomeSweep.sh`** — the whole celestial dome in one run, assembled
   into one sheet with the angles in the filename and burnt into every tile. `desert-engine-verify`
   §1 now prescribes it instead of the six points.
2. **The owner's complaint, answered with numbers.** The brief's hypothesis is **REFUTED**, and the
   number that refutes it is **one level out of 255, over 2.79° of a 90° dome**. What is left is
   named, measured and **proposed rather than done**, because it is a change to shipped scene values.

**Nothing in the renderer was touched.** Every experiment below is one authored field of a temporary
copy of `Clouds_Protocol`; the shipped scene is unmodified and every temporary was deleted.

### Five statements in the task brief that are false about this tree

Collected here because the next person will be handed the same brief.

| the brief says | measured |
|---|---|
| "the deck is cut at **36 %** of the distance it should reach" | `MaxViewDistance` is measured **from the layer entry**, not the camera (`CloudRaymarch.shader:337`, and the component's tooltip says so in capitals). It never puts a floor under the deck. §2.1 |
| "at that cut the base stands **2.1°** above the horizon, and that is the strip of clear blue" | the cap can only act **below 2.79°**, and there it clips the layer's TOP, not its base. The one-degree pale band is the aerial-perspective fade, and it does not move when the cap is raised. §2.1, §2.4, §3.1 |
| "`WeatherTileSize`/thickness = 12/3.6 = **3.33**" | the arithmetic is right and the pair is wrong: the placement lattice is `WeatherTileSize / 4`, so the governing ratio is **3.0/3.6 = 0.83**. §4.1 |
| "raising `MaxViewDistance` **costs march time**" | it is **0.66 ms FASTER** at the longest march in the sky, and all three runs beat all three of the base's. §5 |
| "the complaint has returned after a partial fix, so either the fix was incomplete or there are two mechanisms" | neither. The earlier fix is at a local optimum in both directions, and the mechanism is a **third** one that has never been part of this complaint's history. §6 |

The `TracingStartMaxDistance` = 350 km, base 2.2 km / top 5.8 km, 167 km and 272 km horizons,
`MaxViewDistance` = 60 km and the §5 refutation of the weather-cell direction are all correct as
stated, and are used as given.

---

## 0. Method, and the two things a reader must know first

**Scene** `Editor/Resources/Assets/Scenes/Clouds_Protocol.desce` (all 51 cloud fields written out, so
no C++ default can move it). Camera `0,200,0`, `--play`, Debug, MoltenVK, 1280×766, `--shot-frames 90`.

**The noise floor is 0.** `mvd060_az000_el02` shot twice — **0 differing pixels of 980 480**. The two
runs were taken under *different machine load* (a 40-tile sweep was running for one of them and not
the other), so the floor is zero and is robust to the neighbours. Every pixel figure below is the
change and nothing else.

**The first render in this worktree was discarded** (verify skill §7).

**`--shot`'s bad-scene guard was verified, not assumed.** `--scene NoSuchScene_P8.desce` logs
*"does not exist; refusing to capture a different scene under that name"* and writes **no PNG**. Р5
landed and works.

**Row → elevation.** Several findings below are stated in degrees rather than in pixel rows. The map
uses the EDITOR camera's projection, which is *not* the plain vertical FOV: it anchors apparent size
to a 1080 px reference height (`Engine/Core/Camera.cpp`, `EditorCamera::UpdateProjectionMatrix`), so
at 766 px the vertical FOV is `2·atan(tan(22.5°)·766/1080)` = **32.74°**, not 45°. The map is checked
against an independent landmark rather than trusted: it puts elevation 0.00° at **row 539** of the
horizon frames, and §PR and §DS both record the checker floor's own edge at **y 540**. On the 2°
frames it puts the floor edge between −0.5° and 0.0°. Two frames, two pitches, agreement to a quarter
of a degree.

**Only §5 quotes a timing.** The machine is shared; that one is the `VolumetricClouds` scope's own GPU
line, three scenes interleaved in one session on an otherwise idle machine, minimum of three, with the
spread beside it. Nothing else in this document depends on a cost number.

---

## 1. THE INSTRUMENT — the whole dome, not six patches of it

```bash
scripts/MacOS/DomeSweep.sh Resources/Assets/Scenes/Clouds_Protocol.desce /tmp/dome
```

**8 azimuths × 5 elevations (5/25/45/65/85°) = 40 tiles**, ~25 minutes on this machine at 90 frames.
`Docs/Clouds/Shots/P8_dome_base.png` is the result.

### What was wrong with six

Six look directions is a **sample**, and the two defects the six points exist to catch were both found
by accident rather than by them:

* the empty zenith above ~20°, found by the owner looking up after a ten-merge programme shot almost
  entirely from the horizon;
* the full-width horizontal bands, which needed **sunward azimuth AND high elevation at the same
  time** and so could not be produced by any protocol that varies one axis at a time
  (`REVIEW_622a01a6.md` Ц9).

And both are **arbitrary in azimuth.** Nothing about a lattice of weather cells, a periodic noise or a
placement seed makes a defect appear along `-Z` rather than at 135°; the bands appeared at one azimuth
because that is the azimuth somebody happened to shoot. Two directions out of a circle is a
one-in-four chance of standing in the right place.

### The one relation the instrument rests on

**The label burnt into a tile and the `--look` vector that produced it must be two views of ONE
number.** Evidence that mislabels itself is worse than no evidence, because it still looks like
evidence — and a driver script that formatted `AZ 090` while passing `--look 1,0,0` would be
individually correct on both sides and caught by no test of either. That is the defect shape the
verify skill's table lists four times over.

So `DomeSweep.sh` **computes no angles at all**. It asks `DomeSheet --plan` for the vector, the file
stem and the label *together*, and `Desert/Tests/Tools/DomeSheetLayout` asserts the round trip
`label → angles → vector` over a full 40-tile plan, plus:

* every look vector is a unit direction **at the elevation its own label claims** (`asin(y)` against
  the label, at 7 elevations × 12 azimuths — the bound that catches a sine/cosine swap, which a spot
  check at 45° never would);
* no ray appears twice and no column is 360° (a sweep whose last tile repeats column 0 leaves a wedge
  of sky unshot — the exact hole the instrument exists to close);
* every cell fits inside the sheet the geometry declared, including a partial last row;
* the box filter conserves the mean rather than darkening it, and a single bright pixel in a 4×4 block
  survives as a raised mean instead of being point-sampled away — the thin-cirrus case a reduced sheet
  would otherwise report as empty sky;
* the label is drawn, does not leave its bar, and fits inside a 320 px tile at the shipped reduction.

16 tests, all passing, and the suite links nothing at all — the layout header depends on no glm, no
engine and no stb, so the agreement can be asserted on a machine that cannot build a renderer.

**Azimuth 0 is `-Z` and azimuth 180 is `+Z`.** That is a decision, not a convention: it puts the old
six points on this dome as COLUMNS rather than as a different set of rays, so `AZ 000 / EL 45` and
`--look 0,1,-1` are the same ray and a number from the old corpus is comparable with a tile of the new
one.

**Weight.** The tiles are deleted unless `--keep-tiles` is given. `Docs/Clouds/Shots` is already
508 MB of a 5.6 GB repository, and forty 1.2 MB frames per sweep is not a thing to keep — the sheet is
the deliverable, at 2.4 MB.

### What the baseline sheet says that six points could not

Read bottom-up, the way the sky is read:

| row | what the whole circle looks like |
|---|---|
| **EL 85** | discrete lumps, deep blue between them, ~half the sky open — at every azimuth |
| **EL 65** | larger masses, blue narrowing to channels |
| **EL 45** | **a closed white ceiling from AZ 090 to AZ 225**; blue survives only on the away side |
| **EL 25** | a ceiling at every one of the eight azimuths |
| **EL 05** | a dense mat of small lumps, with the pale horizon band under it |

Two things are visible only because the sweep is a sweep:

1. **The ceiling is a ceiling in every direction.** Between 25° and 65° there is no azimuth with an
   open sky in it. A protocol shooting `-Z` and `+Z` reports the two extremes of the sun axis and
   cannot say whether the sky in between is open.
2. **The apparent lump size barely changes between EL 25 and EL 85** — one size of cloud, at every
   elevation, at every azimuth. That is the absence of a depth ladder, and §3 is what it is.

---

## 2. THE BRIEF'S HYPOTHESIS — REFUTED

> *"`MaxViewDistance` in the scene is 60 km. The deck is cut at 36 % of the distance it should reach.
> At that cut the base stands 2.1° above the horizon — and that is exactly the strip of clear blue."*

### 2.1 The cap is not measured from the camera, and the source says why

`Programs/Clouds/CloudRaymarch.shader:337`:

```glsl
segment.y = min(segment.y, segment.x + max(u_CloudLayer.w, 0.0f));
```

`segment.x` is the ray's **entry into the layer**, not the camera. The comment two lines above states
the reason — *"a limit measured from the camera would cut the layer short at a fixed radius and put a
visible circular edge in the sky"* — and the component's own tooltip says it in capitals:
*"measured FROM THE POINT THE RAY ENTERS THE LAYER"*.

**So the 60 km never puts a floor under the visible deck.** Every ray gets its full 60 km *starting at
the cloud base*, at every elevation, right down to the horizon. What the cap can do is stop a ray
before it reaches the layer's TOP, and only where the in-layer chord exceeds 60 km.

The shell arithmetic, `d(R) = −R₀·sinθ + √(R₀²sin²θ + R² − R₀²)` with R₀ = 6360.002 km (camera at
200 cm), base 6362.2 and top 6365.8 km:

| elevation | entry (base) | exit (top) | in-layer chord | capped at | altitude reached | cap binds? |
|---|---|---|---|---|---|---|
| 0.00° | 167.22 km | 271.63 km | 104.41 km | 227.22 km | 4.06 km | yes |
| 1.00° | 89.71 | 182.44 | 92.73 | 149.71 | 4.38 | yes |
| 2.00° | 55.94 | 128.83 | 72.88 | 115.94 | 5.10 | yes |
| **2.79°** | **42.28** | **102.28** | **60.00** | — | **5.80** | **the boundary** |
| 3.00° | 39.64 | 96.77 | 57.12 | — | 5.80 (full) | no |
| 5.00° | 24.67 | 62.98 | 38.30 | — | 5.80 (full) | no |
| 25.00° | 5.20 | 13.69 | 8.49 | — | 5.80 (full) | no |
| 90.00° | 2.20 | 5.80 | 3.60 | — | 5.80 (full) | no |

> **The 60 km cap can only reach elevations below 2.79°, and even there it removes the top of the
> layer and never the base.**

The brief's "36 %" is `60 / 167`, which would be the right sum if the cap were measured from the
camera. It is not. Its "2.1°" is `atan(2.2/60)` — the elevation of the base at 60 km from the
*camera* — and is a coincidence of the same mistake landing near the real boundary of 2.79°, which is
a different quantity (where the *top* starts being clipped).

### 2.2 On the frame, the prediction is confirmed to within one row — and it is worth one level

`--camera 0,200,0 --look 0,0.0349,∓0.9994` (elevation 2°, so the frame spans −14° to +18° and contains
the whole affected band). Rows 364..428 are elevations 2.79°..0.00°.

| `MaxViewDistance` 60 km → 300 km | pixels differing | max | mean Δ/255 |
|---|---|---|---|
| **rows 364..428 — the band the cap CAN reach (2.79°..0.00°), az000** | **77 633 / 81 920 = 94.77 %** | 14 | **1.03** |
| rows 0..364 — above 2.79°, az000 | 1 846 / 465 920 = **0.40 %** | 7 | **0.0021** |
| **the same band, az180 (sunward)** | 77 610 / 81 920 = **94.74 %** | 13 | **0.98** |
| above 2.79°, az180 | 1 659 / 465 920 = **0.36 %** | 7 | **0.0018** |

Against a measured noise floor of **0**.

The band the arithmetic named is 94.8 % changed; everything above it is 0.4 % changed at a mean of two
thousandths of a level. **The relation predicted from the shell geometry is confirmed on the frame,
and what it buys is one level out of 255 in 2.79° of a 90° dome.**

### 2.3 And beyond 120 km the parameter is dead range

| | pixels differing | max |
|---|---|---|
| 120 km vs 300 km, whole frame, az000 | **0 / 980 480** | 0 |
| 120 km vs 300 km, whole frame, az180 | **0 / 980 480** | 0 |

Byte-identical, and the reason is in the table above: **the longest in-layer chord anywhere in the
sky is 104.41 km**, at elevation exactly 0. Any `MaxViewDistance` at or above that never binds on any
ray, so 120 km and 300 km and 400 km are one setting. **The slider's whole useful travel for a ground
camera under this layer ends at 104.41 km, and everything past it is dead range.** (350 km of
`TracingStartMaxDistance` is not the binding constraint either: the furthest entry in the sky is
167.22 km.)

### 2.4 The deck already reaches the horizon — measured

Mean |dL/dx| per row (the horizontal structure a cloud has and a smooth sky has not), az000, half-degree
buckets, base scene:

| elevation | +5.5° | +4.5° | +3.5° | +2.5° | +1.5° | +1.0° | +0.5° | **+0.0°** |
|---|---|---|---|---|---|---|---|---|
| structure | 0.00293 | 0.00302 | 0.00297 | 0.00313 | 0.00329 | 0.00250 | 0.00192 | **0.00168** |

There is **no gap**. Cloud structure runs continuously from 18° down to the horizon line, and at the
horizon line itself it is still **56 % of its mid-deck value**. The band the brief describes as "clear
blue between the lowest clouds and the horizon" is about **one degree** tall, is not clear (it holds
56 % of the deck's structure), and does not move when the cap is raised — at +0.0° the structure goes
0.00168 → 0.00191 with `MaxViewDistance` at 300 km, which is 13 % of a quantity that is not the
complaint.

**What that one degree actually is** is §3.

### Verdict

**REFUTED.** Raising `MaxViewDistance` is refused, and this is a recorded refusal in the sense of
DEV_CONTRACT §3 / verify §5b:

* it changes nothing above 2.79° of elevation (0.4 % of pixels, mean 0.0021/255);
* inside 2.79° it is worth one level of 255;
* past 120 km it is byte-identical and therefore dead range;
* and it would break a **tested, calibrated pair**.
  `Desert/Tests/Engine/ComponentReflection`'s
  `TheWeatherTileRepeatsNoMoreOftenToTheHorizonThanTheCalibratedSkyDid` bounds
  `MaxViewDistance / WeatherTileSize` at **5** — the number of times the coverage field repeats to the
  vanishing point. `CALIBRATION.md` §4 records the failure at 150 km against an 8 km tile (18.75
  repeats, "about twenty times", radial moiré) and the cure at 60/12 = five. **300 km against a 12 km
  tile is 25 repeats**, five times past the largest count that has ever been looked at and found clean.

**What would change the answer:** a camera above the deck, or a layer whose in-layer chord exceeds
60 km over a useful part of the sky — a thin, high sheet rather than a 3.6 km congestus deck. For a
ground camera under this layer, the parameter has 2.79° of authority and spends it on one level.

---

## 3. WHAT DOES CAUSE IT — and it is one field, in the opposite direction

The complaint is real; the sheet shows it. What produces it is not distance-to-the-horizon but
**distance-to-the-cloud**, and it is switched off.

### 3.1 The mechanism

`CloudRaymarch.shader:681-686`:

```glsl
float aerialAmount = 1.0f;
if (u_CloudFade.y > 0)
    aerialAmount = clamp((meanDistanceKm - u_CloudFade.x) / u_CloudFade.y, 0.0f, 1.0f);
vec3 hazed = aerial.rgb * cloudCoverage + aerial.a * luminance;
luminance  = mix(luminance, hazed, aerialAmount);
```

`u_CloudFade.xy` is `AerialPerspectiveStartDistance` / `AerialPerspectiveFadeDistance`. The
component's default for **both is 0** — the tooltip says so and names it: *"At 0 — the physical
answer, and UE's default"*.

**Every cloud scene in this repository overrides it to 30 km / 90 km.** All **34** of them, without a
single exception — `Clouds_Protocol`, `Clouds_Demo`, the `PR_Hero*` set, all five `SIL_*`, every `PT*`
knob scene. Not one scene carries the default.

At 30 km the consequence is geometric and is not written down anywhere:

| the deck at | slant range | `aerialAmount` |
|---|---|---|
| the zenith | 2.2 km | **0** |
| 25° | 5.2 km | **0** |
| 10° | 12.6 km | **0** |
| 4.07° | 30 km | 0 (the start) |
| 2.0° | 55.9 km | 0.29 |
| 0.5° | 120.7 km | 1.00 |

**Everything above about 4° of elevation carries no atmosphere at all** — and above 4° is 93 % of the
solid angle of the sky above the horizon. A cloud 25 km away and a cloud 2 km away are composited with
*identical* atmospheric depth: none. That is the depth cue that says "this is far, and therefore
high", and it has been turned off across the entire content library.

It also explains the one-degree pale band of §2.4 exactly: it is `aerialAmount` running 0 → 1 between
4.07° and 0.51°, which is the fade doing precisely what it was authored to do.

### 3.2 The knock-out

`AerialPerspectiveStartDistance 30 km → 0`, `FadeDistance 90 km → 0` — i.e. **the component's own
default**, and UE's. One field pair, nothing else touched.

| point | slant range at entry | pixels differing | max | **mean Δ/255** | contrast | **saturation** |
|---|---|---|---|---|---|---|
| **EL 02 away** | 55.9 km | 53.9 % | 35 | **3.61** | 0.664 → 0.656 | **0.146 → 0.203** |
| EL 02 sunward | 55.9 km | 51.2 % | 40 | 4.06 | 0.665 → 0.664 | 0.158 → 0.210 |
| **EL 25 away** | 5.2 km | 84.6 % | 29 | **3.25** | 0.391 → 0.383 | **0.145 → 0.204** |
| EL 25 sunward | 5.2 km | 90.1 % | 32 | 5.10 | 0.255 → 0.250 | 0.111 → 0.173 |
| **EL 65 away** | 2.4 km | 84.0 % | **9** | **1.40** | 0.487 → 0.483 | **0.147 → 0.173** |
| EL 65 sunward | 2.4 km | 89.2 % | 18 | 3.60 | 0.485 → 0.489 | 0.122 → 0.149 |

**The amplitude is monotone in elevation away from the sun — 3.61 / 3.25 / 1.40 as the deck comes
nearer — and the maximum falls 35 → 29 → 9.** That is the signature of a *distance-dependent* term
and is the whole argument: the change tracks the geometry rather than being a uniform tint. The base
frame has no such gradient because the term is off wherever the deck is nearer than 30 km.

**Saturation is where it lands hardest**, and this is a correction to a recorded attribution. Р0's §7
files the saturation deficit as *"mostly a coverage difference rather than a renderer one"* and gives
`Coverage` as its lever. Measured here, one field pair moves it **0.145 → 0.204** at EL 25 and
**0.146 → 0.203** at EL 02. The shipped frames measure **0.111–0.158** over these six points and the
knock-out puts them at **0.149–0.210**, which is the band `DIAGNOSIS_CARTOON.md` §0 records for the
reference (`UE_mid` 0.192, `UE_horizon` 0.286, `UE_zenith` 0.297). `Coverage` is *a* lever; it is not
the only one and it is not the first one.

**And the honest cost, stated because it moves the metric Р0 ranked first.** Contrast falls by
0.001–0.008 at five of the six points (it rises 0.004 at one), and the fine-scale structure falls
27–35 % at low elevation (0.00300 → 0.00196 at 2° of elevation, 0.00280 → 0.00204 at 4°). Distant
cloud that is genuinely hazed has genuinely less contrast and less texture; that is what haze is. It
is a real trade and not a free win.

**The GPU cost is nil, and structurally so.** The gate is `u_CloudAerial.z > 0.5 && aerialWeightSum >
0` — the aerial-perspective volume is fetched either way. `aerialAmount` is a `clamp` of two
subtractions that this change *removes* (`u_CloudFade.y > 0` becomes false and the constant 1 is
used). Measured rather than left to that reading, in §5.

### 3.3 The frames

`P8_ap0_mid_away.png` against `P8_base_mid_away.png`, both at `AZ 000 / EL 25`, and the two dome
sheets. In the base the near mass on the right and the distant mat on the lower left are the *same
white*. With the term at its default the distant mat is blue-grey and the near mass is still white, and
the deck acquires the front-to-back separation it does not otherwise have. At `EL 65` the same change
is worth 1.40/255 and max 9 — which is the frame that shows it did not break the near field.

---

## 4. THE TWO OTHER CANDIDATES — one refuted, one partly

### 4.1 The cell, which the brief asked about by name — REFUTED IN BOTH DIRECTIONS

> *"Today `WeatherTileSize`/thickness = 12/3.6 = 3.33. Check whether that is the relation and whether
> that is the right value."*

**It is not the relation, and the arithmetic in the brief compares the wrong pair.** The placement
lattice is `WeatherTileSize / 4` — stated once in `ECS/VolumetricCloudComponent.hpp:929-947`
(`CloudLayerLatticeKm`) and in the field's own comment (*"12 km -> 3 km cells"*). So the number that
governs how wide a cloud is against how thick the layer is:

> **cell 3.0 km ÷ thickness 3.6 km = 0.83**, not 3.33.

Which means the deck is authored **taller than it is wide** — the proportion the defect table calls
"cumulonimbus proportions", and the one the earlier fix was supposed to have corrected. That is exactly
the shape of a partial fix, and it is why I tested it rather than reasoning about it.

**Tested, and the answer is no.** `WeatherTileSize 12 km → 24 km` (cell 3 → 6 km, ratio 0.83 → 1.67,
repeats 60/24 = 2.5 so the calibrated bound still holds) is the **mirror of the recorded refutation**
— shrinking the cell is already known to make the complaint worse, and enlarging it had never been
tried.

| point | mean Δ/255 | max | contrast | p05 | saturation |
|---|---|---|---|---|---|
| EL 25 away | 19.07 | 114 | **0.391 → 0.283** | 0.327 → **0.439** | 0.145 → 0.120 |
| EL 25 sunward | 10.56 | 86 | **0.255 → 0.087** | 0.476 → 0.513 | 0.111 → 0.115 |
| EL 65 away | 32.82 | 154 | 0.487 → 0.491 | 0.256 → 0.248 | 0.147 → 0.251 |
| EL 65 sunward | 21.81 | 152 | 0.485 → 0.313 | 0.293 → 0.280 | 0.122 → 0.255 |

Contrast collapses at three of the four points and the dark end is destroyed at EL 25 away
(p05 0.327 → 0.439). The frame says the same thing louder than the numbers:
`cell24_az000_el25` is a single featureless grey mass filling two thirds of the sky with a handful of
discrete lumps under it — **more** like a low overcast, not less.

> **Both directions from 3 km are worse.** Shrinking the cell makes "clouds too low" worse (recorded,
> verify §5); enlarging it destroys the contrast. The shipped 3 km cell is at a local optimum on this
> axis and the cell is not the mechanism. **The 0.83 ratio is real and is not the lever** — which is
> worth writing down precisely because the arithmetic is compelling and the next person will otherwise
> derive it, believe it, and spend a phase on `WeatherTileSize`.

### 4.2 The sky-occlusion volume — built, measured, and switched on by NO scene

`SkyOcclusionVolume` exists (`Programs/Clouds/CloudSkyOcclusionVolume.shader`, built by Р4 to answer
Р0's ranked #1), defaults to `false`, and **not one scene in `Editor/Resources/Assets/Scenes` sets it
`true`.** Six scenes even mention the field; all six write `false`.

| point | mean Δ/255 | bias | contrast | p05 |
|---|---|---|---|---|
| EL 25 away | 9.36 | **−12.03** | 0.391 → 0.376 | 0.327 → 0.327 |
| **EL 25 sunward** | 10.67 | −13.75 | **0.255 → 0.288** | **0.476 → 0.431** |
| EL 65 away | 6.40 | −8.17 | 0.487 → 0.471 | 0.256 → 0.256 |
| EL 65 sunward | 8.22 | −10.57 | 0.485 → 0.470 | 0.293 → 0.293 |

It is a large, strongly negative, very coherent term (coherence 33–46): it darkens the deck's
undersides, which the frame confirms — the big mass at `EL 25 away` gains a shaded grey base it does
not otherwise have. **And it is the only thing measured in this task that puts a dark end into a
sunward frame**: p05 0.476 → 0.431 and contrast +0.033 at `EL 25 sunward`, the point Р0 records as
having "no shadow in the frame at all".

But it lowers contrast at the other three points and its bias is a near-uniform darkening, so it is
**not a clean win and it is not free** — it is a dispatch. It is recorded here as a candidate with
its numbers rather than proposed, for two reasons: its cost was not measured in this task, and
`Common/CloudLighting.glslh` and `Graphic/Systems/Scene/Clouds/*` are Р7's while a colour cast is
being chased there.

---

## 5. THE COST — the pass's own GPU line, and one more of the brief's claims that is false

> *"Raising `MaxViewDistance` costs march time — measure the pass's own GPU line, never a whole-frame
> delta."*

Measured. `--gpu-profile --shot-frames 300`, `--look 0,0.0349,-0.9994` (elevation 2° away, which is the
**longest march anywhere in the sky** and therefore the fair worst case for this parameter). The three
scenes were **interleaved in one session** and the machine was otherwise idle; the number is the
`VolumetricClouds` scope's own `gpu ms` column, minimum of three, with the spread.

| scene | r1 | r2 | r3 | **min gpu ms** | spread |
|---|---|---|---|---|---|
| base — `MaxViewDistance` 60 km | 8.211 | 8.136 | 8.371 | **8.136** | 0.235 |
| `MaxViewDistance` 300 km | 7.472 | 7.829 | 7.629 | **7.472** | 0.357 |
| `AerialPerspective{Start,Fade}` = 0 | 8.271 | 8.220 | 8.275 | **8.220** | 0.055 |

**Raising `MaxViewDistance` five-fold is FASTER, not slower — by 0.66 ms, and every one of its three
runs beats every one of the base's.** The brief's premise that it costs march time is false for this
layer, and the mechanism is in the step schedule rather than in the geometry: `CloudStepCount` caps at
`MaxSteps` for any segment past `CLOUD_DISTANCE_TO_MAX_STEPS_KM` = 4 km, so both budgets spend **256
steps**; a longer permitted segment therefore spends them over a longer chord, takes coarser strides
through empty sky and reaches `StopTransmittance` sooner inside cloud. **Cost is not the reason to
refuse it. §2 is.** This is worth recording precisely because "further must be dearer" is the kind of
arithmetic that is compelling, wrong, and never checked.

**The aerial-perspective change is not resolvable.** 8.220 against 8.136 is **+0.084 ms (+1.0 %)**,
against a base spread of **0.235 ms** — a third of the noise. The reading in §3.2 said it should be
nil, because the volume is fetched either way and the change removes a `clamp`; the measurement
neither confirms nor contradicts a difference that small, and the honest statement is that at this
instrument's resolution it is free.

---

## 6. THE ANSWER

**Was the fix incomplete, or are there two mechanisms?** Neither, in the terms the brief offers.

* The earlier fix — cell width against layer thickness — is **not partial and not the axis**. The cell
  is at a local optimum: both directions from 3 km measure worse (§4.1).
* The brief's own hypothesis — the deck cut short of the horizon — is **not a mechanism at all**. The
  deck reaches the horizon; its structure at the horizon line is 56 % of its mid-deck value and the
  cap has 2.79° of authority worth one level (§2).
* What is left is a **third mechanism that has never been part of this complaint's history**: the
  atmosphere between the eye and the cloud is switched off for 93 % of the sky, by an authored value
  present in every cloud scene in the repository and absent from the component's default (§3).

So: **one mechanism, and it is not the one that was looked for twice.** "Feels low, not up in the
atmosphere" is a literal description — there is no atmosphere between the camera and the deck.

### Proposed, not done

Changing shipped scene values is a decision. **Proposal:** set
`AerialPerspectiveStartDistance = 0` and `AerialPerspectiveFadeDistance = 0` in the 34 cloud scenes,
which is the component's own default and UE's, at a GPU cost below this instrument's resolution
(§5: +0.084 ms against a 0.235 ms spread). Price, honestly stated: contrast falls 0.001–0.008 and
fine-scale structure 27–35 % at low elevation, in exchange for saturation moving from 0.111–0.158 into
the reference band at 0.149–0.210 and for a front-to-back depth ladder the deck does not otherwise
have. `P8_dome_base.png` and `P8_dome_ap0.png` are the two skies side by side; the difference is in
rows EL 05 and EL 25 and rows EL 45–85 are all but unchanged, which is the "what it could have broken"
frame as well as the "what it fixed" one.

**Why the override exists is worth preserving in the decision.** The tooltip says it: at 0, *"ninety
kilometres of air erases a cloud on the horizon completely, which is correct and is not always what a
sky is wanted to look like. Pushing it out keeps the distant band visible."* The override solved a
horizon-band problem and paid for it with the depth cue in the other 93 % of the sky. If the distant
band is wanted back, that is a request for a *non-physical* horizon and it should be spelt as one,
rather than as a start distance that silently disables the term everywhere else.

---

## 7. THE FRAMES, AND WHAT WAS THROWN AWAY

All under `Docs/Clouds/Shots/`. `Clouds_Protocol`, camera `0,200,0`, `--play`, 1280×766, Debug,
`--shot-frames 90`.

| frame | what it is for |
|---|---|
| `P8_dome_base.png` | **the deliverable** — the whole dome as shipped, 40 tiles, 8 azimuths × 5 elevations |
| `P8_dome_ap0.png` | the same dome with the aerial perspective at its default; the answer, on the same instrument as the question |
| `P8_mvd060_el02_away.png` | the base at 2° of elevation, the band where `MaxViewDistance` can act |
| `P8_mvd300_el02_away.png` | the same with the cap raised five-fold — §2's refutation, and the two are one level apart |
| `P8_base_mid_away.png` | `AZ 000 / EL 25` as shipped: near mass and distant mat the same white |
| `P8_ap0_mid_away.png` | the same with the term at its default: the distant mat blue, the near mass still white |
| `P8_cell24_mid_away.png` | the cell doubled — §4.1's refutation, a featureless grey mass over two thirds of the sky |

**Deliberately not committed**, and named so nobody looks for them: the **78 full-size tiles** of the
two dome sweeps (≈ 92 MB — the sheets are what the tiles are *for*), the **20 knock-out frames** of
§3–§4 at other points (≈ 24 MB), the **9 timing captures** of §5, and the three temporary `.desce`
variants, which were deleted by the scripts that made them. `Docs/Clouds/Shots` is 508 MB of a 5.6 GB
repository; this task adds **10 MB**.

Seven throwaway instruments and drivers were written for this task and are **not** committed, because
they are experiments rather than tools: the shell geometry calculator, the row-structure profiler
(mean |dL/dx| per row against elevation), the scene-variant writer, and four drivers. What each
measures is stated where it is used, and the one that carries load — the row → elevation map — is
validated against an independent landmark in §0 so the measurement can be repeated without the script.

---

## 8. WHAT I COULD NOT MEASURE

* **Any comparison against Unreal.** `Docs/Clouds/UEReference/` holds only its README in this tree;
  the PNGs are git-ignored and are not here. Every reference figure quoted above is quoted from
  `DIAGNOSIS_CARTOON.md` §0 rather than re-measured, and is labelled as such.
* **The cost of the sky-occlusion volume.** §4.2 gives its amplitude and not its price, so it is a
  candidate and not a proposal.
* **A human at the controls.** Everything is headless `--shot` under a fixed camera. Aerial
  perspective is a term whose weight moves with the camera, and a translation under the deck would
  exercise the temporal resolve's response to it in a way a still cannot.
* **Whether 0/0 is right for every scene.** It was measured on `Clouds_Protocol` only — one genus, one
  coverage, one sun angle. The thin high sheets (`SIL_Cirrus`, `SIL_Altocumulus`) sit further away and
  will take more of this term, not less.
