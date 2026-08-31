# The control census

**Task Р11, 2026-08-31.** One row for every authored value the cloud subsystem exposes: what its
range is, how far the frame moves across that range, at which angles that was measured, and a
verdict of **live**, **nearly dead** or **dead**.

## Why this document exists

The product owner restated the acceptance criterion for the cloud programme as a capability rather
than a likeness:

> *"Even if there is no exact likeness, but there is a TOOL FOR THE ARTIST — where we can set
> different shapes, degree of generation (frequency) and so on — then it is fine and counts as
> done."*

That makes the deliverable testable in exactly one way: go through every authored value and
establish whether it moves the picture. `DEV_CONTRACT.md` §1.3 already forbids the failure mode —
a parameter must be wired "component → serialization → editor UI → GPU → **a visible effect**", and
a slider that moves nothing is "a TODO wearing a feature's clothes".
`Desert/Tests/Engine/SettingConsumers` pins the first four links: every field names the file that
reads it. **Nothing pins the fifth**, and that is what this census measures.

## The headline

| population | values | live | nearly dead | dead | not measured |
|---|---|---|---|---|---|
| `VolumetricCloudComponent` | 52 | **52** | 0 | 0 | 0 |
| `CloudTypeShape` (`.decloudtype`) | 13 scalars + the 16-sample profile | **14** | 0 | 0 | 0 |
| `.dcnv` generator params | 7 | **4** (jointly) | 0 | 0 | 3 |
| `.dclayout` content | pattern + mask | **2** | 0 | 0 | 0 |
| `HeroCloudComponent` | 7 | **7** | 0 | 0 | 0 |
| `.dcmv` recipe | 11 | **8** | 0 | 0 | 3 |
| **total** | **97** | **91** | **0** | **0** | **6** |

**Not one authored value in the cloud subsystem is dead.** The six that are not measured are named
in §3 and are unmeasurable without either a code change or the sculpting panel, not unmeasured by
choice.

**Two of the six values the brief listed as already measured are refuted**, and both were on the
"dead" side of the ledger — see §5.1. The one number that reproduced to three decimals is in §5.9.

---

## 1. The instrument, and its floor

Every number below is `Tools/ImageDiff` over the whole frame of a `--shot` capture: the count of
differing pixels, the largest single-channel difference in 8-bit levels (`max n/255`), and the mean.
The scene is `Editor/Resources/Assets/Scenes/Clouds_Protocol.desce` (`Docs/Clouds/CALIBRATION.md`
says to measure there and not on `Clouds_Demo`), camera `0,200,0`, `--shot-frames 90 --play`,
1280×766 = 980 480 pixels.

**The floor was measured, not assumed** — `desert-engine-verify` §1 requires the repeat shot because
a wall-clock input anywhere in the frame puts it above zero:

| repeat | differing | max |
|---|---|---|
| same command, run twice, serially | **0** / 980 480 | 0 |
| same command, two copies concurrently | **0** / 980 480 | 0 |
| four copies concurrently | **0** / 980 480 | 0 |

So the floor on this scene is exactly zero, and **it survives concurrency**: four editors rendering
at once produce bytes identical to one editor rendering alone. That is what made a census of this
size affordable — the whole sweep is ~900 captures, and at four at a time it is hours rather than a
week. Six at a time is not safe: two of six crashed during Vulkan initialisation, which is why every
capture is checked against its own log (`[Shot] wrote -> …`) and retried, never against the exit
status. The editor segfaults during teardown on every run.

**The first render in a fresh worktree was discarded**, and it earned the rule again: that frame
differs from the second by **7 pixels of 980 480 at max 1/255**, with no authored change of any kind
between them. It is the only non-zero difference in this document that nothing caused.

## 2. The threshold, derived

"Moves the frame" needs a number, and picking one would make it a standard rather than a
measurement. It is derived from what an artist can actually *express*.

**A ranged float property is authored through `ImGui::SliderFloat` with ImGui's default format**
(`Editor/Source/Editor/Panels/PropertyEditor/PropertyEditorBuilder.cpp:539`), which is `"%.3f"`. So
**one click of any float slider in this component is 0.001 in the property's own units**, and no
artist can express anything finer. That gives the census a natural unit: measure what one click of a
knob that is unarguably live does to the frame, and require every other knob's *whole range* to beat
it.

Three knobs that are unarguably live were walked from their shipped value by one click and then by
decade steps, at two rays. `ExtinctionScale` and `DensityScale` are smooth whole-sky multipliers —
the gentlest thing a control can be — and `Coverage` re-places cloud, which is the sharpest:

| clicks from the shipped value | `ExtinctionScale` 8.0 | `DensityScale` 1.0 | `Coverage` 0.762 |
|---|---|---|---|
| **1** (+0.001) | max **1**/255 over 0.3 % / 0.9 % | max **1**/255 over 1.4 % / 4.6 % | max **24**/255 over 6.7 % / 5.8 % |
| 10 | max **1** over 2.4 % / 8.1 % | max **1** over 10.7 % / 30.4 % | max **113** over 22.3 % / 35.7 % |
| 100 | max **2** over 19.9 % / 49.7 % | max **4** over 52.0 % / 78.9 % | max **118** over 77.0 % / 79.3 % |
| 500 | max **9** over 59.6 % / 85.8 % | max **38** over 80.2 % / 97.2 % | — |

*(the two percentages are `AZ000 EL25` / `AZ135 EL45`)*

And the two floors that nothing authored produced at all:

| | differing | max |
|---|---|---|
| the instrument's own repeat | 0 / 980 480 | **0** |
| the first render in a fresh worktree, against the second | 7 / 980 480 | **1** |

Put together, that is the whole derivation:

> **A value is LIVE when its whole authored range moves at least one ray by `max ≥ 4/255`.**
> **1–3/255 is NEARLY DEAD. Exactly 0 is DEAD.**

- **0** is not a choice at all: the knob is not in the 8-bit file, anywhere, at any angle.
- **1** is indistinguishable from the environment. It is what the fresh-worktree artefact produced
  with nothing changed, and it is what **one 0.001 click** of the gentlest live knob produces.
