# UE Volumetric Cloud material — the Layout parameter set

Source: Epic public documentation, *Volumetric Cloud Material in Unreal Engine*
(`https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-material-in-unreal-engine`),
quoted here for engineering reference. This is DOCUMENTATION, not engine source: it describes the
construction the shipped material uses, which is exactly the part we could not read from the code because
the material is a binary asset.

## The three layout textures

| Parameter | What it is |
|---|---|
| `Layout_CloudHeightProfile` | "Describes the shape of the clouds as the altitude changes. Each channel describes the profile shape and relative altitude of a different cloud type: R Stratocumulus, G Altostratus, B Cirrostratus, A Nimbostratus." |
| `Layout_CloudGlobalPattern` | "Defines the world location for each type of cloud, each channel a different type" — same four-type channel assignment. |
| `Layout_GlobalCloudMask` | "Adds clouds to or removes clouds from the global pattern texture in the masked region", per type. |

## The scalar and vector parameters

| Parameter | What it does |
|---|---|
| `Layout_CloudType` | Per-type visibility (RGBA = the four types). |
| `Layout_CloudGlobalScale` | Distance over which the layout textures repeat, in **kilometres**. |
| `Layout_CloudPerTypeScale` | Scale of the global pattern per cloud type. |
| `Layout_GlobalTexturePlacement` | Offset and rotation about world Z of the layout textures. |
| `Layout_WindControls` | Wind strength per world axis; alpha scales all three. |
| `Layout_CloudTypeMask` | How strongly the global mask affects each type; alpha is the overall mask contribution. |
| `Layout_GlobalCoverage` | Overall cloud cover; positive adds, negative removes. |

## What this settles

**The vertical profile is a TEXTURE, not a formula.** One channel per cloud species, indexed by altitude.
That is the authoring surface for "make me a new cloud type", and it is what our
`CloudVerticalProfile()` — a pair of ramps driven by a single scalar — is a one-type simplification of.

**The weather map is four-channel and per-type.** Where each species lives is painted (or generated)
independently, with its own scale, and a separate mask adds or removes it regionally. Our coverage field
is the one-type case of this.

**Placement is art-directable by construction.** Offset, rotation, per-type scale and a mask exist so a
world can be given a deliberate weather layout rather than a purely procedural one. Both are the same
slot: a texture is just another producer of the same field.

**It is the same construction we now have, generalised to four types.** A 2D pattern decides WHERE, a
per-type curve gives the vertical envelope, 3D noise supplies the silhouette and the erosion. Our
producer does exactly this with one type, one pattern and one curve.

## The gap, stated as work

| UE | Ours today | Gap |
|---|---|---|
| `Layout_CloudHeightProfile`, 4 curves in a texture | `CloudVerticalProfile()`, formula on one scalar | curve authoring, four species |
| `Layout_CloudGlobalPattern`, 4 channels | one procedural coverage field + `CloudTypeVariance` | four channels; optional texture override |
| `Layout_GlobalCloudMask` | — | regional add/remove |
| `Layout_CloudGlobalScale` (km) | `WeatherTileSize` | present |
| `Layout_CloudPerTypeScale` | — | per-type scale |
| `Layout_GlobalTexturePlacement` | — | offset + Z rotation |
| `Layout_WindControls` | `WindDirection` + `WindSpeed` | per-axis, alpha master |
| `Layout_CloudType` | — | per-type visibility |
| `Layout_GlobalCoverage` | `Coverage` | present |

Note on the four species Epic chose: Stratocumulus, Altostratus, Cirrostratus, Nimbostratus. Three of the
four are LAYER clouds, and none of them is a cumulus with vertical development — which is consistent with
the flat, sheet-like look the default UE sky has, and is a choice rather than a limitation of the method.
