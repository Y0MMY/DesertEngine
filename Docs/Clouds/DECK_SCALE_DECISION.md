# The sky was a ceiling: the measurement, the mechanism and the decision

Written 2026-08-18 by the teamlead, after the owner reported "the sky sits too close to the ground" and
said it reproduced on every scene. It did. This document is the finding, the two mechanisms behind it,
and the binding decision — so the implementation is executed against numbers rather than against taste.

> **Revised 2026-08-18, after a three-way adversarial audit of what shipped.** The document was partly
> untrue when the commit landed. **D4 and D5 are withdrawn** and the shader files are back at their
> pre-commit content; §3's altitude floor was wrong by a factor of two and the justification for the
> 5 km move has been rewritten onto the measurement that survives; §1/§4's angular figures conflated the
> coverage cell with the visible cloud and are relabelled. What still stands: **D1, D2, D3 and D6**.
> Each correction is marked at the section it belongs to rather than collected at the end, because a
> document whose corrections live in an appendix is read as if it were right.

---

## 1. What was measured

The dominant coverage cell is `WeatherTileSize / 8`. Across all eight shipped presets it sits at
**0.92 to 1.29 times the layer's mid altitude**, so one coverage CELL directly overhead subtends 49 to
66 degrees and a ground observer sees four to six cells across the whole sky above 20 degrees:

| preset | bottom | thickness | cell | cell subtense overhead | cells above 20° |
|---|---|---|---|---|---|
| Clear | 2000 m | 1526 m | 2748 m | 52.9° | 5.5 |
| Fair Weather | 1500 | 1482 | 2519 | 58.7° | 4.9 |
| **Partly Cloudy** | 1500 | 1609 | 2976 | **65.7°** | 4.3 |
| Summer Cumulus | 900 | 1679 | 2015 | 60.2° | 4.7 |
| Stratus | 600 | 700 | 870 | 49.2° | 6.0 |
| Overcast | 900 | 1409 | 1832 | 59.4° | 4.8 |
| Storm | 700 | 9000 | 4762 | 49.2° | 6.0 |
| Cirrus | 8000 | 1200 | 7876 | 49.2° | 6.0 |

**Corrected: the CELL is not the CLOUD, and the original text used the two words interchangeably.** A
covered cell occupies only part of its own period — the rest is the gap that makes the sky broken — so
the visible island is smaller than the cell that carries it. Measured from a straight-up frame at
14.8 px/degree, the islands subtend **13.5 to 22 degrees**, i.e. **0.35 to 0.6 of the cell**. The
column above is therefore cell subtense and is labelled as such; the ratio between the two is measured
here and nowhere else in this repository, and nothing in the code depends on it.

Two clouds cover the sky from horizon to horizon. That is the ceiling, and it is not an accident:
`CloudAutoWeatherTileSize` targets exactly **three cells across the disc above 20 degrees**, and
`CloudGeometry.glslh` says why in as many words — *"Three cells is the smallest count that can show
cloud, gap and cloud overhead."* A **minimum** was adopted as the **target**.

For reference, UE ships `LayerBottomAltitude` 5 km and `LayerHeight` 10 km. Our whole family lives at
0.6 to 2.0 km.

## 2. Why the obvious fix had already been rejected — and why that rejection was right about the frame
   and wrong about the cause

Shrinking the tile was tried before (the 30-degree / 15 km trial, `CloudGeometry.glslh:109-114`) and
rejected: *"the horizon becomes a wall of cells small enough that the step schedule out there samples
each of them four times — a boiling mush."* Rendering it again reproduces the wall exactly.

The recorded cause was wrong. A converged ground-truth probe — 768 uniform samples over the shell, no
state machine, no dither, cheap density only — **renders the same boxes**. Every march hypothesis dies
there, and so do all of these, each knocked out on its own and measured against a byte-identical repeat:

