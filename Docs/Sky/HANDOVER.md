# Sky & clouds programme — state and what is left

Written 2026-08-18 at the end of a 27-merge programme, so a new session can start from facts rather
than from archaeology. `dev` is at **`cdf09cc5`**, every merge green on macOS + Windows Debug/Release.

---

## 1. One thing is IN FLIGHT and needs finishing first

**Cloud light parity — three items, work uncommitted in a worktree.**

    .claude/worktrees/agent-aa8c4f7c0551d7754   (branch worktree-agent-aa8c4f7c0551d7754, base dev @ 090e016d)

19 files are **staged and never committed**; two agents in a row died on it (one to a session limit,
one to a stalled stream), and the second one's transcript is gone. The tree contains a new
`Common/CloudAmbientColumn.glslh` and `Programs/Clouds/CloudAmbientColumnMap.shader`, plus edits to
`VolumetricCloudsComponent`, `AtmosphereEnv`, `CloudPayload`, `VolumetricCloudRenderer`,
`SkyboxRenderer`, `CloudGeometry.glslh`, `CloudRaymarch.shader` and four census/test files.

**Nobody has verified any of it.** The first job is to read `git diff --cached` and establish which
of the three items are actually implemented:

1. **Per-sample atmospheric sun transmittance** — the march tints its sun with ONE global colour, so
   a deep deck is lit as if every sample sat at one altitude. Per-sample `T_atm(altitude, sunDir)`
   from the sky's transmittance LUT replaces it. Component flag, default off like UE, bit-for-bit
   no-op when off.
2. **Clouds in the IBL bake** — `BakeProceduralSky`'s panorama has no clouds, so ground ambient
   carries a clear sky under a solid overcast. UE bakes clouds at ~80 samples, no jitter, no
   temporal. Must NOT spin up a second live SceneRenderer (see the one-SceneRenderer-per-frame rule);
   the decision was a dedicated low-budget bake path on the bake's own cadence.
3. **A vertical summed-density volume for cloud ambient** — the ambient column currently projects the
   SUN-space shadow map onto the vertical, which the audit measured as three seams: exact only for a
   flat uniform layer, an over-estimating sunset clamp, and **zero outside the shadow map's ~30 km
   half-extent**, which is always the far half of a 150 km shell. ~128x128x16 half-precision over the
   shadow map's footprint, refreshed on its cadence.

Anything half-finished must be completed or backed out — a partially wired feature is what the
contract forbids. The calibrated anchor for item 3 is the lit:shadow estimator in
`Docs/Clouds/Shots/README.md`; report before/after on the cameras it documents.

Note the base is behind `dev`: since then the layer geometry changed (`CloudLayerAspect` re-derived
every preset's thickness) and Coverage was re-authored on five scenes. Don't rebase — merge it.

---

## 2. Known defects, recorded and not fixed

| what | where | why it was left |
|---|---|---|
| Three hand-written scenes carry **metres-era gravity under a units stamp** — `Fog_Showcase` and `Sky_PhysicalShowcase` say `UnitVersion:1` but `Gravity:9.81`; `MainMenu` has no `Settings` block | scene files | the stamp is lying about them, but changing values is an *edit*, not a migration, and moves a frame |
| **Grass clump texture** baked 512x256 every scene and bound as `u_GrassClump`, which no shader declares any more | `BakeGrassClumpTexture` | not fully dead — the same image doubles as the cull compute's splat fallback, so removing it is a real change |
| `Collider` / `CharacterController` length fields **not marked `PROPERTY(..., Length)`** although Terrain/Camera/PointLight/SpotLight ones are | reflection | cosmetic today; it is the census the editor's unit-formatted fields use |
| A hero cloud at 13 km still reads **smoother than the deck** around it | voxel path | inherent to phase 1's vocabulary; the distance fade hands the frame back instead |
| **Storm base measures median 24/255** — dark but structured | `Clouds_Storm` | judged preset taste, not a defect. Revisit only if the owner disagrees |

---

## 3. The roadmap items nobody has started

From `Docs/Clouds/UE_VOLUMETRIC_CLOUDS_RESEARCH.md` §4 and `Docs/Clouds/NUBIS3_FULL_AUDIT.md` §3,
in the order they would pay:

1. **Type-as-LUT depth** — Wave 4 gave Cloud Type authored profile curves and per-cell heights. The
   audit's fuller form adds per-cell *bottom/top type indices* so neighbouring cells differ in
   species, not only in height. Cost M.
2. **Detail type from the weather map** (audit item 4) — feed coverage and its local gradient into
   the detail-type ramp, so a growing cell billows and a dissipating one shreds, in one sky. Cost S,
   and the cheapest remaining variety win.
3. **Local envelope entities** (audit item 6) — a placed `CloudMass` (position, radii, type, density)
   evaluated as bottom x top x edge gradients and `max`-ed into the cheap density. The *procedural*
   road to art-directed hero shapes, and a sibling of the voxel path that needs no baking. Cost L.
4. **NVDF import** (audit item 7 / voxel design item 12) — bake our own volumes from a fluid sim or
   the editor's own SDF tools. The seam already speaks the deck's language. Cost L-XL.
5. **Volumetric (froxel-lit) fog** — the one deliberate non-goal from the sky research that is a
   plausible future phase; the component's parameter group names were reserved for it so scenes do
   not migrate twice.

---

## 4. Things a newcomer will otherwise rediscover the hard way

- **Three elevations, never one.** Zenith `--look 0,0.9,-1`, mid `0,0.45,-1`, horizon `0,0.12,-1`.
  The horizon hid two defects through an entire programme.
- **Freeze animation** (`AnimationSpeed: 0`, wind 0) before any pixel comparison, and shoot the same
  build against itself to get the noise floor. These scenes are not byte-reproducible.
- **The camera can move**: `--camera-to`, `--look-to`, `--shot-sequence`, `--shot-every`. A static
  frame proves nothing about the temporal stage.
- **CI is `cancel-in-progress`** and Windows takes ~35 minutes. A second push while a run is in
  flight cancels it; a cancelled job is not evidence.
- **`git stash` is shared across worktrees** on this machine, and so is the machine itself — timings
  taken while another agent renders measure that agent.
- All three skills (`desert-engine-dev`, `-verify`, `-contract`) were updated on `cdf09cc5` with the
  above and with the diagnosis method that actually found the defects: instrument, knock out one
  contributor at a time, state the mechanism with numbers, build a converged ground truth.

---

## 5. Where the reasoning lives

- `Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md` — UE's 5-LUT sky pipeline and the height-fog closed form,
  read from real Epic source. **§5 holds binding teamlead decisions.**
- `Docs/Clouds/UE_VOLUMETRIC_CLOUDS_RESEARCH.md` — UE clouds vs Nubis3 vs us, reconciled roadmap. §5
  decided.
- `Docs/Clouds/NUBIS3_FULL_AUDIT.md` — our clouds against the paper, formula by formula.
- `Docs/Clouds/VOXEL_CLOUD_PATH.md` — the hero-cloud design. §7 decided.
- `Docs/Clouds/Shots/README.md` — the lit:shadow estimator and the recorded reference values.