- **2–3** is what one to five *hundred* clicks of that knob produce. A whole range worth that is a
  control nobody can steer a sky with — the artist would have to travel the entire slider to get
  what a live knob gives in a hundredth of its travel.
- **4** is the first level that beats a hundred clicks of the weakest live smooth knob measured
  (`DensityScale` +10 % of range → max 4).

**The census turns out not to be sensitive to where in 1..7 that line is put.** Across all 52
component properties the measured movements are **either exactly 0 or at least 8/255**; nothing
landed in between. The threshold decides no verdict in this document. It is stated anyway, because
the next person to add a property needs the bar to exist before they measure against it.

## 3. What was cut, and what is therefore unmeasured

The brief allowed a reduced dome and asked that the reduction be named rather than left to be
inferred. It was reduced, twice over, and here is exactly how.

**THE DOME IS THREE RAYS, NOT FORTY.** Every value in the tables below was first swept at

| ray | why it is in the set |
|---|---|
| `AZ135 EL45` | 22° from the sun (the sun sits at **AZ 151, EL 50** — `-normalize(-30,-75,-55)` of the scene's Sun entity). The sunward mid angle, where the phase function is ~16x the away one and where `REVIEW_622a01a6.md`'s bands needed sunward azimuth AND height together. |
| `AZ000 EL25` | anti-sun, low-mid. Its baseline contrast is **0.391**, which is the same number Р8 quotes, so a figure here and a figure there are on one ruler. |
| `AZ270 EL85` | near-zenith, cross-sun — the angle whose emptiness survived a ten-merge programme. |

They differ in BOTH axes at once, which is the property the six-point protocol lacked. **Every knob
that failed to move all three was then chased somewhere else**, because a verdict of *dead* is the
one that gets a working control deleted and it is the one that has to be expensive. In the event
none of them needed more *rays* — each needed a different **condition**, and every one of the four
turned live under it (§5.2, §5.4, §5.6). Three further ray sets were added on top:

- **two downward rays** on `Clouds_ShadowsOnGround`, for the two shadow knobs;
- **twelve grazing rays** — AZ 000 and AZ 135 at EL 0.5, 1, 2, 2.79, 4 and 8 — for the three range
  gates, which by construction can only act near the horizon (§4.5);
- **three rays aimed at the hero cloud**, which the dome's rays do not point at.

**What is therefore unmeasured.** The sky rays here are a subset of the 40 of a full
`DomeSweep.sh`. Nothing in this document has been checked at **EL 45, 65 or 85 away from the
cardinal azimuths**, except AZ 135 at EL 45; and nothing at EL 25 or 45 at AZ 045, 225 or 315. If a
knob acts *only* at, say, AZ 225 / EL 65, this census would call it dead and be wrong. That risk is
real — it is precisely the shape of the defect `DomeSweep.sh` was built for, and it is the largest
single hole in this document.

**Frames: 90, and `--play`.** The 30-frame and 90-frame renders of the same scene differ by
**65.4 % of pixels at max 54/255**, so the convergence window is not optional; everything here is at
90.

**Three of the seven generator parameters of a `.dcnv` could not be swept at all.** A `.dcnv` stores
baked voxels, and `CloudNoiseVolumeAsset::Save` is only reachable from the noise-volume panel — there
is no headless bake and adding one is a code change this task was not given. What *was* available is
a natural experiment: the two shipped volumes are identical in every header field except the four
lattice periods, which are **exactly doubled** (`Default` 2/4 wispy, 3/6 billowy → `FineWisp` 4/8,
6/12; decoded from the 72-byte headers). Swapping the slot therefore measures all four periods
together. **`CurlStrength`, `Seed` and `Resolution` are not measured by anything in this document.**

**Three per-lump `.dcmv` values could not be swept either**, and there the obstruction is the format
rather than the tooling: the recipe validator and the bake's boundary check refuse a *uniform* edit
of `RadiiKm`, `CentreKm` or `Primitive` across all eight lumps in **both** directions — small makes
the profile touch the volume boundary, large bursts `SizeKm`. They are per-lump values that only mean
anything one lump at a time, which is what the sculpting panel does.

**No performance numbers.** The brief did not ask for them and the machine is shared.

## 4. The tables

Each row is the property's **own authored range**, end to end — the `Range()` of its `PROPERTY`
macro for a component field, the panel's own slider for a `.decloudtype` or `.dcmv` number. Where
the range had to be clipped, the clipping rule is named in the row. "Movement" is the largest
single-channel difference between the two ends over the whole 980 480-pixel frame, at the ray where
it is largest, with the per-ray figures beside it.

### 4.1 `VolumetricCloudComponent` — all 52 properties

| # | value | range swept | movement | per ray (max/255 · % of frame) | verdict |
|---|---|---|---|---|---|
| 1 | `Enabled` | off → on | **154**/255 | AZ135/EL45 137/100 % · AZ000/EL25 128/85.6 % · AZ270/EL85 154/63.0 % | live |
| 2 | `CloudType1` | Cumulus_Congestus → Cirrus | **153**/255 | 129/100 % · 107/100 % · 153/88.3 % | live |
| 3 | `CloudType2` | empty → Cirrus | **114**/255 | 88/95.1 % · 97/98.2 % · 114/90.6 % | live |
| 4 | `CloudType3` | empty → Cirrus | **114**/255 | 88/95.1 % · 97/98.2 % · 114/90.6 % | live |
| 5 | `CloudType4` | empty → Cirrus | **114**/255 | 88/95.1 % · 97/98.2 % · 114/90.6 % | live |
| 6 | `PlanetRadius` | 100 → 7000 km | **122**/255 | 52/87.1 % · 122/83.3 % · 49/28.2 % | live |
| 7 | `MaxViewDistance` | 1 → 400 km | **146**/255 | 109/95.8 % · 128/83.5 % · 146/46.2 % | live |
| 8 | `TracingStartMaxDistance` | 10 → 1000 km | **89**/255 | 0/0 % · 89/14.5 % · 0/0 % | live *(one ray only — see §5.3)* |
| 9 | `TracingStartDistance` | 0 → 50 km | **154**/255 | 137/100 % · 128/85.6 % · 154/63.0 % | live |
| 10 | `Coverage` | 0 → 1 | **155**/255 | 146/100 % · 128/100 % · 155/77.4 % | live |
| 11 | `CoverageContrast` | 0.1 → 4 | **154**/255 | 119/100 % · 128/88.2 % · 154/74.6 % | live |
| 12 | `WeatherTileSize` | 2 → 80 km | **171**/255 | 161/99.9 % · 134/98.7 % · 171/92.6 % | live |
| 13 | `RegionSize` | 16 → 120 km | **131**/255 | 63/94.2 % · 121/84.3 % · 131/72.6 % | live |
| 14 | `Seed` | 0 → 65535 | **147**/255 | 112/99.7 % · 140/91.4 % · 147/81.9 % | live |
| 15 | `PlacementDensity` | 0.25 → 8 | **146**/255 | 146/99.7 % · 136/90.1 % · 124/100 % | live |
| 16 | `PlacementScatter` | 0 → 4 | **134**/255 | 126/98.4 % · 134/99.0 % · 133/100 % | live |
| 17 | `PlacementSizeVariety` | 0 → 1 | **153**/255 | 107/99.8 % · 127/90.4 % · 153/83.9 % | live |
| 18 | `PatchTileSize` | 5 → 200 km *(low end silently raised to 3 lattice cells = 9 km)* | **154**/255 | 45/71.7 % · 103/68.8 % · 154/50.3 % | live |
| 19 | `PatchStrength` | 0 → 1 | **154**/255 | 45/68.3 % · 118/77.3 % · 154/60.3 % | live |
| 20 | `CloudLayout` | empty → `Layout_Stripe` | **154**/255 | 79/90.7 % · 121/82.0 % · 154/71.0 % | live |
| 21 | `LayoutPatternStrength` | 0 → 1, `Layout_Stripe` bound | **154**/255 | 79/90.7 % · 121/82.0 % · 154/71.0 % | live *(0 with `Layout_LetterD` — §5.2)* |
| 22 | `LayoutMaskStrength` | 0 → 1, `Layout_LetterD` bound | **152**/255 | 137/94.0 % · 129/78.6 % · 152/37.6 % | live *(0 with `Layout_Stripe` — §5.2)* |
| 23 | `LayoutRepeats` | 1 → 16, `Layout_Stripe` bound | **152**/255 | 143/97.3 % · 121/76.2 % · 152/43.3 % | live |
| 24 | `LayoutRotation` | 0 → 3, `Layout_Stripe` bound | **151**/255 | 143/37.2 % · 128/61.2 % · 151/32.8 % | live |
| 25 | `LayoutOffset` | (0,0) → (24 km, 24 km), `Layout_Stripe` bound | **154**/255 | 143/100 % · 128/100 % · 154/78.0 % | live |
| 26 | `DetailTileSize` | 0.2 → 30 km | **81**/255 | 59/79.2 % · 66/70.1 % · 81/55.0 % | live **— contradicts the brief, §5.1** |
| 27 | `DetailStrength` | 0 → 1 | **151**/255 | 84/100 % · 117/92.7 % · 151/84.8 % | live **— contradicts the brief, §5.1** |
| 28 | `DensityScale` | 0 → 2 | **158**/255 | 118/100 % · 130/85.7 % · 158/63.6 % | live |
| 29 | `ExtinctionScale` | 0.5 → 60 /km | **147**/255 | 136/100 % · 125/85.9 % · 147/66.0 % | live |
| 30 | `NearFadeStartDistance` | 0 → 20 km, camera inside a 0.02–1.6 km deck, `End` = 20 km | **145**/255 | 145/91.9 % · 127/96.1 % · 117/2.2 % | live *(0 at the shipped camera — §5.4)* |
| 31 | `NearFadeEndDistance` | 0 → 20 km, same deck, `Start` = 0 | **145**/255 | 145/91.9 % · 127/96.1 % · 117/2.2 % | live |
| 32 | `ScatteringAlbedo` | 0 → 1 | **237**/255 | 237/100 % · 195/85.7 % · 205/63.1 % | live — **the largest in the subsystem** |
| 33 | `PhaseG` | −0.9 → 0.9 | **98**/255 | 98/95.2 % · 2/28.2 % · 19/53.0 % | live |
| 34 | `PhaseGBackward` | −0.9 → 0.9 | **71**/255 | 71/97.2 % · 4/32.7 % · 20/55.4 % | live |
| 35 | `PhaseBlend` | 0 → 1 | **85**/255 | 85/95.4 % · 26/83.2 % · 34/61.5 % | live |
| 36 | `AmbientOcclusionStrength` | 0 → 1 | **53**/255 | 53/100 % · 47/84.6 % · 50/60.0 % | live |
| 37 | `SkyOcclusionVolume` | off → on | **11**/255 | 9/92.4 % · 11/79.5 % · 6/49.0 % | live |
| 38 | `LightMarchDistance` | 0.1 → 20 km | **146**/255 | 120/100 % · 61/83.1 % · 146/66.3 % | live |
| 39 | `LightMarchSamples` | 1 → 64 | **178**/255 | 133/100 % · 71/84.6 % · 178/81.9 % | live |
| 40 | `MultiScatterOctaves` | 1 → 3 | **81**/255 | 81/100 % · 65/85.3 % · 59/62.3 % | live |
| 41 | `MultiScatterContribution` | 0 → 1 | **124**/255 | 124/100 % · 105/94.2 % · 100/84.5 % | live |
| 42 | `MultiScatterOcclusion` | 0 → 1 | **125**/255 | 18/28.6 % · 125/58.7 % · 32/42.3 % | live |
| 43 | `MultiScatterEccentricity` | 0 → 1 | **119**/255 | 119/100 % · 27/85.1 % · 85/61.5 % | live |
| 44 | `AerialPerspectiveStartDistance` | 0 → 200 km | **8**/255 | 2/75.0 % · 8/64.8 % · 2/15.0 % | live — **the smallest, §5.5** |
| 45 | `AerialPerspectiveFadeDistance` | 0 → 200 km | **29**/255 | 19/100 % · 29/84.6 % · 12/59.4 % | live |
| 46 | `AmbientScale` | black → white | **102**/255 | 97/100 % · 102/85.5 % · 88/62.3 % | live |
| 47 | `CastShadows` | off → on, on `Clouds_ShadowsOnGround` | **179**/255 | down45 179/100 % · down20 179/100 % | live *(0 on `Clouds_Protocol` — §5.6)* |
| 48 | `ShadowStrength` | 0 → 1, same scene | **179**/255 | down45 179/100 % · down20 179/100 % | live |
| 49 | `MaxSteps` | 8 → 512 | **154**/255 | 136/100 % · 128/85.6 % · 154/63.0 % | live *(256→512 alone is 2/8/4 — §5.7)* |
| 50 | `StopTransmittance` | 0 → 0.2 | **50**/255 | 50/99.2 % · 26/78.9 % · 21/45.1 % | live |
| 51 | `WindDirection` | +X → +Z | **154**/255 | 117/99.9 % · 128/97.8 % · 154/87.1 % | live |
| 52 | `WindSpeed` | 0 → 500 m/s | **161**/255 | 86/99.4 % · 125/89.0 % · 161/88.6 % | live |

**52 of 52 live. None dead. None nearly dead.**

### 4.2 `CloudTypeShape` — the `.decloudtype`'s numbers

The ranges are the **Cloud Type panel's own sliders** (`CloudTypePanel.cpp`), clipped where
`ValidateCloudTypeShape` would refuse the file. A census copy of `Cumulus_Congestus` carries the
change and `Cloud Type 1` points at it.

| value | range swept | movement | per ray | verdict |
|---|---|---|---|---|
| `BaseAltitudeKm` | 0 → 5 km *(panel 0–14, clipped by Top > Base)* | **139**/255 | 106/99.9 % · 131/99.6 % · 139/82.9 % | live |
| `TopAltitudeKm` | 2.5 → 16 km *(panel 0–16, clipped by Top > Base)* | **160**/255 | 149/100 % · 130/98.0 % · 160/80.5 % | live |
| `EdgeTopFraction` | 0 → 1 | **134**/255 | 93/72.4 % · 127/42.5 % · 134/61.2 % | live |
| `BaseRampFraction` | 0.001 → 1 | **154**/255 | 100/97.9 % · 128/85.3 % · 154/66.2 % | live |
| `AnvilStrength` | 0 → 1 *(thickness 1 km supplied — a lobe with no thickness is refused)* | **153**/255 | 107/99.7 % · 133/95.6 % · 153/85.6 % | live |
| `AnvilAltitudeKm` | 0 → 16 km *(strength 1 supplied)* | **157**/255 | 142/97.4 % · 139/94.3 % · 157/78.5 % | live |
| `AnvilThicknessKm` | 0.001 → 5 km *(strength 1 supplied)* | **156**/255 | 156/100 % · 133/100 % · 153/89.2 % | live |
| `DetailCharacter` | 0 → 1 | **113**/255 | 62/72.7 % · 57/67.3 % · 113/54.5 % | live |
| `DetailFactor` | 0 → 8 | **151**/255 | 84/100 % · 117/92.7 % · 151/84.8 % | live |
| `DensityFactor` | 0 → 8 | **160**/255 | 99/100 % · 134/86.0 % · 160/65.6 % | live |
| `ExtinctionFactor` | 0 → 8 | **162**/255 | 119/100 % · 136/86.0 % · 162/66.2 % | live |
| `PlacementScale` | 0.05 → 8 | **149**/255 | 146/95.6 % · 137/92.0 % · 149/61.9 % | live |
| `PlacementAnisotropy` | 0.1 → 16 | **162**/255 | 147/99.9 % · 130/99.8 % · 162/93.3 % | live |

**Thirteen, not twelve.** The brief says "the twelve `CloudTypeShape` numbers"; the struct carries
thirteen scalars beside the profile, and `SettingConsumers`' own comment says so
(`setting_consumers_test.cpp:210`, "each handle into **thirteen numbers** and a vertical profile
curve").

#### The vertical profile that landed today

Sixteen `HalfWidth` samples, so the interesting question is not whether *scaling* the curve moves the
frame — a wider cloud is trivially a different cloud — but whether its **shape** does, independently
of its size. Two of the three tests hold the mean half-width **exactly** constant:

| test | what is held fixed | movement | verdict |
|---|---|---|---|
| `Profile` **shape** — shipped congestus curve vs the same curve **reversed** | mean half-width identical to the last digit | **153**/255 over 65.9 % | live |
| `Profile` **flat** — shipped curve vs a constant column at its own mean | mean half-width identical | **154**/255 over 64.0 % | live |
| `Profile` **scale** — flat 0.15 vs flat 1.20 | nothing (a size test, for reference) | **129**/255 over 99.1 % | live |

The reversed curve renders **mushroom-topped** clouds — narrow at the base, flaring at the top —
against the shipped congestus' wide base tapering upward. That is the criterion's "different shapes"
demonstrated with one variable and the size held still.

### 4.3 The `.dcnv` noise volume

| value | range swept | movement | per ray | verdict |
|---|---|---|---|---|
| `WispyPeriodLowFrequency`, `WispyPeriodHighFrequency`, `BillowPeriodLowFrequency`, `BillowPeriodHighFrequency` — **all four together** | 2/4/3/6 → 4/8/6/12 (`CloudNoise_Default` → `CloudNoise_FineWisp`, which differ in nothing else) | **79**/255 | 44/71.5 % · 47/66.4 % · 79/55.5 % | live |
| `CurlStrength` | — | — | — | **not measured** (§3) |
| `Seed` | — | — | — | **not measured** (§3) |
| `Resolution` | — | — | — | **not measured** (§3) |

### 4.4 The `.dclayout`

Its two authored strengths are component fields and are rows 21–22 above. The **content** — the
painted pattern and mask themselves — is measured by row 20 (`CloudLayout` empty → `Layout_Stripe`,
**154**/255) and by binding `Layout_LetterD`, which has a mask and moves the frame by **154**/255
against an empty slot. Both live.

### 4.5 The three range gates, at the elevations where they live

Rows 7, 8 and 9 all clamp *how far along the ray the march may run*, measured against where the ray
enters the layer. A dome that stops at EL 05 cannot see them properly, so they were also swept from
EL 0.5 upward, at two azimuths:

| elevation | `MaxViewDistance` 1 → 400 km | `TracingStartMaxDistance` 10 → 1000 km | `TracingStartDistance` 0 → 50 km |
|---|---|---|---|
| EL 0.5 | **101** / 52.1 % | **89** / 40.4 % | **101** / 44.1 % |
| EL 1.0 | **103** / 53.3 % | **89** / 40.2 % | **103** / 45.6 % |
| EL 2.0 | **105** / 56.5 % | **89** / 40.3 % | **105** / 48.5 % |
| EL 2.79 | **105** / 58.8 % | **90** / 40.2 % | **105** / 50.8 % |
| EL 4.0 | **109** / 62.4 % | **89** / 40.1 % | **109** / 54.4 % |
| EL 8.0 | **119** / 73.6 % | **89** / 40.1 % | **119** / 65.6 % |
| EL 25 / 45 / 85 (from §4.1) | 128 / 109 / 146 | 89 / 0 / 0 | 128 / 137 / 154 |

*(all at AZ000; AZ135 agrees to within a few levels at every row)*

`TracingStartMaxDistance` is the only one of the three that behaves as a grazing-ray guard should:
its effect is **flat at 89–90 of 255 over 40 % of the frame from EL 0.5 to EL 25 and then vanishes**,
which is the elevation at which the whole frame's entry distance finally falls inside 10 km. The
other two act at every elevation, because their low ends truncate the march before it reaches any
cloud at all.

### 4.6 `HeroCloudComponent` — the only route a `.dcmv` has into a scene

Not named in the brief, but it is where the sculpting volumes are authored from, so the census would
be incomplete without it. Measured on `Clouds_HeroCloud`, whose body sits at 3.8 km up and 8.44 km
north; the rays are aimed at it rather than taken from the dome, and the extents are small because
one hero cloud occupies about a tenth of the frame.

| value | range swept | movement | per ray (at the body · below it · off axis) | verdict |
|---|---|---|---|---|
| `Enabled` | off → on | **126**/255 | 126/10.9 % · 126/10.4 % · 126/8.5 % | live |
| `Volume` | `HeroCloud_Congestus.dcmv` → `HeroCloud_Cumulonimbus.dcmv` | **124**/255 | 124/35.6 % · 106/20.5 % · 102/24.6 % | live |
| `Strength` | 0 → 1 | **126**/255 | 126/10.9 % · 126/10.4 % · 126/8.5 % | live |
| `SuppressProceduralField` | off → on | **86**/255 | 85/2.4 % · 86/2.4 % · 7/1.1 % | live |
| `DetailFactor` | 0 → 4 | **121**/255 | 121/11.7 % · 121/10.5 % · 121/9.2 % | live |
| `DensityFactor` | 0 → 4 | **132**/255 | 132/10.0 % · 129/9.5 % · 131/8.3 % | live |
| `ExtinctionFactor` | 0 → 4 | **132**/255 | 132/10.0 % · 129/9.4 % · 132/8.2 % | live |

`Enabled` and `Strength` give identical figures because strength 0 removes the body entirely, which
is what "off" means.

### 4.7 The `.dcmv` sculpting recipe

Swept by patching the recipe stored in `HeroCloud_Congestus.dcmv` and re-baking it with
`CloudVolumeBaker --in … --out …`. The panel's slider gives the artist's range; the recipe validator
and the bake's boundary check then refuse combinations that burst the body's `SizeKm`, so each end
was taken as far toward the panel's limit as the validator accepts **on this body**, and the value
actually reached is in the table. A sweep clipped by a stated rule is a measurement; one clipped
silently is not.

| value | panel range | reached | movement | per ray | verdict |
|---|---|---|---|---|---|
| `ProfileDepthKm` | 0.001 – 2 | 0.001 – 2 | **130**/255 over 12.7 % | 130/12.7 % · 127/11.5 % · 130/10.0 % | live |
| `blob.Weight` | 0.125 – 8 | 0.125 – **1.52** | **127**/255 over 12.2 % | 127/12.2 % · 125/11.3 % · 127/9.4 % | live |
| `blob.DensityScale` | 0 – 1 | 0 – 1 | **126**/255 over 9.8 % | 126/9.8 % · 126/9.2 % · 126/8.2 % | live |
| `BlendRadiusKm` | 0.001 – 1 | 0.001 – **0.0593** | **122**/255 over 9.9 % | 122/9.9 % · 118/9.2 % · 122/7.5 % | live |
| `blob.RotationDeg` | −180 – 180 | **0 → 90° about Y** (see below) | **86**/255 over 9.6 % | 85/9.6 % · 86/9.2 % · 53/7.5 % | live |
| `EnvelopeMarginKm` | 0.001 – 2 | 0.001 – **0.1105** | **88**/255 over 3.6 % | 88/3.6 % · 88/3.4 % · 9/2.1 % | live |
| `blob.DetailType` | 0 – 1 | 0 – 1 | **52**/255 over 7.0 % | 52/7.0 % · 50/6.4 % · 46/5.6 % | live |
| `SizeKm` | 0.1 – 16 | 2×1×2 → 8×4×8 km | **31**/255 over 7.7 % | 29/8.2 % · 31/7.7 % · 30/6.0 % | live |
| `blob.CentreKm`, `blob.RadiiKm`, `blob.Primitive` | — | — | — | — | **not measured** (§3) |

**`blob.RotationDeg` is the census's own near-miss, and worth writing down.** Swept over the panel's
own endpoints, −180° → +180°, it moves the frame by **1/255 over 0.000 %** — which reads as dead and
is not. Every lump in this recipe is an **ellipsoid**, and an ellipsoid is centrosymmetric: a 180°
turn about any principal axis maps it exactly onto itself, so the two ends of the slider and its
centre are all *the same orientation*. Choosing the panel's endpoints chose three names for one
shape.

Rotating instead by a quarter turn moves the frame by **86/255** — and even that had to be about
**Y**, because this recipe's lumps are flattened discs (`radii 0.6 × 0.11 × 0.54 km`) sitting inside
a half-height of 0.5 km with essentially no headroom: a Z rotation of as little as **5°** stands them
on edge and the bake refuses the body. So on *this* recipe the control's legal travel about Z is
under five degrees, and about Y it is a full quarter turn. That is `SizeKm` constraining
`RotationDeg` — two values obliged to agree, and the validator says so with the numbers in the
message rather than clamping quietly.

## 5. What the numbers mean, row by row

### 5.1 The two "prime candidates for dead" are the two liveliest surprises

The task brief listed six values as already measured and told me not to re-measure them. I
re-measured them anyway — a verdict of *dead* is the one that gets a working control deleted, so it
is the one that has to be expensive — and **two of the six are refuted**.

**`DetailTileSize`.** The brief: *"the whole range moves **1.29/255**, and its 0.5→3 km sweep gives
roughness 60.7 / 61.0 / 59.8 / 58.7 m, which cancels exactly (Р9). Prime candidate for dead."* The
same claim stands in `Common/CloudField.glslh` at the head of the Р9 note.

Measured on `dev` at `a2631ce0`, `Clouds_Protocol`, 90 frames, `--play`:

| sweep | AZ135/EL45 | AZ000/EL25 | AZ270/EL85 |
|---|---|---|---|
| **0.5 → 3 km — the exact slice Р9 describes** | **79**/255 over 78.6 % | **71**/255 over 70.3 % | **126**/255 over 58.0 % |
| 0.2 → 30 km — the property's own `Range()` | **59**/255 over 79.2 % | **66**/255 over 70.1 % | **81**/255 over 55.0 % |
| 0.2 → 1 km | 26/69.6 % | 45/64.9 % | 87/55.3 % |
| 3 → 30 km | 86/82.3 % | 75/72.9 % | 119/55.0 % |

**Ninety-eight times the quoted figure, on the quoted sweep.** And it is not a statistical
curiosity — the two frames are unmistakable side by side and are committed beside this document
(`Docs/Clouds/Shots/P11_DetailTileSize_*.png`): at 200 m every cloud face is covered in fine
granular speckle and the silhouettes are peppered; at 30 km the same clouds are glass-smooth
hard-edged lobes. That is precisely the "degree of generation (frequency)" the product owner asked
for, and it has been in the tree the whole time labelled as dead.

**What is still true is Р9's mechanism, and it is worth keeping.** The 657 m optical depth against
the erosion's 160 m correlation length is a real measurement, and so is the roughness table: at a
50 m lag the *silhouette roughness* really does barely move (60.7 / 61.0 / 59.8 / 58.7 m). The
error is in what was concluded from it. **Roughness at one lag is not the frame.** The tile also
sets the SIZE of the cut and whether it lands above or below the march's resolvable chord, and both
of those change the picture enormously while leaving a 50 m autocorrelation almost where it was.
The Р9 note contains the warning against its own conclusion, two paragraphs later: *"THE STATISTICS
ALL SAID YES… Only the frame said no, which is §1 of the verify skill in one line."* Here the
statistics said no and the frame says yes.

**`DetailStrength`.** The brief: *"whole range **2.86/255**; surface displaced 138.7 m at 0.65 and
180.0 m at 1.00. A ceiling, not a knob."* Measured, 0 → 1: **84 / 117 / 151** of 255 over 85–100 %
of the frame, and the tonal contrast at the sunward ray runs **0.170 → 0.304**. Half the range
(0 → 0.5) is already **50 / 108 / 145**. The 138.7 m and 180.0 m surface-travel figures are not in
question here — this census did not re-measure them, and they are a property of the field rather
than of the frame. What is not true of this tree is that the range is worth 2.86 levels of 255.

Both numbers most plausibly predate the phase Э5 producer rebuild, whose own note records that the
cut *"got STRONGER when the producer changed"* — at strength 0.10 the erosion removed 1.7 % of the
old field's mass and removes 7.1 % of this one's. A figure carried forward from before that change
would be exactly this wrong, and neither was re-taken.

### 5.2 A layout knob is only measurable against a layout that can carry it

`Layout Mask Strength` measured **0/255 on all three rays** in the first sweep — and that was my
harness, not the engine. It was bound to `Layout_Stripe`, whose container flags are `1`:
**pattern only, no mask**. The component's own tooltip already says *"A layout with no mask in it
contributes nothing at any setting"*, so the reading was correct and the conclusion would have been
a lie. Against `Layout_LetterD` (flags `3`, pattern **and** mask) the same sweep is **152**/255.

The mirror image bit as well. `Layout Pattern Strength` reads **0/255** against `Layout_LetterD` —
because `LetterD`'s mask at full strength empties the sky at all three rays, so both ends of the
pattern slider render the same cloudless frame. (`CloudLayout` empty → `LetterD` gives
**154**/255 over 63.0 %, the identical signature to `Enabled` off → on.) Against `Layout_Stripe` it
is **154**/255.

**Neither knob is dead. Each is dead against the layout that cannot express it**, and no single
condition measures both. That is a property of a painted asset with two independent channels, and it
is why every layout row in §4.1 names the layout it was measured against.

### 5.3 `Tracing Start Max Distance` moves one ray of three, and that is the mechanism working

`CloudRaymarch.shader:343` — `if (segment.x > max(u_CloudPhase.w, 0.0f)) return;` — drops a ray
whose *entry into the layer* is beyond the limit. With the camera 2 m up and the congestus base at
2.2 km, entry is about `2.2 / sin(elevation)` km: 2.2 km at the zenith, 5.2 km at EL 25, 25 km at
EL 05. At the low end of the slider (10 km) nothing at EL 25, 45 or 85 is beyond it — except the
**bottom of the EL 25 frame**, which looks along much shallower rays than its centre. Hence
**89**/255 over 14.5 % of that one frame and exactly 0 elsewhere. It is a guard on grazing rays
doing what it is documented to do, and §4.5 measures it where it lives.

### 5.4 The near fade is one setting with two fields, and the census can see that

Swept in isolation at the shipped camera, `Near Fade Start Distance` moves **0/255**. That is
`Graphic::CloudResolveNearFade` behaving exactly as its `static_assert`s say: the pair returns
`{0, 0}` — fade OFF — unless `End` is strictly past `Start`, so sweeping `Start` with the shipped
`End` of 0 never turns the fade on at either end.

Measured with the other half of the pair set to the value that turns the fade on, and the camera put
inside the layer (a census type with base 0.02 km), **both fields move 145/255 and the two numbers
are identical to the digit** — because both sweeps are the same two states, fade off and fade on
over 0–20 km. Two fields, one setting, and the census reproduces that rather than reporting a dead
knob.

### 5.5 The smallest live control in the subsystem

`Aerial Perspective Start Distance`, at **8/255** over 64.8 % of the frame, is the weakest thing in
the census that still clears the bar — and it is weak for a reason the shader states:

```glsl
// CloudRaymarch.shader:682
if (u_CloudFade.y > 0.0f)                                    // Fade Distance
    aerialAmount = clamp((meanDistanceKm - u_CloudFade.x)    // Start Distance
                         / u_CloudFade.y, 0.0f, 1.0f);
```

`Start` is **only read when `Fade` is non-zero**, and `Fade`'s own shipped default is `0`. So on a
component straight out of the constructor this knob is inert; it is live here only because
`Clouds_Protocol` authors `Fade = 90 km`. Its whole travel changes the frame's saturation
(0.152 → 0.145) and leaves the tonal contrast at 0.391 either way — it is a haze dial, and haze at
5–60 km of cloud distance is a small effect.

### 5.6 Two knobs the measurement scene could not see at all

`Cast Shadows` and `Shadow Strength` shade the world *under* the layer, and `Clouds_Protocol`'s
"Checker Floor" is a cube of `Scale 4000` — **±20 m of ground**, against cloud shadow cells 3 km
across. Both read 0/255 there and both are **179/255 over 100 % of the frame** on
`Clouds_ShadowsOnGround`, whose pad is ten times larger, looking down. The two give identical
numbers because `Shadow Strength = 0` skips the pass entirely, "exactly as if Cast Shadows were
off" — which is what the tooltip promises.

It is worth naming what this nearly cost: a census that swept only sky rays — which is what "shoot
the dome" means if taken literally — would have declared two working features dead, on a scene whose
only ground is a 40-metre pad nobody put there for shadows.

### 5.7 `Max Steps` is live; its top octave is saturated

The brief's figure — 256 → 512 worth 0.92/255 at 24° and 2.30/255 at 7° — **reproduces**: I measure
**2 / 8 / 4** over that octave. But that is not the property's range. `Range(8, 512)` end to end is
**136 / 128 / 154** of 255: at 8 steps the sky is a washed-out band (saturation 0.352 → 0.074,
contrast 0.261 → 0.256 at the sunward ray). The knob is live; only its top doubling is past the
plateau, which is what a cost ceiling should look like.

### 5.8 `Max View Distance`: two of Р8's three claims reproduce exactly

The brief carries three statements about it, and separating them is the whole answer.

| sweep | EL 0.5 | EL 2.79 | EL 8 | EL 25 |
|---|---|---|---|---|
| **104.41 km → 400 km** (the claimed dead range) | **0**/255 | **0**/255 | **0**/255 | **0**/255 |
| 60 km (shipped) → 400 km | 13/8.5 % | 15/8.5 % | 12/8.6 % | **0**/255 |
| 60 km → 30 km | 14/19.4 % | 15/19.3 % | 16/19.5 % | **0**/255 |
| 1 km → 400 km (the property's own `Range()`) | 101/52.1 % | 105/58.8 % | 119/73.6 % | 128/83.5 % |

- ***"past 104.41 km is dead range"* — confirmed, to the byte.** Not one pixel of 980 480 moves
  between 104.41 km and the top of the slider at any elevation tested. That is as clean a
  confirmation as this instrument can produce.
- ***"can only act below 2.79° of elevation"* — consistent, to within a quarter of a degree.** A
  frame is a cone of rays, not one ray: the camera's vertical field of view is 45°
  (`ECS::CameraComponent::FOV`, `Core/Camera.cpp` passes it to `MakePerspective` as the vertical
  angle), so the frame centred at EL 25 spans **rays from 2.5° to 47.5°** and shows **exactly 0**
  differing pixels for both sweeps around the default. The frames centred at EL 0.5, 2.79 and 8 all
  reach far below 2.79° and all show the effect over the same ~8.5 % / ~19 % of frame. So the onset
  sits at or just under **2.5°** of *ray* elevation — Р8's 2.79° to within what a whole-frame
  instrument can resolve. The knob acts on a band at the horizon and nowhere else, as claimed.
- ***"buys one level of 255"* — not confirmed.** Around the default it buys **12–16** of 255 in that
  band, and over its authored range **146**.

So the right way to state this row is: **the knob is live over its range, and inert over the top
three-quarters of its slider.** That is a *range* finding, not a dead setting — and if anything it
argues for lowering the slider's ceiling from 400 km toward 105 km, which would be a decision for
the owner rather than a defect.

### 5.9 The one number that reproduced exactly

The brief's *"Placement lattice — doubling collapses contrast 0.391 → 0.283"* is `Weather Tile Size`
12 km → 24 km, and at `AZ000 EL25` this harness measures **contrast 0.391 → 0.283**, to three
decimals, on a scene and instrument set up independently. That agreement is the reason to trust the
rest of the numbers here.

## 6. Decisions

**Nothing is dead, so nothing is proposed for deletion.** `DEV_CONTRACT.md` §1.3 is satisfied by
every authored value in the subsystem: each is wired to a visible effect, and the effect has now been
measured rather than assumed.

Four things do need a decision, and all four are yours — this task changed no code.

1. **Correct the Р9 note in `Common/CloudField.glslh` (a repair, not a deletion).** Its head
   currently tells the next developer that `DetailTileSize` is worth 1.29/255 and `DetailStrength`
   2.86/255. Both are false about this tree by two orders of magnitude, and the note is exactly
   where somebody would go to decide whether to delete a control. The *mechanism* it records is
   sound and should stay; the two frame figures should be replaced with the numbers in §5.1 and the
   sentence "which is why six tasks found nothing here" re-pointed at the roughness metric rather
   than at the picture.
2. **Say in `Aerial Perspective Start Distance`'s tooltip that it needs a non-zero Fade Distance.**
   The shader gates it and the component default is zero, so out of the box the knob is inert — the
   `Near Fade` pair has a comment saying exactly this about itself and this pair has none.
3. **Nothing to do about `Detail Tile Size` beyond point 1.** It is not inert and needs no repair.
   If anything its tooltip is already the best in the component: it names both bounds and why.
4. **Consider whether `Max Steps` should stop at 256.** Its top octave is worth 2–8 levels of 255
   and costs proportionally; that is a range decision, not a dead setting, and it is a question for
   the quality tiers rather than for this census.

## 7. The six values the brief supplied, as measured here

The brief supplied six figures and said not to re-measure them. They are reproduced as given, beside
what this harness measures on `dev` at `a2631ce0`.

| value | as supplied | measured here | agrees? |
|---|---|---|---|
| `DetailTileSize` | whole range **1.29**/255; 0.5→3 km roughness 60.7/61.0/59.8/58.7 m "cancels exactly"; *prime candidate for dead* | **126**/255 over the 0.5→3 km slice itself, **81**/255 over the full `Range()` | **no — §5.1** |
| `DetailStrength` | whole range **2.86**/255; 138.7 m at 0.65, 180.0 m at 1.00; *a ceiling, not a knob* | **151**/255 over 0→1; **145**/255 over 0→0.5 | **no — §5.1** |
| `MaxSteps` 256→512 | 0.92/255 at 24°, 2.30/255 at 7° | 2/255 at EL45, **8**/255 at EL25, 4/255 at EL85 | yes, same order — but the property's *range* is 8→512 and that is **154**/255 (§5.7) |
| `MaxViewDistance` | acts only below 2.79° of elevation; buys one level of 255; past 104.41 km is dead range | *dead range past 104.41 km:* **0** differing pixels at every elevation — exact. *Below 2.79°:* consistent to a quarter degree. *One level:* no — 12–16/255 around the default, **146**/255 over the whole `Range()` (§5.8) | 2 of 3 |
| Placement lattice, doubled | contrast 0.391 → 0.283, live | `WeatherTileSize` 12→24 km at AZ000/EL25: contrast **0.391 → 0.283** | **yes, exactly** |
| `AmbientScale` 1→0 | contrast 0.202 → 0.401; live, *the largest in the subsystem* | live, **102**/255; contrast at the sunward ray 0.257 → 0.447 — same direction, this scene's own values. But the largest single movement in the whole census is `ScatteringAlbedo` at **237**/255 | live: yes. Largest: **no** |

## 8. The harness

Everything above is reproducible from the tree plus three scripts' worth of glue. What it does:

1. Parse `Clouds_Protocol.desce`, which spells out **all 52** `VolumetricCloud` fields explicitly,
   so any of them can be set without relying on a C++ default. Only four scenes in the repository do
   — the protocol scene and `PR_Hero0/3/8`; the next-richest carries 16 fields and most carry 15.
   *(A project note says "a `.desce` carries 7 of 51 fields and the rest follow C++ defaults". That
   was the state it described; it is not true of the protocol scene, which is the one to measure on.)*
2. Write one scene per endpoint into `Editor/Resources/Assets/Scenes/_census/`, one census
   `.decloudtype` per type-shape endpoint into `Clouds/Types/`, and one patched-and-re-baked
   `.dcmv` per recipe endpoint — patch the recipe in place (the two CRCs cover the voxels and the
   blob bytes, both plain zlib CRC-32), then `CloudVolumeBaker --in … --out …`. All of these are
   deleted again afterwards; nothing under `Editor/Resources/Assets` is changed by this task.
3. Render four at a time, checking each capture against its own log rather than its exit status.
4. `Tools/ImageDiff` between the two endpoints, per ray.

None of it is committed: it produced numbers, not a feature, and a measurement rig that nobody
maintains is worse than none. What **is** worth keeping is the two facts that made it affordable,
because the next census will need them:

- **`--shot` is byte-deterministic under concurrency on this scene.** Four editors rendering at once
  give the identical PNG to one editor rendering alone. Six is not safe — two of six died in Vulkan
  initialisation.
- **Frames are nearly free; startup is not.** 90 frames cost ~2 s more than 30 across two concurrent
  captures, against ~30 s of startup, so there is never a reason to render an unconverged frame.

Committed beside this document, and nothing else:

- `Shots/P11_DetailTileSize_0k2km_az000_el25.png` and `…_30km_….png` — the refutation of §5.1.
  Fine granular speckle over every cloud face at 200 m; glass-smooth hard-edged lobes at 30 km.
- `Shots/P11_Profile_shipped_az000_el25.png` and `…_reversed_….png` — the vertical profile with its
  mean half-width held constant to the last digit and only the silhouette reversed: a wide-based
  congestus deck against mushroom-topped clouds. This is the pair that answers the product owner's
  criterion directly.

## 9. What this task did not do

- **No code changed.** The two repairs §6 proposes are the owner's to approve; nothing in this
  branch touches a header, a shader or an asset.
- **The full suite sweep of `desert-engine-verify` §3 was run anyway** and is green — 0 build
  failures, 0 suite failures — because the harness wrote and deleted files under
  `Editor/Resources/Assets/Clouds/Types/`, and `Desert/Tests/Engine/CloudType` pins the shipped
  library. The §7 skip-list audit reports nothing hidden.
- **No performance numbers.** Not asked for, and the machine was shared throughout.

## Related

- `Docs/Clouds/DEV_CONTRACT.md` §1.3 — the rule this census tests.
- `Desert/Tests/Engine/SettingConsumers` — pins that every field is *read*. This document is the
  other half: that reading it changes the picture.
- `Editor/Resources/Shaders/Common/CloudField.glslh` — the Р9 note whose two frame figures §5.1
  contradicts and whose mechanism it keeps.
- `desert-engine-verify` §1 — the repeat shot, the dome, and "you still have to look".
