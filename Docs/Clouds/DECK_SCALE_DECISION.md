# The sky was a ceiling: the measurement, the mechanism and the decision

Written 2026-08-18 by the teamlead, after the owner reported "the sky sits too close to the ground" and
said it reproduced on every scene. It did. This document is the finding, the two mechanisms behind it,
and the binding decision — so the implementation is executed against numbers rather than against taste.

---

## 1. What was measured

The dominant coverage cell is `WeatherTileSize / 8`. Across all eight shipped presets it sits at
**0.92 to 1.29 times the layer's mid altitude**, so one cloud directly overhead subtends 49 to 66
degrees and a ground observer sees four to six clouds across the whole sky above 20 degrees:

| preset | bottom | thickness | cell | overhead | clouds above 20° |
|---|---|---|---|---|---|
| Clear | 2000 m | 1526 m | 2748 m | 52.9° | 5.5 |
| Fair Weather | 1500 | 1482 | 2519 | 58.7° | 4.9 |
| **Partly Cloudy** | 1500 | 1609 | 2976 | **65.7°** | 4.3 |
| Summer Cumulus | 900 | 1679 | 2015 | 60.2° | 4.7 |
| Stratus | 600 | 700 | 870 | 49.2° | 6.0 |
| Overcast | 900 | 1409 | 1832 | 59.4° | 4.8 |
| Storm | 700 | 9000 | 4762 | 49.2° | 6.0 |
| Cirrus | 8000 | 1200 | 7876 | 49.2° | 6.0 |

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
edge — half the deck's screen area on the horizon view.

**So the two defects are independent, and the second one exists today.** Today's 3 km cells hide it as a
band of torn paper above the horizon. Shrink the cell and it becomes the frame.

## 3. The relation nobody had written down

Three scale relations already exist — `CloudWeatherScale` (tile vs altitude), `CloudMarchScale` (stride
vs thickness), `CloudLayerAspect` (cell width vs thickness). None of them constrains the quantity that
decides whether a distant cloud is a cloud at all:

> **A coverage island must stay larger than the finest erosion the march can still carry at the distance
> the deck is drawn to.**

Island size is about `WeatherTileSize / 16` (an island is roughly half the FBM's base period at the
shipped coverages). The erosion the march carries is `4 x stride` — `CloudNyquistWeight`'s own "fully
carried where `S <= featureSize / 4`". Two consequences follow, and they are the decision below:

1. **A floor under the layer's altitude.** Island size is proportional to the tile, and the tile is
   proportional to mid altitude. Requiring the island to survive to where the erosion dies puts a
   minimum on the altitude of any *broken* sky. At the shipped detail tiles and the High tier that floor
   is about **3.7 km** for the cumulus aspect — which is why UE's 5 km default is where it is.
2. **The deck must end where its own detail ends.** Not at the planet's geometric horizon.

**Applicability, stated rather than assumed.** A *sheet* has no islands. Above roughly Coverage 0.90 the
coverage field is connected, there is no isolated blob to read as a box, and this relation is vacuous.
Stratus (0.90), Overcast (0.95) and Storm (0.98) are sheets; the relation does not govern them and must
not be applied to them.

## 4. The decision

**D1. `CellsOverhead` 3 -> 4**, in `CloudWeatherScale.hpp` and its `CloudGeometry.glslh` mirror.
Measured on frames at three elevations: a cloud overhead goes from 65.7 to 37.9 degrees and the zenith
and mid-sky read as a cumulus field with depth instead of a ceiling.

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

> **Corrected during implementation.** This clause originally claimed all four land at exactly 1.333x the
> new derived tile, inside the shipped `[0.7, 1.6]` band. Three do — Stratus 1.33334, Storm 1.33333,
> Cirrus 1.33333 — because their tiles *were* the derived tile at three cells overhead. **Overcast lands
> at 1.66199x and leaves the band**: its tile was authored, at 1.2465x what its own altitude asks for,
> and 1.2465 x 4/3 = 1.66199. Resolved not by widening a measured band and not by exempting a row by
> name — either would be content baked into a mechanism — but by giving `CloudWeatherTileIsPlausible`
> the same applicability clause `CloudDeckIsResolvable` carries: above `kCloudConnectedCoverage` there
> are no discrete cells to count, the band's two measured failures (2.5x empties the zenith, 0.63x walls
> the horizon) cannot happen to a blanket, and the relation is vacuous. Overcast is admitted as a sheet,
> as every future sheet will be. Note Cirrus is **not** a sheet (Coverage 0.66) and needs no exemption —
> it passes the band on its own.

Re-deriving them was computed and rejected
with numbers: it puts Stratus at a 4556 m tile (below the field's own 5000 m minimum) and 1.50 search
samples at Low, Overcast at 2.00, and Storm at 2591 m of depth against `kCloudDeepConvectionThickness`'s
6 km — three broken rows in exchange for an angular improvement on species that have no discrete clouds
to make angularly smaller.

**D4. `CloudAutoFadeEnd` stops returning the geometric horizon.** It becomes the nearest of: where the
deck's own erosion stops being carried, and `MaxViewDistance`. Measured on the fixed geometry, this
turns the wall of cubes into atmosphere at a cost of **6.57-9.33 ms/frame against an unchanged baseline
that itself spread 3.93-8.97 ms on this shared machine** — inside the noise floor. Frames: RMS delta
9.45 at the horizon, 6.04 at mid, **0.24 at the zenith** (max 3 grey levels — untouched).

**D5. Adjunct, free: the erosion tracks the stride** instead of switching off —
`lodTile = max(DetailTileSize, 4 * stride * 5.12)`. Same fetch count, one `max()`, bit-identical wherever
the authored tile is resolvable, and it cleans the band between where the fine detail dies and where the
deck now ends. Cost measured inside the baseline noise.

**D6. Refused, with the numbers: spending steps in the far field.** Growth 0.001, max stride 200 m, 512
steps costs **13.70-14.80 ms** — the only variant whose runs do not overlap any other's range, 1.6x the
baseline's slowest run and 3.5x its fastest — and it buys a better-marched cube, because the silhouette
is a property of the density field and not of the sampling. Do not build it.

## 5. Known, recorded, not fixed here

- **The hatching inside a box is a second and separate defect.** It is buffer-locked, not world-locked:
  autocorrelation of the high-passed band gives a correlation length of exactly one cloud-buffer texel at
  both half and full resolution, and halving the buffer halves the period. The temporal resolve cuts its
  variance by 57% — it hides it, it does not cause it. Out of scope for this change; write it down rather
  than absorb it.
- **The fade end is now tier-dependent** (the erosion carry distance is a function of the step schedule).
  That is correct and is the same argument `CloudNyquistWeight` already makes for reading the step rather
  than authoring a distance — but Low's horizon comes visibly near, and that wants either a floor or an
  explicit refusal recorded at the site.
- **Small coverage cells at low altitude remain undrawable.** The C_N8 probe (1500 m base, 10.1 km tile)
  is boxy from 10 km out and D4 does not rescue it. If a low broken sky is ever wanted, the thing to write
  down is the island-against-stride relation of §3, not another fade.