| knocked out | result |
|---|---|
| the march itself (ground-truth probe) | boxes present |
| budget exhaustion | zero rays exhaust budget |
| temporal resolve | boxes unchanged |
| Half → Full resolution | boxes unchanged |
| dither / jitter | boxes unchanged |
| coarse skip, empty-sample count | boxes unchanged |
| distance softening | boxes unchanged |
| weather-map texel (65.9 m vs a 2 km box) | disproved arithmetically |
| **detail erosion** | **near clouds become boxes too** |

**The mechanism.** The far-field density is `coverage(x,z) x verticalProfile x baseShape`. Coverage is a
**2-D** field, so the silhouette is a vertical extrusion — vertical sides. The profile puts every cell
base within 3.3% and every cell top within 16.5% of the layer — flat base, flat top. Measured on a
5 km / 2.28 km layer: boxes 2.0 km wide by 2.3 km tall. A cube. **The only thing that has ever made a
coverage island read as a cloud is the detail erosion**, and `CloudNyquistWeight` switches that off by
design where the march cannot resolve it: with a 4 km detail tile at High, the coarse channel pair
(781 m) is gone by **47 km**. Past that the density carries the noise's mean — the raw box.

Nothing removed the result either: `CloudAutoFadeEnd` returns the layer's *geometric* horizon,
`sqrt(2R*top)` = **304 km**, while the march is clipped at `MaxViewDistance` = 150 km. The dissolve
weight at the clip was measured at **0.597**, so 60% of a deck of boxes was drawn right up to a hard
edge — half the deck's screen area on the horizon view. **That measurement stands and the defect is
still open**: D4 was the attempted fix, it is withdrawn, and `CloudAutoFadeEnd` still returns the
geometric horizon today. Whatever replaces it must not be a function of `DetailTileSize` — see D4.

**So the two defects are independent, and the second one exists today.** Today's 3 km cells hide it as a
band of torn paper above the horizon. Shrink the cell and it becomes the frame.

## 3. The relation that was proposed here — WITHDRAWN — and what actually justifies the 5 km move

