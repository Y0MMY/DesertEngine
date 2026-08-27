# Р0 — what "мультяшные" actually is, measured

Task Р0 of `PLAN_REALISM_AND_AUTHORING.md`. **No rendering code was changed.** The deliverable is a ranked
list of measured discrepancies, six explicit verdicts, and the frames they were taken on.

Decision **D-21** exists because two explanations of this look were asserted and both turned out to be
wrong. This document is written so that a third assertion cannot be made out of it: every claim below is a
number taken on a named frame over a named rectangle, and where the measurement **refutes** a hypothesis
the refutation is stated as loudly as a confirmation.

**Of the six hypotheses, exactly one is confirmed.** Three are refuted outright (1, 4, 6), one is refuted
as a cause with a residue I could not close (5), and one is confirmed as a ceiling and refuted as a knob
(2). One of the refutations is of a mechanism I had already written up as the answer before I rendered
the test that killed it — §5.

---

## 0. The method, and the one thing about it the reader must know first

**Scene** `Editor/Resources/Assets/Scenes/Clouds_Protocol.desce` — all 51 cloud parameters written out
(§PR of `CALIBRATION.md`), so no C++ default can move it. Camera `0,200,0`, `--play`, Debug, MoltenVK,
1280x766.

**Six points**, three elevations × two azimuths. `--look 0,0.9,∓1` is **42°** of elevation, `0,0.45,∓1` is
**24°**, `0,0.12,∓1` is **7°**; `-Z` is away from the sun, `+Z` is into it.

**Two frame windows**: `--shot-frames 90` and `--shot-frames 3`.

**Instruments.** `Tools/ImageStat` over `0 0 1280 551`, `Tools/LineJump` over `2 2 1278 551`. Seven
further CLIs were written for this task, run, and deliberately **not committed** — they are experiments,
and what each one measures is stated where it is used: `PixDiff`, `EdgeStat`, `ScaleStat`, `ShapeStat`,
`VertShade`, `DepthShade`, `BandLimit`. Four of the seven produced nothing usable and §7 says so.

**The reference** is `Docs/Clouds/UEReference/UE_{mid,zenith,horizon}.png` over `377 169 2035 991`,
re-measured in this tree rather than quoted:

```
UE_mid.png      mean 0.609  p05 0.321  p50 0.650  p95 0.800  contrast 0.479  sat 0.192
UE_zenith.png   mean 0.569  p05 0.335  p50 0.496  p95 0.813  contrast 0.479  sat 0.297
UE_horizon.png  mean 0.595  p05 0.365  p50 0.622  p95 0.803  contrast 0.438  sat 0.286
```

`UE_mid` is `CALIBRATION.md`'s recorded row to the last digit. The ruler is the programme's own ruler.

**The noise floor is measured, not claimed.** The base command at `mid_away` run a second time gives
**0 differing pixels of 705 280** and the same md5 (`8ecda0f1dfcebd52812ed023d59d5a72`). Every pixel diff
below is the change and nothing else.

**The first render in this worktree was discarded** (verify skill §7).

**No timings are quoted.** The machine was shared throughout — load average between 9 and 88 while these
frames were taken — and by the lead's ruling a slope measured now would measure the neighbours. Nothing in
the conclusion depends on a cost number; where one would help it is named in §8.

### ⚠️ THE REFERENCE AND THE PROTOCOL SCENE ARE NOT THE SAME SUBJECT

This has to come before the tables, because it decides how much any of them is worth.

`UE_mid.png` is a **thin, distant, high-frequency stratocumulus sheet** seen from below, about half deep
blue, whose smallest cloud features are a handful of pixels across. `Clouds_Protocol` is a **near-field
cumulus congestus deck** (`Cumulus_Congestus.decloudtype`, base 2.2 km, top 5.8 km) whose nearest bodies
fill a quarter of the frame. They differ in cloud genus, in distance, in coverage and in sun angle.

`CALIBRATION.md` hedges this once — "the two frames are still not the same sun angle or the same camera
elevation, so an exact match is not the target and never was". **The hedge is not strong enough for what
Р0 was asked to do.** Any statistic that depends on the SIZE or the ARRANGEMENT of the clouds — silhouette
width in pixels, component counts, isoperimetric ratio, top-versus-bottom shading — measures the
difference of subject and not the difference of renderer. I built four such instruments, ran them, and
**I am not ranking on any of them**; their numbers are in §7 so that nobody builds them again believing
they will settle this.

What survives the difference of subject, and what the ranking rests on:

* **Tonal statistics** — `p05`, `p95`, contrast — which are about how a lit cloud and a clear sky sit
  against each other and are only weakly a function of which cloud it is.
* **Knock-outs inside our own renderer**, where the noise floor is zero and the comparison is exact.
* **Relations between two of our own numbers**, which need no reference at all.

---

## 1. THE RANKING