**This section originally proposed a fourth scale relation** ("a coverage island must stay larger than
the finest erosion the march can still carry at the distance the deck is drawn to"), shipped it as
`CloudIslandScale.hpp`, and rested D2's altitude move on it. **The header has been deleted and the
relation is withdrawn.** Three things were wrong with it and each is fatal on its own:

- **Its central constant was a guess.** `kCloudIslandsPerCell = 2.0f` asserted that an island is half a
  cell at the shipped coverages, giving an island fraction of 0.0625. Baking the real weather field and
  measuring connected components gives a fraction that rises **monotonically 0.028 → 0.88 as Coverage
  goes 0.10 → 0.90**, and equals 0.0625 only near Coverage 0.37. The function took neither `Coverage`
  nor `CoverageContrast`, so its verdict on **6 of the 13 broken-sky layers in the repository was an
  artifact of the constant**, not a property of the sky. A relation whose constant is a guess is worse
  than no relation: it launders the guess as a measurement.
- **Its numeric consequence below was wrong by a factor of two** — see the next paragraph.
- **The half of it that reached the GPU deleted the sky.** See D4, withdrawn.

**The altitude floor was 1.86 km, not 3.70 km.** The floor is **linear in `DetailTileSize`** and 3.70 km
was computed at a 4000 m tile — which is what the *scenes* ship, not the presets. The preset table ships
**1250 / 1500 / 2000 / 2500 / 3000 m**, and the four cumulus rows the decision moves ship **2000 m**. At
the table's own numbers the floor is therefore **1.86 km**, which the 1500 m rows already all but clear.
**The 5 km move does not follow from the island relation.** It never did.

**What does justify it, and is measured:** `CloudMarchScale`. At four cells overhead, `CloudWeatherScale`
and `CloudLayerAspect` together admit exactly one (thickness, tile) pair per base altitude, and the pair
at a 1500 m base is a **684 m** layer. A 684 m layer gets **1.99** empty-space search samples across
itself at the Low tier against a bound of **four** — a march that steps over its own cloud depending on
the ray's dither phase, and a bound with no tolerance in it at all. At a 5000 m base the same solution is
**2279.6 m** thick and gets **4.15**. That is the whole argument for the altitude, it is a relation this
repository already owns and tests, and it survives.

**What is still not known.** The wall at 0.63x the derived tile is real and reproduces; its mechanism is
NOT established. The step-schedule explanation was disproved by the ground-truth probe in §2 and nothing
replaced it. Until something does, no relation should be written down that claims to explain it.

## 4. The decision

**D1. `CellsOverhead` 3 -> 4**, in `CloudWeatherScale.hpp` and its `CloudGeometry.glslh` mirror.
Measured on frames at three elevations: a coverage CELL overhead goes from 65.7 to 37.9 degrees and the
zenith and mid-sky read as a cumulus field with depth instead of a ceiling. (Cell, not cloud — see §1;
the visible island is 0.35 to 0.6 of that.) **Stands.**

**D2. The four cumulus rows move to UE's altitude band** — Clear, Fair Weather and Partly Cloudy to
**5000 m**, Summer Cumulus to **4000 m** — and their thickness and tile are re-derived as the
simultaneous solution of `CloudWeatherScale` and `CloudLayerAspect` at the new constant. This is the
change that fixes the ceiling *without* shrinking the island below what the march can draw: it buys
angular size by moving the layer, not by shrinking the cloud.

Derived, and every row checked against all four relations:

| preset | bottom | thickness | weather tile | cell | island | overhead | search samples Low/Med/High/Ultra |
|---|---|---|---|---|---|---|---|
| Clear | 5000 | 2357.8 | 33952.9 | 4244 | 2122 | 37.9° | 4.27 / 9.65 / 11.11 / 19.31 |
| Fair Weather | 5000 | 2531.6 | 34430.4 | 4304 | 2152 | 37.9° | 4.53 / 10.24 / 11.77 / 20.47 |
| Partly Cloudy | 5000 | 2279.6 | 33737.9 | 4217 | 2109 | 37.9° | 4.15 / 9.39 / 10.80 / 18.78 |
| Summer Cumulus | 4000 | 3207.6 | 30792.5 | 3849 | 1925 | 37.9° | 6.41 / 14.52 / 16.72 / 29.03 |

All four clear `CloudMarchScale`'s four-sample bound on **every** tier, including Low — which the
1.5 km-altitude versions of the same geometry do not (Partly Cloudy at 1500 m and the same constant
gives 1.99 samples at Low). The altitude is what pays for the smaller angular cloud.

**D3. The four sheet/deep rows are NOT re-derived.** Stratus, Overcast, Storm and Cirrus keep their
authored geometry. Changing the constant moves the *derived* tile they are compared against by 0.75x.

> **Corrected twice. The second correction is the one that is true.**
>
> This clause originally claimed all four land at exactly 1.333x the new derived tile, inside the shipped
> `[0.7, 1.6]` band. Three do — Stratus 1.33334, Storm 1.33333, Cirrus 1.33333 — because their tiles
> *were* the derived tile at three cells overhead. **Overcast lands at 1.66199x**: its tile was authored,
> at 1.2465x what its own altitude asks for, and 1.2465 x 4/3 = 1.66199.
>
> The commit resolved this with a coverage clause — `kCloudConnectedCoverage = 0.90`, above which the
> tile relation was declared vacuous. **That clause is removed.** It was not a mechanism, it was the
> Overcast row wearing one: measured percolation of the real coverage field reaches 90% of covered area
> at **Coverage 0.55-0.75** depending on density level, not at 0.90, and the header's own comment
> admitted the number was chosen because "0.90 is where the shipped Stratus row sits". It was also
> float-equality-critical — one ULP below 0.90 and Stratus fires two warnings.
>
> **The actual defect it was papering over.** `kCloudWeatherTileTolerance{Low,High}` are ratios *to the
> derived tile*, and the derived tile is proportional to `1 / kCloudWeatherCellsOverhead`. But the band's
> endpoints were **measured as absolute tiles on one layer**: on Clouds_UEShowcase, whose derived tile
> was 23.8 km at three cells, 15 km walls the horizon (0.63x) and 60 km empties the zenith (2.5x). D1
> rescaled every derived tile by 0.75x **and left the two ratios alone**, so the band silently moved by
> 4/3 relative to the frames that define it. Overcast did not leave the band; the band left Overcast.
>
> **The fix is to multiply both endpoints by 4/3** — `0.7 -> 0.9333`, `1.6 -> 2.1333` — and, because
> that is the whole point, to **derive them in code from the constant** rather than write two new
> literals: `CloudWeatherScale.hpp` now carries the band *as measured* plus the cells-overhead count it
> was measured under, and computes the live ratios from `kCloudWeatherCellsOverhead`. Change the count
> again and the band follows it exactly. Under the corrected band **all eight rows are inside** — the
> four derived cumulus at 1.000, Stratus/Storm/Cirrus at 1.333, Overcast at 1.662 — with **no preset row
> changed**, no clause, and no by-name exemption. `Clouds_ShadowsOnWorld` at 0.280 stays outside and
> keeps warning, exactly as before.
>
> **AND THE CONSEQUENCE THAT MATTERS MOST, RECORDED AS A DEFECT AND NOT AS A REMARK.** Under the
> corrected band a row solved **exactly** to the derivation sits at 1.000, and the band's low end — the
> **measured** horizon wall — is 0.9333. **The four cumulus presets ship 7.1% above a measured failure.**
> That is not a comfortable place to be, and it is not hypothetical: it is the band of hard-edged boxes
> above the horizon the owner can see on the cumulus scenes today, one twelfth of the way back from the
> tile at which the horizon was measured to degenerate. The derivation does not aim at the middle of the
> measured range; it lands just inside its bad end, and because 1.000 is inside 0.9333, **no test in this
> repository can go red about it** — the CloudMath suite now pins the 1.0714 headroom as a number so it
> is at least impossible to read the suite as saying the margin is fine. What would answer it is the
> unfinished work in §3: the mechanism behind the wall at 0.63x is not established, and until it is,
> moving the derivation up inside the band would be a number chosen to look better rather than a fix.

Re-deriving them was computed and rejected
with numbers: it puts Stratus at a 4556 m tile (below the field's own 5000 m minimum) and 1.50 search
samples at Low, Overcast at 2.00, and Storm at 2591 m of depth against `kCloudDeepConvectionThickness`'s
6 km — three broken rows in exchange for an angular improvement on species that have no discrete clouds
to make angularly smaller.

**D4. WITHDRAWN. `CloudAutoFadeEnd` derived from where the erosion stops being carried.** It was built:
`CloudAutoFadeEnd` returned the nearer of the erosion carry distance and `MaxViewDistance` instead of the
layer's geometric horizon. **The shader files are back at their pre-commit content.** What was tried,
what killed it, and the mechanism — recorded so nobody rebuilds it:

- **It deletes the sky on the scenes it was not measured on.** The fade end is proportional to
  `DetailTileSize`, and the scenes ship it anywhere from 1200 to 15000 m. Rendered: **`Clouds_Cirrus` is
  empty blue at the horizon; `Clouds_HeroVolumes` has nothing below ~20 degrees; `Clouds_TwoLayerSunset`
  stops mid-sky.** D4 was measured on exactly one row — the 4 km detail tile, High tier, cumulus.
- **Low and Medium were never rendered at all, and they are worse.** At Low the Cirrus preset keeps 15%
  of its cloud at the **zenith** and nothing above 45 degrees; Partly Cloudy is at 0.79 at the zenith.
- **It turns a look knob into a geometry knob, silently.** `DetailTileSize` is an artist slider with a
  200 m - 30 km range. At High, 200 m erases the layer from the camera outward. At Low, **614 m disables
  the fade entirely and 615 m erases everything** — a one-metre discontinuity with no warning anywhere.
- **It is expensive in the wrong place.** The schedule has no closed-form inverse, so it bisects; and the
  bisection is **per-segment, not per-ray** (`CloudMarchOneSegment`, up to 3 segments, plus a tail call),
  giving **36-126 `sqrt` per pixel before the first density sample** — to recompute a value the CPU
  already computes every frame in `VolumetricCloudRenderer.cpp` and throws away.
- **Nothing faded the shadow passes with it**, so a deck dissolved in view still casts full shadows.

**D5. WITHDRAWN. The erosion tracking the stride**, `lodTile = max(DetailTileSize, 4 * stride * 5.12)`.
It only existed to clean the band D4 opened, so it goes with D4. The claim that it was free was also
false: removing the early-out costs **2 texture fetches per shaded sample** over a band whose dissolve
weight is exactly 0 — "same fetch count" in the original text was wrong.

**D6. Refused, with the numbers, and the refusal STANDS: spending steps in the far field.** Growth 0.001,
max stride 200 m, 512 steps costs **13.70-14.80 ms** — the only variant whose runs do not overlap any
other's range, 1.6x the baseline's slowest run and 3.5x its fastest — and it buys a better-marched cube,
because the silhouette is a property of the density field and not of the sampling. Do not build it.

## 5. Known, recorded, not fixed here

Everything in this list is a defect somebody will otherwise find again from scratch. None of it is fixed
by this change and none of it should be absorbed into the next one silently.

- **The four cumulus rows sit 7.1% above the measured horizon wall.** The most important item on this
  list; stated in full under D3 above and in `CloudWeatherScale.hpp`. It is visible today as a band of
  hard-edged boxes above the horizon on the cumulus scenes, and no test can go red about it.
- **The hatching inside a box is a second and separate defect.** It is buffer-locked, not world-locked:
  autocorrelation of the high-passed band gives a correlation length of exactly one cloud-buffer texel at
  both half and full resolution, and halving the buffer halves the period. The temporal resolve cuts its
  variance by 57% — it hides it, it does not cause it.
- **The tops are flat and the silhouettes rectangular from above.** From 9 km looking down, the cell tops
  are a plane: the vertical profile holds every cell top within **16.5%** of the layer, so the density
  field has no top relief to render. Pre-existing and independent of anything decided here — but D2 put
  the layer at 4-5 km, which is where a flying camera looks *down* on it, so the move made a latent
  defect a visible one.
- **Small coverage cells at low altitude remain undrawable.** The C_N8 probe (1500 m base, 10.1 km tile)
  is boxy from 10 km out. Nothing here rescues it, and §3 no longer claims to know why.
- **Four scenes carry an off-by-one `Preset` ordinal.** `Clouds_Stratus`, `Clouds_Overcast`,
  `Clouds_Storm` and `Clouds_Cirrus` each name a preset one place off in the enum. Pre-existing since
  `4193b556` and **cosmetic only** — the serialized geometry in each file matches that file's NAME
  correctly, so what renders is right and only the Details dropdown reads wrong.
- **`Clouds_ShadowsOnWorld` authors a `WeatherTileSize` of 1000 m, below its own slider minimum of
  5000 m.** Pre-existing; a serialized value outside a declared `Range` is loadable but cannot be
  re-authored without jumping to 5000 m. It is also the one shipped layer outside the tile band, at
  0.280, and it warns about that on every load — correctly.
- **The presets and the scenes disagree about `DetailTileSize`**: the preset table ships 1250-3000 m
  (the cumulus rows 2000 m) and the cloud scenes ship 4000 m. Nothing relates the two, so a scene built
  from a preset silently doubles it. This divergence is what made §3's floor wrong by a factor of two.
- **The cumulus rows now sit at 4000-5000 m with no absolute-altitude assertion anywhere.** Every
  altitude constraint in the engine is a *relation* — tile vs altitude, thickness vs tile, stride vs
  thickness — and all three are satisfied at any altitude if the others move with it. Meanwhile
  `CloudLayerAspect.hpp` names species: a cumulus mediocris base is meteorologically **800-2000 m**, so
  the table now ships **altostratus altitudes under cumulus names**, and nothing catches it. The move is
  defensible against the march (§3) and indefensible against the species names, and only one of those two
  is written down in code.