| # | discrepancy | the number that ranks it | hypothesis |
|---|---|---|---|
| **1** | **The sky-light term is applied with no occlusion by the cloud above the sample, and it floods the frame.** A sample can never receive less than **half** the whole sky's mean radiance however much cloud is over it — the floor is algebraic, not authored. | At `horizon_away`, setting `AmbientScale` to 0 moves contrast **0.202 → 0.401** against the reference's 0.438: **84 % of the gap, from one field**. Largest knock-out anywhere in the subsystem: mean **62.26 / 255** over 97.5 % of pixels, and the ladder is monotone (1.0 → 0.5 → 0.0 gives 0.202 → 0.271 → 0.401). | **3, CONFIRMED** |
| **2** | **The existing occlusion knob cannot reach it, and it fails hardest exactly where the occluder is OTHER cloud.** `AmbientOcclusionStrength` is a `mix` toward `sqrt(1-Profile)`, and `Profile` is the sample's own local depth. | At its ceiling (1.0) it recovers **17 %** of the gap at 7° (0.202 → 0.241 of 0.438) and **34 %** at 42° sunward (0.296 → 0.358 of 0.479). A local term works where the body occludes itself and fails where the deck does. | **3, CONFIRMED** |
| **3** | **The sunlit cloud top is short by 0.080 of display range, by the SAME amount at all three away elevations, and NO authored field lifts it.** Ranked below #1 and #2 only because it names a symptom rather than a mechanism. | `p95` 0.734 / 0.719 / 0.723 against 0.813 / 0.800 / 0.803 — short by **0.079 / 0.081 / 0.080**. Across all eighteen knock-out rows that keep the clouds, the highest `p95` reached is **0.743** (`PhaseBlend` = 1), still 0.057 short. `p05` at 42° and 24° away matches the reference to 0.018 and 0.003. | **3 and 4** |
| **4** | **The contrast deficit is invariant to exposure and to the tonemapper.** It is a property of the scene's own dynamic range, not of where that range sits on a curve. | Exposure 0.18 / 0.26 / 0.35 / 0.50 → contrast **0.373 / 0.395 / 0.393 / 0.402**, while `mean` sweeps 0.436 → 0.715. ACES 0.395 vs Reinhard 0.265. Reference 0.479. | **6, REFUTED** |
| **5** | **The erosion — the only mechanism that can break a smooth silhouette — is saturated.** At the TOP of its authored range it displaces the cloud's visible surface by less than the march's own search chord at two of the three elevations. | Surface travel **138.7 m at the shipped 0.65 and 180.0 m at 1.00** (`Desert/Tests/Engine/CloudField`, printed by the suite). Search chord **168 m at 42°, 273 m at 24°, 906 m at 7°**. `DetailStrength` 0.65→1.00 moves mean **2.86 / 255** and contrast 0.395 → 0.394. | **2, CONFIRMED as a ceiling, REFUTED as a knob** |
| **6** | **Over cloud pixels our frames carry about half the reference's fine-scale luminance variation, and I could not name what limits it.** Not the erosion, not the march step, not the convergence window, not the tier. | `E1` over cloud: ours **0.00165 / 0.00199** at 42°/24° away against the reference's **0.00318 / 0.00375**. `MaxSteps` 256→512 leaves it at 0.00201; `DetailTileSize` 1 km→0.5 km at 0.00204. The remaining candidate is the quarter-resolution trace, which needs a C++ edit this task does not own — §8. | **OPEN** |
| — | **Density and transmittance are on a plateau.** | `ExtinctionScale` 4 / 8 / 16 → contrast 0.389 / 0.395 / 0.400. `StopTransmittance` ×10 → mean 1.16 / 255. | **4, REFUTED** |
| — | **Reconstruction contributes nothing to the softness of a still, and 0.40 of a level under motion.** | 3 vs 90 frames: `E1` 0.00202 vs 0.00202. Tier High→Low: mean 1.29 / 255. Moving vs static at the same end pose, 600 m at 400 m/s: **mean 0.40 / 255, max 13**. | **5, REFUTED** |

**If one discrepancy dominates, it is #1, and #2 is the reason it has not already been fixed by turning a
dial.** They are one sentence: *the clouds are lit by a large, undirected, effectively unoccluded sky term
that fills their shadows*, so a deck has no dark side; and the knob that exists to occlude it is a
function of a local quantity that cannot express "there is cloud above me".

#5 and #6 say the surface texture that would otherwise disguise this **cannot be turned up either**, and
that is a separate finding rather than a corollary: the erosion is at its ceiling, and the four things one
would reach for to raise it — more strength, a finer tile, more march steps, both together — total less
than 3 of 255 between them. **What limits the surface is not any of them and I did not find it.**

**#4 is the one that has to be said out loud because the brief asked for it the other way round.** The
brief lists the 0.333-vs-0.482 contrast gap as "still open in `CALIBRATION.md`". It is not: that
paragraph was superseded in the same file (lines 245–248, §T-ACES), and my own measurement confirms the
supersession. Tonemapping is not part of this.

---

## 2. The base — six points, both windows, against the reference

`--shot-frames 90`. Frames: `Shots/P0_base90_*.png`.

| point | mean | p05 | p50 | p95 | contrast | sat |
|---|---|---|---|---|---|---|
| zenith away `0,0.9,-1` (42°) | 0.569 | 0.317 | 0.557 | 0.734 | 0.417 | 0.098 |
| mid away `0,0.45,-1` (24°) | 0.533 | 0.324 | 0.546 | 0.719 | 0.395 | 0.176 |
| horizon away `0,0.12,-1` (7°) | 0.606 | **0.521** | 0.598 | 0.723 | **0.202** | 0.085 |
| zenith sunward `0,0.9,1` | 0.569 | **0.497** | 0.542 | 0.794 | **0.296** | 0.084 |
| mid sunward `0,0.45,1` | 0.560 | **0.465** | 0.545 | 0.719 | **0.254** | 0.117 |
| horizon sunward `0,0.12,1` | 0.590 | **0.494** | 0.573 | 0.743 | **0.248** | 0.113 |
| **UE_mid** | 0.609 | 0.321 | 0.650 | **0.800** | **0.479** | 0.192 |
| **UE_zenith** | 0.569 | 0.335 | 0.496 | **0.813** | **0.479** | 0.297 |
| **UE_horizon** | 0.595 | 0.365 | 0.622 | **0.803** | **0.438** | 0.286 |

These reproduce §PR's frozen base — `0.570 / 0.319 / 0.558 / 0.734 / 0.415 / 0.098` at zenith away against
my `0.569 / 0.317 / 0.557 / 0.734 / 0.417 / 0.098` — to within the 1.5 s of wind that `--play` advances
and §PR did not have. **The chain from §PR to this tree is intact**, so everything below is about the sky
the programme believes it shipped.

**The table says two different things at the two azimuths, and the protocol's insistence on both is what
made that visible:**

* **Away from the sun, at 42° and 24°, the dark end is RIGHT.** `p05` 0.317 and 0.324 against the
  reference's 0.335 and 0.321 — the blue gaps are as deep as Unreal's, and the whole deficit at those two
  points is at `p95`.
* **And `p95` is short by the SAME amount at all three away elevations**: 0.734 / 0.719 / 0.723 against
  0.813 / 0.800 / 0.803 — short by **0.079, 0.081 and 0.080**. A deficit that does not vary with
  elevation is not a march or a step-schedule effect; it is a constant shortfall in how bright a lit
  cloud comes out, and it survives every knock-out in §3.
* **Toward the sun the dark end is gone.** `p05` 0.465 to 0.497 where the reference is 0.321 to 0.365.
  There is no shadow in the frame at all.
* **The horizon-away point is the worst of the six** at contrast 0.202, and for the sunward reason
  rather than the away one: `p05` 0.521. A grazing view through the deck is a wall of one tone.

`LineJump` over `2 2 1278 551` finds **no band anywhere**: row maxima 0.00145–0.00348 at five points
against mean steps of 0.00041–0.00074, and 0.0968 / 0.0948 at `y 540` on the two horizon frames, which is
the checker floor's own edge and is the figure §PR and §DS both record. **Including at sunward-and-high,
which is the one combination the full-width bands of `REVIEW_622a01a6.md` Ц9 needed** — 0.00145 @ y 117
at 90 frames and nothing visible at 3. Banding is not part of this, and the point that would have carried
it was checked in both windows.

### The 3-frame window: no defect is being masked by convergence

`--shot-frames 3`. Frames: `Shots/P0_base3_*.png`.

| point | mean | p05 | p50 | p95 | contrast | sat | vs the 90-frame frame |
|---|---|---|---|---|---|---|---|
| zenith away | 0.571 | 0.319 | 0.559 | 0.735 | 0.415 | 0.098 | 78.3 % of pixels, max 89, mean 2.38 |
| mid away | 0.534 | 0.324 | 0.546 | 0.718 | 0.395 | 0.174 | 67.0 %, max 71, mean 2.38 |
| horizon away | 0.606 | 0.520 | 0.600 | 0.723 | 0.203 | 0.085 | 81.3 %, max 64, mean 2.11 |
| zenith sunward | 0.571 | 0.498 | 0.542 | 0.801 | 0.303 | 0.084 | 81.7 %, max 49, mean 1.71 |
| mid sunward | 0.560 | 0.464 | 0.545 | 0.719 | 0.254 | 0.117 | 74.3 %, max 64, mean 1.95 |
| horizon sunward | 0.590 | 0.495 | 0.573 | 0.742 | 0.247 | 0.113 | 73.2 %, max 58, mean 2.00 |

The convergence window moves individual pixels by up to 89 levels and moves no statistic that matters:
contrast agrees to 0.001–0.007 at all six points, and the fine-scale energy is unchanged (`E1` 0.00200 at
3 frames against 0.00202 at 90, `mid_away`; 0.00316 against 0.00332, `horizon_away`).

`LineJump` on the short window agrees: row maxima **0.00135–0.00376** in the sky at all six, against
0.00145–0.00348 at ninety. The two horizon frames read 0.128 and 0.120 at `y 541` where the long window
reads 0.0968 and 0.0948 at `y 540` — that is the checker floor's own edge, one row lower and not yet
settled, and it is geometry rather than sky.

**The three-frame window found nothing the ninety-frame window hides.** That is a negative result and it
is worth the lines: it is the check that a defect of the kind that once lived in `dev` unseen at 90 is
not present here.

---

## 3. The knock-out table

`mid_away`, `--shot-frames 90`, one authored field changed per row, everything against
`Shots/P0_base90_mid_away.png` on a zero noise floor. The scene variants were temporary `.desce` copies,
now deleted; the shader was not touched.

| row | mean | p05 | p95 | contrast | sat | pixels differing | max | **mean Δ/255** |
|---|---|---|---|---|---|---|---|---|
| **base** | 0.533 | 0.324 | 0.719 | **0.395** | 0.176 | — | — | — |
| clouds off (`Enabled` false) | 0.355 | 0.294 | 0.435 | 0.142 | 0.510 | 80.1 % | 128 | 62.07 |
| **`AmbientScale` 1 → 0** | 0.369 | 0.237 | 0.607 | 0.369 | 0.229 | 80.0 % | 98 | **53.33** |
| `Coverage` 0.762 → 0.40 | 0.490 | 0.304 | 0.711 | 0.407 | **0.287** | 80.1 % | 128 | 33.70 |
| **`MultiScatterOctaves` 3 → 1** | 0.424 | 0.322 | 0.548 | 0.227 | 0.271 | 79.8 % | 62 | **31.60** |
| `Exposure` 0.26 → 0.18 | 0.436 | 0.248 | 0.621 | 0.373 | 0.193 | 100 % | 28 | 26.39 |
| `DetailStrength` 0.65 → 0 | 0.558 | 0.335 | 0.723 | 0.388 | 0.146 | 89.5 % | 112 | 10.17 |
| `Tonemapper` ACES → Reinhard | 0.539 | 0.399 | 0.665 | 0.265 | 0.154 | 97.1 % | 24 | 8.68 |
| `AmbientOcclusionStrength` 0.5 → 0 | 0.559 | 0.324 | 0.735 | 0.411 | 0.180 | 78.3 % | 20 | 7.73 |
| `ExtinctionScale` 8 → 4 | 0.545 | 0.322 | 0.711 | 0.389 | 0.166 | 79.4 % | 28 | 6.69 |
| `ExtinctionScale` 8 → 16 | 0.526 | 0.324 | 0.724 | 0.400 | 0.185 | 79.4 % | 29 | 4.64 |
| `PhaseBlend` 0.575 → 0 | 0.522 | 0.324 | 0.673 | 0.349 | 0.181 | 74.1 % | 17 | 3.03 |
| **`DetailStrength` 0.65 → 1.00** | 0.527 | 0.320 | 0.715 | **0.394** | 0.182 | 72.7 % | 59 | **2.86** |
| `PhaseBlend` 0.575 → 1 | 0.540 | 0.324 | 0.743 | 0.420 | 0.173 | 70.3 % | 10 | 2.00 |
| `DetailTileSize` 1 km → 0.5 km, `MaxSteps` 256 → 512 | 0.530 | 0.323 | 0.715 | 0.392 | 0.177 | 73.3 % | 44 | 1.58 |
| `Cloud Quality` High → Low | 0.530 | 0.324 | 0.715 | 0.392 | 0.181 | 70.8 % | 14 | 1.29 |
| `DetailTileSize` 1 km → 0.5 km | 0.533 | 0.323 | 0.719 | 0.395 | 0.175 | 62.7 % | 49 | 1.29 |
| `StopTransmittance` 0.005 → 0.05 | 0.530 | 0.324 | 0.715 | 0.391 | 0.181 | 67.0 % | 7 | 1.16 |
| **`MaxSteps` 256 → 512** | 0.530 | 0.324 | 0.715 | 0.391 | 0.177 | 74.1 % | 9 | **0.92** |

Read it as an ordering: **the ambient and the multiple-scattering octaves are the two large terms, and
everything the artist can reach for shape is at the bottom of the list.** Of the four rows meant to buy
surface — detail strength up, detail tile down, march steps up, and both together — **not one reaches 3 of
255**, and the tier and the stop threshold are noise.

And one column is worth reading on its own. **No row in the table raises `p95` above 0.743.** Plenty push
it down — the ambient to 0.607, the octaves to 0.548, Reinhard to 0.665 — but the highest any authored
field reaches is `PhaseBlend` = 1 at 0.743, still **0.057 short of the reference's 0.800**. The sunlit
cloud top has a ceiling in this renderer and no exposed parameter lifts it.

### And the same knock-outs at the horizon, where the gap is biggest

`horizon_away`, against `Shots/P0_base90_horizon_away.png`. The reference for this elevation is
`UE_horizon`, contrast **0.438**, so the shipped frame's gap is **−0.236** — the largest of the six points.

| row | p05 | p95 | contrast | gap to 0.438 | closed | mean Δ/255 |
|---|---|---|---|---|---|---|
| **base** | 0.521 | 0.723 | **0.202** | −0.236 | — | — |
| **`AmbientScale` 1 → 0** | **0.233** | 0.634 | **0.401** | **−0.037** | **84 %** | **62.26** |
| `Coverage` 0.762 → 0.40 | 0.408 | 0.735 | 0.327 | −0.111 | 53 % | 30.91 |
| `AmbientScale` 1 → 0.5 | 0.408 | 0.680 | 0.271 | −0.167 | 29 % | — |
| **`AmbientOcclusionStrength` 0.5 → 1.0** (the knob's ceiling) | 0.463 | 0.704 | 0.241 | −0.197 | **17 %** | — |
| `PhaseBlend` 0.575 → 1 | 0.522 | 0.749 | 0.228 | −0.210 | 11 % | 2.29 |
| `ExtinctionScale` 8 → 16 | 0.513 | 0.734 | 0.221 | −0.217 | 8 % | 4.16 |
| `Exposure` 0.26 → 0.18 | 0.419 | 0.628 | 0.209 | −0.229 | 3 % | 26.46 |
| `DetailStrength` 0.65 → 1.00 | 0.515 | 0.723 | 0.208 | −0.230 | 3 % | 2.23 |
| `Cloud Quality` High → Low | 0.518 | 0.723 | 0.205 | — | — | 1.27 |
| `StopTransmittance` 0.005 → 0.05 | 0.518 | 0.720 | 0.202 | — | — | 1.07 |
| **`MaxSteps` 256 → 512** | 0.514 | 0.715 | **0.201** | — | — | **2.30** |
| `DetailTileSize` 1 km → 0.5 km | — | — | — | — | — | 1.11 |
| `MultiScatterOctaves` 3 → 1 | 0.410 | 0.602 | 0.192 | worse | — | 39.37 |
| `DetailStrength` 0.65 → 0 | 0.536 | 0.723 | 0.186 | worse | — | 6.41 |
| `ExtinctionScale` 8 → 4 | 0.534 | 0.715 | 0.181 | worse | — | 5.89 |
| `AmbientOcclusionStrength` 0.5 → 0 | 0.564 | 0.742 | 0.178 | worse | — | 7.99 |
| `PhaseBlend` 0.575 → 0 | 0.518 | 0.684 | 0.166 | worse | — | 3.44 |
| `Tonemapper` ACES → Reinhard | 0.530 | 0.669 | 0.139 | worse | — | 6.62 |

**One authored field closes 84 % of the largest contrast gap in the protocol.** No other single field
closes more than half of it; the best of the four rows that are about cloud SHAPE closes 3 %, and the
alternatives to the shipped tonemapper and to the shipped exposure both make it worse.

Frames: `Shots/P0_ko_amb0_horizon_away.png` and `Shots/P0_ko_ao1max_horizon_away.png` against
`Shots/P0_base90_horizon_away.png`. The three of them are the argument, and the numbers only say where to
look. The base frame is a field of near-identical white lobes with no underside. The knob at its ceiling
is the same frame with slightly more definition. The ambient-off frame is a deck with modelled, shaded
bases — too dark to ship, because zero ambient is not physical and is not a proposal, but unmistakably
the more cloud-like of the three, which is what makes the term rather than the shape the first suspect.

### The third point, sunward and high, which is where the knob DOES bite

`zenith_sun`, reference `UE_zenith`, contrast **0.479**, shipped gap **−0.183**.

| row | p05 | p95 | contrast | gap | closed |
|---|---|---|---|---|---|
| **base** | 0.497 | 0.794 | **0.296** | −0.183 | — |
| `AmbientScale` 1 → 0 | 0.213 | 0.712 | **0.499** | +0.020 | **overshoots** |
| `AmbientScale` 1 → 0.5 | 0.378 | 0.755 | 0.377 | −0.102 | 44 % |
| **`AmbientOcclusionStrength` 0.5 → 1.0** | 0.417 | 0.775 | 0.358 | −0.121 | **34 %** |
| `AmbientOcclusionStrength` 0.5 → 0 | 0.560 | 0.809 | 0.249 | worse | — |

**Compare the 34 % here with the 17 % at the horizon** — same knob, same maximum, half the effect at the
low elevation. That difference is the whole of verdict 3's mechanism and is set out there.

**And the two azimuths bracket the answer, which is worth more than either alone.** Deleting the ambient
entirely **overshoots** the reference here by 0.020 and **undershoots** it at the horizon by 0.037. So
the right amount of occlusion is somewhere strictly inside "all of it" at 42° sunward, and at 7° even
removing all of it does not close the gap — something else contributes there as well, and the horizon
table names the candidate: `Coverage`, which closes 53 % on its own. **Zero ambient is therefore not the
answer at either point**; it is the knock-out that says which term the answer is about.

**One reading of the ladder has to be stated carefully, because it changes what the fix would be.**
Halving the ambient takes `p05` from 0.521 to 0.408, close to the reference's 0.365 — but `p95` falls with
it, 0.723 → 0.680. The term is doing two jobs at once: it brightens the cloud and it fills the cloud's
shadow, and there is nothing in the march that separates them. **So the finding is NOT "the ambient is
about twice too strong".** It is that the ambient has no geometric occluder, and scaling it down trades
the highlight for the shadow one-for-one. That distinction is the whole difference between a tuning pass
and the second volume Unreal builds.

---

## 4. The six verdicts

### 1. Silhouette — the coverage `smoothstep` threshold. **REFUTED, and the hypothesis is stale**

**There is no coverage `smoothstep` threshold in the tree.** D-15 preserved one against the reference, and
§Э5 and §RW then deleted it: `Coverage` now addresses a fraction of sky directly through the fraction of
living cells (`Engine/Assets/CloudProceduralVolume.cpp:926-943`, `pow(cellCoverage, 0.68)` and a hash gate),
and the profile is the normalised distance field of an exponential smooth-min join of lumps
(`Editor/Resources/Shaders/Common/CloudField.glslh:180-200`). `RESEARCH_LAYOUT_TEXTURES.md:290-294` already
records that this is "a third construction, neither Unreal's nor our previous one". The only threshold
remaining anywhere on the path is a per-CELL hash gate that decides whether a cloud exists at all, softened
by `fill`, and it operates on whole bodies rather than on their edges — which is what a weather system is
supposed to do.

**The number:** the density is `clamp((Profile − erosion) / (1 − erosion), 0, 1) × DensityScale`
(`CloudField.glslh:549-551`) — a linear rescale, with no step function anywhere in it.

D-15's own revisiting condition ("if the T0 frames show the threshold conflicting with the profile table")
can never be met, because neither the threshold nor the table exists any more. **D-15 should be retired as
spent rather than revisited.**

### 2. Edge erosion — `CloudSampleDensity`, `DetailStrength`, `DetailTileSize`. **CONFIRMED as a ceiling, REFUTED as a knob**

**The brief asks for the transition-zone width at the silhouette, in pixels. Here it is, and here is why
it is not the answer.** `EdgeStat` measures every monotone crossing of the sky↔cloud band along each row
and reports the median width:

| | 42° away | 24° away | 7° away | 42° sun | 24° sun | 7° sun | UE_mid | UE_zenith | UE_horizon |
|---|---|---|---|---|---|---|---|---|---|
| median crossing, px | 14 | 10 | 17 | 96 | 37 | 22 | 29 | 47 | 48 |

Ours is **sharper** than the whole reference range at four of the six points, inside it at one, and much
softer at one. Across two subjects at two distances the number is measuring how far away the cloud is,
not how eroded it is — §7. **Within** our own renderer it does work, and there it says the erosion is
saturated: erosion off → 18 px, shipped 0.65 → 10 px, maximum 1.00 → 10 px at `mid_away`.

So the erosion is measured in **metres of cloud** instead, which needs no reference frame at all.
`Desert/Tests/Engine/CloudField` prints, in this tree, on every run:

```
tile 1.00 km: erosion wave 235 m against a body chord of 1672 m (0.14 of it)
the surface sits at profile 0.576; at strength 0.65 it travels 139 m (the march resolves 125 m)
   strength   travel (m)   dissolved
     0.10         36.9       0.014
     0.65        138.7       0.068     <- shipped
     1.00        180.0       0.088     <- the top of the slider's Range
```

**The whole authored range of the erosion moves the visible surface by 180 m at most.**

Against that, the march's own search chord — two coarse steps, `2 × 4 × segment / MaxSteps` =
**`segment / 32`** (`Common/CloudGeometry.glslh:261`, `:266-269`, `:286-289`) — at the protocol's three
elevations. The segment is the chord of the shell, `d(R) = −R₀·sinθ + √(R² − R₀²cos²θ)` between
`R` = 6362.2 and 6365.8 km at `R₀` = 6360 km, so every figure below is checkable rather than asserted:

| point | shell entry → exit | segment | fine step | **search chord** | 180 m as a multiple of it |
|---|---|---|---|---|---|
| 42° | 3.33 → 8.71 km | 5.38 km | 21.0 m | **168 m** | 1.07x |
| 24° | 5.37 → 14.11 km | 8.74 km | 34.1 m | **273 m** | 0.66x |
| 7° | 18.29 → 47.27 km | 28.98 km | 113.2 m | **906 m** | 0.20x |

The suite's own line — *"at every tier the march resolves 125 m and the erosion's wave is 1.88x it"* —
compares against `CloudFinestResolvableChordKm`, which is the chord for a segment of exactly
`CLOUD_DISTANCE_TO_MAX_STEPS_KM` = 4 km. **No protocol camera point has a segment that short**; the
shortest is 5.38 km. The relation the test asserts is true and is about the best case, and the shipped
cameras are not in it.

**The frame agrees about the saturation.** Turning `DetailStrength` from 0.65 to the top of its range
moves mean 2.86 / 255, contrast 0.395 → 0.394, and the median silhouette crossing 10 px → 10 px; turning
it OFF moves 10.17 / 255 and widens the crossing to 18 px. **The knob's remaining range is worth less than
a quarter of switching it off**, which is what "saturated" means, and it is visible in
`Shots/P0_ko_det0_mid_away.png` → `P0_base90_mid_away.png` → `P0_ko_det1max_mid_away.png`: the first is a
pile of smooth ellipsoids, the second has a little granularity, and the third is indistinguishable from
the second.

**The frame does NOT agree about the chord being the cause.** ⚠️ Doubling `MaxSteps` halves every chord in
the table above and changes the picture by 0.92 / 255 at 24° and 2.30 / 255 at 7°, with no gain in
fine-scale energy at either. The relation is arithmetically true and is **not the binding constraint** —
see §5, which sets it out at length precisely so it is not re-derived and believed.

### 3. Lighting — phase, octaves, ambient, ambient occlusion. **CONFIRMED, and it is the ranking's #1**

**The mechanism, from the source and then from the frame.**

`CloudRaymarch.shader:400-403` resolves one ambient radiance per pixel — `AmbientScale` times the
full-sphere mean of the sky, one texel written by `SkyDistantLight.shader`. It enters the first
scattering order only (`:572-573`), multiplied by `CloudAmbientOcclusion(field.Profile, strength)`, which
is `mix(1, sqrt(1 − Profile), strength)` (`CloudLighting.glslh:88-92`).

`Profile` is the sample's own normalised depth inside its body. **It carries no information about how much
cloud lies between the sample and the sky.** So at the shipped strength of 0.5:

* a sample at the very centre of the densest possible body (`Profile` = 1) receives `mix(1, 0, 0.5)` =
  **0.500** of the whole sky's mean radiance;
* a sample on the visible surface, which the suite measures at `Profile` 0.576, receives
  `mix(1, sqrt(0.424), 0.5)` = **0.826** of it.

**Three kilometres of cumulus congestus overhead changes neither number.** The header already names the
hole — `ECS/VolumetricCloudComponent.hpp` records that sky-light occlusion "is a DIFFERENT quantity with a
different geometry (a hemisphere rather than a direction) and Unreal builds a second, separate volume for
it. It is not approximated here with this map... Named as out of scope rather than half-done." It is
honestly named and it is the largest measured discrepancy in the sky.

**The numbers:**

| | contrast at `horizon_away` | vs `UE_horizon` 0.438 |
|---|---|---|
| shipped (`AmbientScale` 1, AO 0.5) | 0.202 | **−0.236** |
| `AmbientScale` = 0.5 | 0.271 | −0.167 |
| `AmbientScale` = 0 | **0.401** | −0.037 |
| `AmbientOcclusionStrength` = 1.0 — the knob's CEILING | 0.241 | −0.197 |
| `AmbientOcclusionStrength` = 0 — its other end | 0.178 | −0.260 |

and the amplitude, mean channel delta over the band: `AmbientScale` 1→0 is **62.26 / 255** at the horizon
and **53.33 / 255** at mid, the largest of any knock-out except deleting the clouds.

**The occlusion knob cannot reach it**, which is why this has survived a calibration programme: at its
maximum it recovers 0.039 of a 0.236 gap — **17 %** — where the term itself is worth 0.199, or 84 %.

**And the way it fails is the mechanism's own signature, which is the strongest single piece of evidence
in this document.** The same knob at the same maximum recovers **34 %** of the gap at the sunward zenith
(0.296 → 0.358 against 0.479) and only **17 %** at the horizon. At 42° the camera looks at one nearby
body, whose own `Profile` is high, so a term built from the sample's local depth does bite. At 7° the ray
crosses a whole deck: each individual sample's `Profile` is unremarkable while the accumulated cloud
between it and the sky is enormous, and a local term has nothing to say about that. **The knob works
exactly where the occluder is the sample's own body and fails exactly where the occluder is other cloud**
— which is the difference between the quantity we have and the quantity Unreal builds a second volume
for.

**The other three lighting parameters were measured and are not the story.** `MultiScatterOctaves` 3→1 is
the second-largest amplitude in the table (31.60 / 255) but it moves contrast the WRONG way — 0.395 →
0.227 — so the octaves are earning their place, exactly as §QT's refusal said. `PhaseBlend` at either end
of its range moves contrast by −0.046 / +0.025, and the +0.025 end is the near-isotropic lobe, which is
already where Unreal's shipped instance sits. `AmbientOcclusionStrength` is #2 above.

### 4. Density and transmittance — `DensityScale`, `ExtinctionScale`, `StopTransmittance`. **REFUTED**

An early march exit would give a uniformly white fill. Raising the exit threshold by a factor of ten moves
**1.16 / 255** with a maximum of **7**, and contrast 0.395 → 0.391. The march is not exiting early.

Halving and doubling `ExtinctionScale` around the shipped 8 gives contrast **0.389 / 0.395 / 0.400** —
a total span of 0.011 across a factor of four, against a gap of 0.084. The parameter is on a plateau; it
is not where the missing range is.

### 5. Reconstruction — half resolution plus temporal resolve. **REFUTED as a cause; a residue named**

Under a **fixed** camera the reprojection is exact by construction, so this was measured under motion, as
the protocol requires. Two `--camera-to` translations along the view direction across the 90 warm-up
frames, each compared against a **static** shot taken from the pose the path ends at — so everything the
two frames differ by is the resolve's response to the camera having moved:

| motion over 1.5 s of `--play` | pixels differing | max | mean Δ/255 |
|---|---|---|---|
| 600 m (≈ 400 m/s) | 31.8 % | 13 | **0.40** |
| 6 000 m (≈ 4 km/s) | 75.8 % | 89 | 2.46 |

At a speed a camera might plausibly travel the reconstruction is wrong by **0.40 of a level out of 255**,
which is below the unit this programme measures in and agrees with §PLAY's independent finding of 0.43 for
world motion. At ten times that speed it is 2.46 and visible. The quality tier — the other half of the
hypothesis — moves **1.29 / 255** from High to Low.

**And the convergence window is not hiding anything**: 3 frames against 90 leaves `E1` at 0.00200 vs
0.00202.

**The residue, named because it is the one thing I could not knock out.** Over cloud pixels only, the
finest-scale energy of our frames is about half the reference's at the two higher elevations —
0.00165 and 0.00199 against 0.00318 and 0.00375 — and a 4x downsample-and-upsample round trip destroys
less of ours than of the reference's (58.7 % and 61.0 % surviving, against 45.7 % and 53.8 %), which is
the signature of an image that was produced at a lower resolution than it is displayed at. The march runs
at a **quarter** of the framebuffer (`VolumetricCloudRenderer.cpp:43, :1094`). **I could not A/B that**,
because changing it is a C++ edit and this task owns no engine source. §8 says what would settle it.

### 6. Tonemapper — ACES, and the recorded 0.333-vs-0.482 residual. **REFUTED, and the brief's premise is stale**

The brief lists the residual as "still open in `CALIBRATION.md`". `CALIBRATION.md:245-248` retired it in
§T-ACES: *"That gap was the tonemapper, and T-ACES closed it without touching a single line of cloud code
... It is left recorded because being wrong about WHERE a discrepancy lives is the expensive mistake this
document exists to prevent."* The line the brief quotes is the paragraph that was replaced, not the
current state. Measured rather than argued, on `mid_away`:

| | contrast | mean | p95 |
|---|---|---|---|
| **ACES** (shipped, `Tonemapper: 0`) | **0.395** | 0.533 | 0.719 |
| Reinhard | 0.265 | 0.539 | 0.665 |
| reference | 0.479 | 0.609 | 0.800 |

ACES is worth +0.130 of contrast over the alternative and is already the shipped operator. Switching
operators can only make it worse.

**Exposure is the other half of the same question and it is refuted too**, which matters because "the
frame is just exposed low" is the obvious next objection:

| Exposure | mean | p05 | p95 | contrast |
|---|---|---|---|---|
| 0.18 | 0.436 | 0.248 | 0.621 | 0.373 |
| **0.26** (shipped) | 0.533 | 0.324 | 0.719 | **0.395** |
| 0.35 | 0.612 | 0.392 | 0.785 | 0.393 |
| 0.50 | 0.715 | 0.493 | 0.894 | 0.402 |
| reference | 0.609 | 0.321 | 0.800 | **0.479** |

Exposure 0.35 lands the mean on the reference's 0.609 and `p95` within 0.015 of its 0.800 — and contrast
stays at 0.393. **Over a factor of 2.8 in exposure the contrast moves 0.029 and never approaches 0.479.**
The missing range is in the scene, not on the curve.

---

## 5. The relation that is not written down anywhere — and that turned out NOT to be the cause

Stated separately because it is the kind this project has been bitten by four times, and because the
suite currently asserts the safe half of it:

> **The march's search chord is `segment / 32`, and the segment grows as roughly `layer thickness /
> sin(elevation)`** (exactly, the shell chord in §4.2, which at 7° is 28.98 km against the flat-earth
> 30.2 km).
> `CloudFinestResolvableChordKm` — the 125 m every calibration is quoted against — is the value at a
> segment of exactly 4 km, and is a FLOOR rather than the working figure. At the protocol's own three
> elevations the chord is **168 m, 273 m and 906 m**, and the erosion whose whole range displaces the
> surface by 180 m is under it at two of the three.

`Desert/Tests/Engine/CloudField.TheErosionsWaveIsShorterThanABodyAndCoarserThanTheMarchsOwnChord` asserts
the 235 m wave against 125 m and passes. **The assertion is true and the sky it protects is not the sky
that is rendered.** A relation test that pins the best case is the shape of test this document's own
§2.3.1 lineage warns about, and it would be worth an elevation term.

**Caveat, and it is a big one: I tested this relation on the frame and it did not behave as the arithmetic
predicts.** Doubling `MaxSteps` to 512 halves every chord in the table above — 84 / 136 / 453 m, putting
the erosion's 180 m travel comfortably above it at all three elevations — and:

| test, `MaxSteps` 256 → 512 | contrast | `E1` | mean Δ/255 | max |
|---|---|---|---|---|
| `mid_away` (chord 273 → 137 m) | 0.395 → 0.391 | 0.00202 → 0.00201 | **0.92** | 9 |
| `horizon_away` (chord 906 → 453 m — the biggest predicted effect) | 0.202 → 0.201 | 0.00332 → **0.00322** | **2.30** | 17 |

Halving the chord at the elevation where the erosion is furthest under it makes the frame **slightly less
detailed**, not more. Halving `DetailTileSize` instead moves 1.29 and 1.11 and leaves `E1` at 0.00204 and
0.00334; both together 1.58.

**And the one thing that DOES raise `E1` is the knob that raises nothing else.** `DetailStrength` 1.00 at
the horizon takes `E1` from 0.00332 to **0.00354** (+6.6 %) — the largest fine-scale gain of any row —
while moving contrast by 0.006 and the frame by 2.23 / 255. The erosion is the only source of surface,
it is at its ceiling, and its ceiling is worth 6.6 % of a quantity that is short by a factor of two.

So the chord is *a* ceiling on what the erosion could deliver, but **it is not the binding one** —
something else limits the surface first, and §4.5's residue (the quarter-resolution trace) is the
remaining candidate I could not eliminate. **I had this relation written up as the answer before I
rendered the test.** It is recorded at full length because the arithmetic is correct and compelling and
the next person will otherwise derive it again, believe it, and spend the phase on `MaxSteps`.

---

## 6. The frames

All under `Docs/Clouds/Shots/`, `Clouds_Protocol`, camera `0,200,0`, `--play`, 1280x766, Debug.

| frame | what it is for |
|---|---|
| `P0_base90_{zenith,mid,horizon}_{away,sun}.png` | the six points, 90 frames — the base every number above is against |
| `P0_base3_{zenith,mid,horizon}_{away,sun}.png` | the same six at 3 frames — the check that convergence hides nothing |
| `P0_ko_amb0_horizon_away.png` | hypothesis 3's knock-out at the point where it owns 84 % of the gap |
| `P0_ko_amb50_horizon_away.png` | the middle of the ambient ladder — the frame behind "scaling it trades highlight for shadow" |
| `P0_ko_ao1max_horizon_away.png` | the occlusion knob at its CEILING, still white popcorn — verdict 3's #2 |
| `P0_ko_amb0_mid_away.png` | the ambient knock-out where the deck is close and the shading is legible |
| `P0_ko_det0_mid_away.png` | the erosion switched OFF — the raw lump field, smooth ellipsoids, no surface at all |
| `P0_ko_det1max_mid_away.png` | the erosion at the TOP of its range — beside `det0` and the base, the argument for "saturated" |
| `P0_ko_steps512_mid_away.png` | the march at twice the resolution, which changes nothing — §5's refutation |
| `P0_motion_end_mid_away.png`, `P0_static_end_mid_away.png` | hypothesis 5: arrived by moving, against always having been there |

**Look at three of them together and the ranking is legible without any of the numbers**:
`P0_ko_det0_mid_away.png` (no erosion) is a pile of smooth ellipsoids; `P0_base90_mid_away.png` (shipped)
is the same pile with a little granularity; `P0_ko_det1max_mid_away.png` (erosion at maximum) is
indistinguishable from the shipped one. Then `P0_ko_amb0_horizon_away.png` against
`P0_base90_horizon_away.png` is a different sky.

The reference PNGs are git-ignored by `.gitignore:112` and are not committed; they are in the working tree
of anyone who has them, and the rectangle and the row are printed above so the measurement can be repeated.

---

## 7. Measurements that did NOT support a conclusion

Recorded in full because a disproof is worth what a proof is, and because three of these are instruments
somebody will otherwise build again.

**Silhouette-crossing width in pixels.** `EdgeStat` — Otsu-split, then the width of every monotone
crossing of the sky↔cloud transition band along each row. Ours: median 14 / 10 / 17 / 96 / 37 / 22 px at
the six points. Reference: 29 / 47 / 48 px. **Ours is SHARPER than the reference at four points and much
softer at one.** The number is dominated by how far away the cloud is, and our clouds are nearer.
Unusable across subjects; usable as a within-renderer knock-out statistic, which is how §4.2 uses it.

**Multi-scale structure function over the whole band.** `ScaleStat` — mean |ΔL| at strides 1…64. The
ratio `E1/E32` is **0.061–0.103 for ours and 0.063–0.105 for the reference**, i.e. the two skies have the
same spectral SHAPE. Our absolute energies are lower at every scale, in the same proportion as the
contrast deficit. There is no separate "our clouds are blurrier" effect over the whole frame; §4.5's
finding appears only when the measurement is restricted to cloud pixels, and I would have concluded the
opposite from the whole-band number alone.

**Component shape and isoperimetric ratio.** `ShapeStat`. Ours 1.85–13.88, reference 5.72–16.74 — but the
statistic swings by a factor of seven with the number of connected components, and the frames have between
1 and 26. It is measuring how many separate clouds are in shot. Refused as evidence.

**Vertical shading asymmetry — and this one refuted my own reading of the frames.** `VertShade` — top
third against bottom third of every vertical run of cloud, area-weighted. I expected our decks to show
less top-to-bottom shading than the reference, because the base frames have no dark cloud bases. They
show **more**: ours +0.084 / +0.038 / +0.018 at the away points against the reference's +0.001 / +0.032 /
−0.037. The reference sheet is thin and lit through, so it has no top-and-bottom either. **The visual
reading was right about our frames and wrong about the comparison**, and the ranking above therefore
rests on the ambient knock-out's contrast movement rather than on this.

**Rim-to-core shading ramp.** `DepthShade` — Chamfer distance into the silhouette, mean luminance per
depth decade. Ours −0.061 to −0.178, reference −0.147 to −0.177; the sign is negative in both (a thin rim
blends with the sky behind it). Ours is shallower on average but overlaps the reference's range, and the
deep buckets are dominated by whichever single large body happens to be in shot. Suggestive, not
decisive.

**Saturation.** Ours 0.084–0.176 against the reference's 0.192–0.297 looks like a large discrepancy and is
**mostly a coverage difference rather than a renderer one**: `Coverage` 0.762 → 0.40 takes `mid_away`
from 0.176 to **0.287**, past `UE_mid`'s 0.192, and `horizon_away` from 0.085 to 0.151 against
`UE_horizon`'s 0.286 — so it explains the mid point entirely and the horizon about half way. With the
clouds switched off the clear sky measures sat **0.510**, so the blue itself is not washed out; there is
simply more white in front of it. This is a scene-authoring number and it is out of Р0's scope, but the
horizon residue is real and belongs to whoever re-authors the scene.

**The march step.** §5's caveat. Doubling `MaxSteps` buys nothing measurable.

### ⚠️ And one incidental finding about the INSTRUMENT, which is not about clouds at all

While tidying up I deleted a temporary `.desce` while a capture that named it was still queued. The
editor logged `Scene file does not exist: Resources/Assets/Scenes/P0TMP_cov40.desce`
(`Editor/Source/EditorLayer.cpp:2565`), **returned, rendered the default empty scene anyway, wrote a
1280x766 PNG and exited 0.** The frame measures `mean 0.238 / p05 0.235 / p95 0.239 / contrast 0.004` —
a flat grey rectangle that a table of numbers will happily accept as a row.

That is contract §1.4's silent fallback wearing a capture tool's clothes, and it is how a knock-out table
lies. **I caught it only because a uniform frame is obvious; a missing `--scene` on a scene that differs
subtly from the default would not have been.** One row of my own table was affected, it is deleted, and
every other number in this document was checked against its own log for the same line — none has it.
Recorded here because the whole programme measures through this flag. `--shot` with a scene it could not
load should fail rather than photograph something else.

---

## 8. What would change the answer, and what I could not measure

**What would change the answer.**

* **A frame from Unreal of a NEAR cumulus deck**, shot from a ground camera under a congestus layer at the
  three protocol elevations. It costs somebody an afternoon in the editor and it would convert five of the
  seven instruments in §7 from unusable to decisive. Every shape and frequency comparison in this document
  is weak for one reason, and this is the reason.
* **Any measurement showing that the ambient term is occluded somewhere I did not look.** I read the march,
  the lighting header and the payload packer; if occlusion enters through the sky's own distant-light
  texel rather than through the cloud march, #1 changes shape.
* **A cost number for a sky-light occlusion volume.** The ranking says what the largest discrepancy is; it
  does not say the fix is affordable. Unreal spends a second volume on it. If that volume prices out, the
  answer becomes "the largest discrepancy is one we have decided not to pay for", which is a different
  conclusion from the one above and an equally legitimate one.

**What I could not measure, and why.**

* **The trace resolution.** §4.5's residue — half the reference's fine-scale energy over cloud pixels, and
  a frame that survives a 4x round trip better than the reference does — points at the quarter-resolution
  march as the remaining candidate for the smooth surface. Changing it is an edit to
  `VolumetricCloudRenderer.cpp`, which this task does not own; every other knock-out in this document was
  an authored field or nothing. **One rebuild with `HalfExtent` applied once instead of twice at
  `VolumetricCloudRenderer.cpp:1094` would settle it in two frames**, and it is the single most valuable
  follow-up measurement in this document.

  > **ANSWERED, 2026-08-28, by Р6 — and the answer is no.** Two corrections and one result.
  >
  > The two applications are **not** at one site and are **not** an accident: `:1094` produces the HALF
  > extent the composite upsamples from, and `EnsureTraceTargets` applies it again at **`:915`** for the
  > QUARTER extent the march writes. Removing either does not undo a doubling — it raises the whole
  > pyramid an octave. The design was already stated twice in the tree.
  >
  > Р6 then built the strongest form of this hypothesis — trace at half, reconstruction at **native**, so
  > every displayed pixel gets its own traced ray and no resolution deficit remains — and it closes
  > **1.3 % and 3.4 %** of a gap short by about half. Contrast moves under 0.001 at all six points; the
  > frame moves 0.12–0.51/255, *less than doubling `MaxSteps` moved it*, for **2.83×** the cost
  > (march 12.695 → 35.907 ms; 71.2 MiB at 1920×1080, over D-9's whole subsystem budget by itself).
  >
  > **The signature in §4.5 was misread, and that is the useful part.** Our frames surviving a 4× round
  > trip better than the reference barely moves at native resolution either (58.7 → 58.2 %, 60.8 →
  > 59.4 %, against the reference's 45.7 / 53.8 %). It was reading the smoothness of the cloud itself,
  > not the sampling grid. What limits the surface is still open, and it is not sampling — it needs a
  > mechanism that ADDS surface. See the recorded refusal at `VolumetricCloudRenderer.cpp:36-42`.
* **Anything costing time.** By the lead's ruling, and the machine's load average ran from 9 to 88 while
  these frames were taken. No conclusion here depends on a slope.
* **The interactive editor.** Everything is headless `--shot`; the temporal stage was exercised with
  `--camera-to` rather than by a person flying the camera, and a human at the controls may see a
  reconstruction artefact that a 90-frame path does not produce.

---

## 9. What Р4 would be, if the ranking holds

Not a proposal — Р0 does not get to design the fix, and D-21 says the look is closed by what Р0 NAMES.
But the ranking points somewhere specific enough that the ⬛ show can be about a decision rather than an
exploration:

1. **The sky-light term needs an occluder that knows what is above the sample.** The largest single number
   in this document, the one knob that exists cannot reach it, and the source already says why.
2. **The erosion needs its ceiling raised or its mechanism changed** — but NOT before somebody finds what
   actually limits the surface. At the top of its authored range it displaces the cloud by less than the
   march's own search chord at two of the three elevations, and yet halving that chord releases nothing.
   **§8's one rebuild is the cheapest next measurement in the programme** and it should happen before any
   of this is scoped.
3. **Nothing should be spent on the tonemapper, the exposure, the extinction, the stop threshold, the
   quality tier or the temporal resolve.** Each was measured, and the largest of them is worth 1.29 of
   255 — a quarter of the amount by which the ambient alone moves the horizon frame.

And one thing that is **not** Р4's and should be filed where it belongs: the saturation deficit is
`Coverage`, and `Coverage` is the artist's. §7 has the number.
