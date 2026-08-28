#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

#include <Engine/Assets/Common.hpp>
#include <Engine/Reflection/ReflectionMacros.hpp>

#include <cstdint>

namespace Desert::ECS
{
    // The volumetric cloud layer: a spherical shell around the planet, marched per pixel by
    // Graphic::System::VolumetricCloudRenderer.
    //
    // WHERE THE PARAMETERS COME FROM. The layer geometry and the tracing limits are
    // UVolumetricCloudComponent's, name for name, so a UE-calibrated sky transplants number for number
    // (licence position: Docs/Clouds/LICENCE_RECORD.md). The shape parameters are ours, because UE has
    // none — in UE the density is a material graph the artist authors, and there is no cloud-shape
    // parameter on the component at all. The shape maths itself follows Nubis (SIGGRAPH 2023): the
    // vertical profile multiplied by a coverage field (deck p.19), eroded by a remap rather than a
    // subtraction (p.120).
    //
    // EVERY FIELD BELOW IS READ, and it reaches the GPU by one of two routes rather than one. Every scalar
    // is packed into Graphic::CloudGpuPayload — eleven vec4s and a vec3, with no padding and no reserved
    // slot. The one field that is not a scalar is CloudType, which names an asset:
    // Graphic::System::VolumetricCloudRenderer resolves the handle through Runtime::CloudTypeService and
    // gets back the twelve numbers the profile table is generated from AND the noise volume that type's
    // edge is cut from. Either way each field has a named consumer, and Desert/Tests/Engine/SettingConsumers
    // names it. The contract forbids a knob that moves nothing, and this component is where that is easiest
    // to get wrong, because a cloud parameter that does nothing still LOOKS like it does when the sky is
    // already busy.
    //
    // UNITS. Distances are world units — centimetres (Length) — and are converted to kilometres exactly
    // once, in Graphic::PackCloudParams. The planet radius is the one exception and is authored in
    // kilometres, because 6360 km is 636 000 000 cm and no slider is useful at that scale; UE authors it
    // in kilometres for the same reason.
    //
    // ALTITUDES ARE ANCHORED TO METEOROLOGY, not to a relation, and they are NOT AUTHORED HERE. A cloud
    // TYPE carries its own base and top in kilometres, and the shell the march intersects is COMPUTED from
    // them by Graphic::PackCloudParams. The two fields that used to state it — Layer Bottom Altitude and
    // Layer Thickness — are gone, and their removal is the point rather than a tidy-up: an envelope and a
    // type's altitudes are two numbers obliged to agree, which is the exact class of defect §2.3.1 of the
    // contract is about. Nothing that has to agree with the type is left for a hand to move.
    //
    // AND THE TYPE ITSELF IS NO LONGER HERE EITHER. T0 put a four-valued Species enum on this component;
    // T1 replaced it with a handle to a `.decloudtype`, because an enumerator cannot be authored, named,
    // duplicated or shipped and the whole point of the programme is that an artist makes their own kinds
    // of cloud. The enum is gone rather than deprecated (§4.1) and the v4 -> v5 scene migration turns each
    // of its four values into the shipped preset that carries the same numbers.
    //
    // AND THE ONE HANDLE IS NOW A SET OF FOUR. One type in one slot is one sky of one kind of cloud, which
    // is not what was asked for: an artist composes a sky FROM their types. The slots overlap by
    // construction rather than dividing the sky between them — each carries its own placement field —
    // because the accepting frame for this phase is a stratocumulus deck with a congestus tower standing
    // in it, and any rule whose weights sum to one forbids exactly that (decision D-14). The v5 -> v6
    // migration renames the single `CloudType` key to `CloudType1`; nothing else about a scene changes,
    // and a scene that went in with one kind of cloud comes out rendering the same sky.

    // The top of the shadow ray's sample count, in ONE place because THREE of them disagreed. The slider's
    // Range, the clamp in Graphic::PackCloudParams and the clamp in Programs/Clouds/CloudRaymarch.shader
    // are three copies of one number, and while they were three literals an artist could drag the slider
    // to a value the shader silently threw away. The first two now read this constant; the shader cannot,
    // so Desert/Tests/Engine/SettingConsumers reads the shader's text and asserts the literal matches —
    // §2.3.1's "two values obliged to agree", asserted rather than remembered.
    //
    // SIXTY-FOUR because that is where the measurement stops moving: 48 and 64 render the sunward zenith
    // p95 identically to three decimals (0.798), so nothing above it is buying anything. Unreal's own
    // ceiling is 80 (r.VolumetricCloud.Shadow.ViewRaySampleMaxCount); the difference is not a shortfall,
    // it is the plateau measured on OUR quadrature instead of copied from theirs.
    inline constexpr int32_t kCloudLightMarchMaxSamples = 64;

    struct VolumetricCloudData
    {
        REFLECT()

        // ---- Cloud Layer ----------------------------------------------------------------------------

        PROPERTY( DisplayName( "Enabled" ), Category( "Cloud Layer" ), Summary,
                  Tooltip( "Master switch. Off dispatches nothing: a scene with the clouds disabled pays "
                           "zero GPU cost, exactly like a scene without the component." ) )
        bool Enabled = true;

        // FOUR SLOTS AND NOT ONE, which is what "a sky made of several kinds of cloud" costs in fields.
        //
        // WHY FOUR. A type owns one CHANNEL of the profile table, so the ceiling is the width of a texel
        // rather than a preference — Unreal fixes exactly four types to R, G, B and A of its three layout
        // textures (EpicDoc_CloudMaterial.md §2-3). The LIBRARY on disk is not limited by this; a project
        // may ship a hundred `.decloudtype` files and this is how many of them one sky holds at once.
        //
        // WHY FOUR NAMED FIELDS AND NOT A LIST. A slot is a channel, and a channel is a place rather than a
        // position in a sequence: dragging a type into the third slot has to mean the third channel, and
        // the profile table's decorrelation offsets are per SLOT. A container would also lose the asset
        // picker, the drag-and-drop target and the per-row tooltip the Details panel gives every reflected
        // asset field, and gain a reorder operation that changes the sky for no reason an artist can see.
        //
        // THEY ARE FILLED FROM THE FRONT. The renderer packs the non-empty ones down to a prefix and sends
        // a count; a gap in the middle is closed rather than carried, so slot 3 alone behaves exactly like
        // slot 1 alone. That is what makes "how many species" one number instead of a mask.
        //
        // ALL FOUR EMPTY IS A DOCUMENTED ANSWER, not a hole: the layer resolves to one
        // Assets::CloudTypeDefaultShape, which is T0's cumulus congestus digit for digit. A scene nobody
        // has authored a type for still has to have a sky, and that is a load-bearing requirement of the
        // whole programme rather than a convenience — it is also why every default here is 0 and not the id
        // of a shipped file, which would make every scene depend on a file being present.
        PROPERTY( DisplayName( "Cloud Type 1" ), Category( "Cloud Layer" ), Summary, Asset<CloudTypeAsset>,
                  Tooltip( "Which kind of cloud this layer is made of — drag a .decloudtype from the "
                           "Content Browser or author one in Window > Cloud Type. The type carries its own "
                           "base and top in kilometres, its own family of vertical profiles, its own edge "
                           "character, its own density and its own placement scale, and the shell the "
                           "march intersects is COMPUTED from all of them together. A layer may carry up "
                           "to four; they OVERLAP rather than divide the sky between them, so a low deck "
                           "and a tall tower can stand in the same place. All four empty uses the engine's "
                           "built-in cumulus congestus." ) )
        Assets::AssetHandle CloudType1;

        PROPERTY( DisplayName( "Cloud Type 2" ), Category( "Cloud Layer" ), Asset<CloudTypeAsset>,
                  Tooltip( "A second kind of cloud in the same sky. Its placement field is its OWN — its "
                           "own scale, its own stretch along the wind, its own patches — so it is not a "
                           "share of the first type's sky but an independent one laid over it. Where two "
                           "types meet, the deeper body wins and keeps its own edge and density." ) )
        Assets::AssetHandle CloudType2;

        PROPERTY( DisplayName( "Cloud Type 3" ), Category( "Cloud Layer" ), Asset<CloudTypeAsset>,
                  Tooltip( "A third kind of cloud. Costs one noise fetch per sample only at altitudes its "
                           "own base and top actually reach — a cirrus at eight kilometres is free "
                           "everywhere a cumulus lives." ) )
        Assets::AssetHandle CloudType3;

        PROPERTY( DisplayName( "Cloud Type 4" ), Category( "Cloud Layer" ), Asset<CloudTypeAsset>,
                  Tooltip( "The fourth and last kind. Four is the ceiling because a type owns one channel "
                           "of the vertical profile table, which is how Unreal arranges it too." ) )
        Assets::AssetHandle CloudType4;

        PROPERTY( DisplayName( "Planet Radius" ), Category( "Cloud Layer" ), Units( "km" ),
                  Range( 100.0f, 7000.0f ), Advanced,
                  Tooltip( "Radius of the planet the layer curves around. It is what puts the horizon "
                           "where it belongs: a flat layer has no horizon at all and either fills the "
                           "whole lower sky or ends at an invisible edge." ) )
        float PlanetRadius = 6360.0f;

        PROPERTY( DisplayName( "Max View Distance" ), Category( "Cloud Layer" ), Length,
                  Range( 100000.0f, 40000000.0f ),
                  Tooltip( "How far along the ray the march may run, measured FROM THE POINT THE RAY "
                           "ENTERS THE LAYER. Measured from the camera instead it would cut the layer at "
                           "a fixed radius and draw a circular edge across the sky." ) )
        // SIXTY KILOMETRES, and it is half of a PAIR rather than a number of its own. Divided by
        // WeatherTileSize it gives the number of times the coverage field REPEATS between the camera and
        // the vanishing point, and a repeating field seen end-on reads as streaks radiating from that
        // point. Docs/Clouds/CALIBRATION.md section 4 records the failure and its cure: at 150 km against
        // an 8 km tile the field repeated about twenty times and the moire was unmissable, and the pair
        // that fixed it was 60 km against 12 km — five repeats. These defaults ARE that pair, and
        // ComponentReflection asserts the ratio rather than the two numbers, because it is the ratio that
        // was measured.
        float MaxViewDistance = 6000000.0f; // 60 km

        PROPERTY( DisplayName( "Tracing Start Max Distance" ), Category( "Cloud Layer" ), Length,
                  Range( 1000000.0f, 100000000.0f ), Advanced,
                  Tooltip( "If a ray only enters the layer beyond this distance, it is not traced at all. "
                           "It bounds the cost of grazing rays, and it is the guard that keeps a ray whose "
                           "entry the geometry reports thousands of kilometres away from ever being "
                           "marched." ) )
        float TracingStartMaxDistance = 35000000.0f; // 350 km — UE's shipped default

        PROPERTY( DisplayName( "Tracing Start Distance" ), Category( "Cloud Layer" ), Length,
                  Range( 0.0f, 5000000.0f ), Advanced,
                  Tooltip( "Pushes the start of the march away from the camera. Useful when the camera "
                           "is inside the layer and the nearest metres are both the most expensive and "
                           "the least interesting." ) )
        float TracingStartDistance = 0.0f;

        // ---- Weather --------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Coverage" ), Category( "Weather" ), Range( 0.0f, 1.0f ), Summary,
                  Tooltip( "What fraction of the sky has cloud in it. It decides how many cells of the "
                           "cloud lattice carry a cloud, so lowering it opens clear gaps rather than "
                           "thinning everything." ) )
        // 0.45, MEASURED — and the metric is stated, which the first version of this table was not.
        //
        // "sky cover" below is the fraction of vertical COLUMNS through the layer whose cloud hides at
        // least half the sky behind it, over one period of the field at contrast 1 with the defaults of
        // this component. Desert/Tests/Engine/CloudField measures it and prints exactly this row, so it is
        // reproducible rather than remembered:
        //
        //     Coverage   0.10   0.20   0.30   0.40   0.50   0.60   0.70   0.80   0.90   1.00
        //     touched     13%    23%    34%    44%    56%    67%    76%    85%    92%    96%
        //     sky cover    7%    13%    20%    30%    42%    52%    60%    69%    80%    86%
        //
        // THE SLIDER STOPPED BEING A THRESHOLD AND BECAME A COUNT, and the default moved by exactly the
        // amount needed to leave the sky where it was — which is the third time this has happened and the
        // third time it is recorded here. There is no coverage FIELD any more to threshold: a lattice cell
        // carries a cluster of lumps when its own hash falls below `pow(Coverage, 0.68)`, and the 0.68 is
        // measured on the top-down projection of the baked volume so that the slider and what a person
        // looking up would count are the same number to within 0.03 of the sky
        // (Desert/Tests/Engine/CloudProceduralField re-measures it on every run). 0.45 is the setting that
        // reproduces the ~36 % sky cover the previous default of 0.10 produced.
        //
        // Both ends are exact BY CONSTRUCTION rather than by arithmetic that cancels: a power is zero at
        // zero and one at one, so 0 is genuinely clear and 1 genuinely solid whatever the seed, the cell
        // size or the type. The whole travel is useful now — the old row had spent a third of its band
        // before 0.10 — because the count is linear in the sky where a threshold on a bounded field was
        // not.
        //
        // THE SHIPPED CLOUD SCENES WERE RE-AUTHORED THIS TIME, and the difference from the two previous
        // recalibrations is the condition they were justified by: "all three still read as cloud". At the
        // new mapping they did not. Clouds_Demo's authored 0.24 fell from ~66 % sky cover to ~15 %, and
        // the frame it produced had cloud on the horizon and an EMPTY ZENITH — which is the defect
        // Docs/Clouds/REVIEW_622a01a6.md names and the one the owner found by looking up. Each scene's
        // Coverage was mapped through the two measured rows at EQUAL SKY COVER (0.24 -> 0.762,
        // 0.25 -> 0.779, 0.30 -> 0.855, 0.08 -> 0.384, 0.07 -> 0.347, 0.04 -> 0.209, 0.015 -> 0.075), so
        // what was preserved is the appearance the scene was authored for rather than the number.
        float Coverage = 0.45f;

        PROPERTY( DisplayName( "Coverage Contrast" ), Category( "Weather" ), Range( 0.1f, 4.0f ),
                  Tooltip( "Sharpness of the transition from clear to cloudy, as the WIDTH of the ramp "
                           "from an empty cell to a full one. Above 1 the ramp narrows and the sky is "
                           "decisively cloud or decisively clear; below 1 it widens and the cloud sizes "
                           "spread out, which is what a broken deck looks like." ) )
        // IT RAMPS THE SIZE OF A CLUSTER NOW, not the width of a threshold's smoothstep, and the two are
        // the same statement about the same picture: a cell whose hash only just cleared the coverage
        // grows a small, low cloud, and one well inside it grows a full one. Read by the BAKE
        // (Engine/Assets/CloudProceduralVolume.cpp) rather than by the march, because it decides what is
        // in the volume.
        float CoverageContrast = 1.0f;

        // THE TWO FIELDS THAT USED TO STAND HERE ARE GONE, and neither has moved anywhere. `Cloud Type`
        // was a scalar between "flat sheet" and "heaped cloud" that fed one analytic curve, and
        // `Cloud Type Variance` mixed noise into it so that neighbouring clouds would not all reach the
        // same ceiling. Both are answered by the profile TABLE and answered better: the table's second
        // axis is the placement pattern's own value, so a cloud is low and flat at the rim of a patch and
        // a tower in its middle — height that is CORRELATED with how much cloud is there, instead of
        // height sprinkled by a noise that knew nothing about the patch it was decorating. A tower on a
        // thin edge was the visible cost of the old arrangement.
        //
        // A THIRD FIELD WAS BUILT HERE AND MEASURED AWAY. `Shape Distortion` drove Unreal's
        // height-dependent domain warp — the second noise that displaces the coverage field's coordinates
        // so that one flat pattern cannot be extruded upward into a column. Over the WHOLE travel of the
        // control it moved ImageStat contrast by at most 0.018 and with no consistent sign, against the
        // 0.08 the profile table itself moved and the 0.2 that separates two species; the six rows of
        // numbers are in Common/CloudField.glslh. It buys nothing HERE because our coverage field is
        // already sampled in full three dimensions, which is a different solution to the same problem —
        // and it cost one fetch of the noise volume per sample, in the march and in every light-march
        // sample beneath it. A control that moves nothing is what §1.3 of the contract calls a TODO
        // wearing a feature's clothes.

        PROPERTY( DisplayName( "Weather Tile Size" ), Category( "Weather" ), Length,
                  Range( 200000.0f, 8000000.0f ),
                  Tooltip( "World size the cloud lattice is measured against. One cell is a QUARTER of it, "
                           "and a cell carries Cloud Density clouds on average — which is also what "
                           "decides whether there is any cloud overhead at all: a cell much larger than "
                           "the layer altitude cannot fit one above the camera, and the zenith comes out "
                           "empty however high the coverage is set." ) )
        // TWELVE KILOMETRES -> 3 km cells, a cumulus field. The other half of the calibrated pair; see
        // MaxViewDistance for what the two of them together decide and for where it was measured.
        //
        // IT IS A LATTICE AND NOT A NOISE PERIOD NOW. The number and its quarter are unchanged and so is
        // everything the calibration of §4 says about the pair — what changed is what sits in a cell. It
        // was one cell of an Alligator field thresholded into a cushion; it is a CLUSTER OF OVERLAPPING
        // LUMPS fused by the modelling volume's smooth minimum (Engine/Assets/CloudProceduralVolume.hpp).
        float WeatherTileSize = 1200000.0f; // 12 km -> 3 km cells, a cumulus field

        PROPERTY( DisplayName( "Region Size" ), Category( "Weather" ), Length, Range( 1600000.0f, 12000000.0f ),
                  Advanced,
                  Tooltip( "How much world the camera-centric modelling volume covers, and the distance "
                           "over which the sky repeats beyond it. Larger is more sky before the "
                           "repetition shows and a coarser voxel; the volume's 256 voxels are spread over "
                           "exactly this." ) )
        // FORTY-EIGHT KILOMETRES, and it is half of a relation rather than a taste.
        //
        // BELOW, by what the march can find: the volume is 256 voxels across, trilinear filtering cannot
        // express a feature narrower than two of them, and the march SEARCHES at
        // CloudFinestResolvableChordKm — 125 m at the default Max Steps. A region under 16 km would put
        // structure in the volume that no ray can be relied on to sample, and
        // Assets::ValidateCloudProceduralParams refuses it by name. The slider's floor is that bound.
        //
        // ABOVE, by the repetition: the volume is periodic, so past the region the sky is the region
        // again. Max View Distance over this number is how many times it repeats between the camera and
        // the vanishing point — 60 over 48 is 1.25, against the five repeats CALIBRATION.md §4 measured
        // as the cure for the moire at twenty. At the shipped pair the voxel is 187.5 m.
        float RegionSize = 4800000.0f; // 48 km -> 187.5 m per voxel, 1.25 repeats to the vanishing point

        PROPERTY( DisplayName( "Seed" ), Category( "Weather" ), Range( 0, 65535 ),
                  Tooltip( "Which sky. Two layers with different seeds have unrelated clouds; the same "
                           "seed always gives the same sky, in the same places, for the same settings." ) )
        // WHICH REALIZATION OF THE FIELD, and it exists because the lumps are placed by a HASH of their
        // lattice cell rather than read out of a texture. Before this phase the realization was a property
        // of the noise volume ASSET — its own baked seed — and a scene that wanted a different sky had to
        // bake a different volume. The placement is procedural now, so the choice is a number, and a
        // number an artist can turn is worth one slider.
        int32_t Seed = 1;

        // ---- Placement ------------------------------------------------------------------------------
        //
        // WHY THIS CATEGORY EXISTS. The owner looked at the sky and said the clouds "just go in a row" and
        // that the whole sky was cloud "with an obvious pattern". Both statements were true and both were
        // MEASURABLE, which is what Tools/LatticePeak was built for: it takes the autocorrelation of the
        // baked field seen from below and reports how far a bump standing on a multiple of the lattice's
        // own period rises above the estimator's noise. On the sky that shipped before this category the
        // bumps stood at 6.000, 9.000 and 12.000 km against a predicted cell of 3.000 km — the lattice's
        // multiples, to the voxel — at eight to eighteen times that noise.
        //
        // THE CAUSE WAS NOT THE ONE THE SHAPE OF THE DEFECT SUGGESTED, and the numbers are in
        // Docs/Clouds/CALIBRATION.md section RW. Raising the number of clouds per cell ALONE makes the
        // grid two and a half times WORSE, because several small clouds crowded into the middle of a cell
        // mark that cell's site more sharply than one large one did. What removes the lattice is letting a
        // cloud LEAVE the cell that made it: on that change alone the bump falls from 0.066 to 0.011.
        //
        // Every one of the four below is read by Engine/Assets/CloudProceduralVolume.cpp at BAKE time, not
        // per frame, so all four are free at render time and cost a rebake when they change.

        PROPERTY( DisplayName( "Cloud Density" ), Category( "Placement" ), Range( 0.25f, 8.0f ),
                  Tooltip( "How many clouds a lattice cell carries on average — a whole number is drawn "
                           "per cell with this mean, so 'exactly one per cell' stops being a property of "
                           "the sky. It does NOT change how much cloud there is: each cloud is narrowed as "
                           "this rises, so the matter is redistributed rather than added and Coverage "
                           "keeps meaning what it says. Raise it for many small clouds at the same cover, "
                           "lower it for few large ones. On its own it makes the lattice MORE visible, not "
                           "less — Cloud Scatter is what removes that." ) )
        // 1.75, AND THE VALUE IS MEASURED TWICE OVER — see Docs/Clouds/CALIBRATION.md §RW2, which moved it
        // down from the 2.5 §RW shipped. Upward it is bounded by the PICTURE and not by the bake: the
        // cluster narrows as this rises, so at 2.5 every cloud in the frame had shrunk to the size the far
        // ones already were and the sky read as one flat pelt of identical lozenges. The frame's contrast
        // goes 0.379 here against 0.349 at 2.5, and the sky the owner accepted measured 0.384.
        //
        // Downward it is bounded by the GRID, and that bound is where §RW put it: at a density of 1 the
        // lattice bump comes back at 0.0264, 5.9 times the estimator's noise. Between them the bump is
        // inside the noise at every setting measured — 0.0007 at 1.5, 0.0017 here, 0.0027 at 2.0 — so the
        // count is not what removes the grid and 2.5 was buying nothing the instrument could see. Cloud
        // Scatter is what removes it, and §RW's own arm table says so.
        float PlacementDensity = 1.75f;

        PROPERTY( DisplayName( "Cloud Scatter" ), Category( "Placement" ), Range( 0.0f, 4.0f ),
                  Tooltip( "How far a cloud may wander from its lattice site, measured in CELLS — at 1 it "
                           "may sit anywhere in a cell-wide box around its site and so crosses into its "
                           "neighbours' ground. THIS IS THE KNOB THAT REMOVES THE GRID: at 0 the sky is a "
                           "lattice with a wobble, and the measured lattice bump falls by six times "
                           "between 0 and 1. It is not free — independently placed clouds overlap where a "
                           "lattice keeps them apart, so the sky covers a few points less than Coverage "
                           "says at 0 and a few points more when this is returned to zero." ) )
        // ONE CELL, and the ceiling of 4 is where a cluster's own site stops meaning anything: at four
        // cells of travel a cloud is as likely to be anywhere in a nine-cell neighbourhood as at home, so
        // the lattice has already stopped being measurable long before the top of the slider. The travel
        // above 1 is left because it is what an artist reaches for when the cell is small.
        float PlacementScatter = 1.0f;

        PROPERTY( DisplayName( "Cloud Size Variety" ), Category( "Placement" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How much cloud sizes differ from each other. At 0 every cloud in the sky is "
                           "the size its cell's coverage says, which is the second half of what reads as a "
                           "pattern; at 1 the widest is about four times the narrowest. The draw is "
                           "uniform in AREA rather than in width, so the mean cloud covers the same ground "
                           "at every setting and Coverage does not move with it. A smaller cloud is also a "
                           "flatter one, which is what a field of cumulus looks like." ) )
        float PlacementSizeVariety = 0.75f;

        PROPERTY( DisplayName( "Weather Patch Size" ), Category( "Placement" ), Length,
                  Range( 500000.0f, 20000000.0f ),
                  Tooltip( "World size over which the sky's BUSY and CLEAR regions alternate — the scale "
                           "of a weather system rather than of a cloud. Bounded from BELOW at three "
                           "lattice cells and raised to it silently if set finer, because a modulation "
                           "whose period is near a cell's decides cells one at a time and reads as a "
                           "checkerboard rather than as weather. Above Region Size it stops buying "
                           "anything: the sky already repeats at that distance, so a longer period cannot "
                           "complete a cycle before the repetition does." ) )
        // TWENTY-ONE KILOMETRES — seven cells at the shipped 12 km weather tile, and under half the 48 km
        // region so a full cycle of busy and clear fits inside one period of the sky.
        float PatchTileSize = 2100000.0f;

        PROPERTY( DisplayName( "Weather Patch Strength" ), Category( "Placement" ), Range( 0.0f, 1.0f ), Summary,
                  Tooltip( "How hard the sky is divided into busy and clear regions. At 0 the coverage is "
                           "the same everywhere and the sky is uniformly occupied — which is what 'the "
                           "whole sky is cloud' describes; at 1 a patch can reach nearly solid and its "
                           "neighbour nearly empty. It is symmetric about Coverage, so it moves cloud "
                           "around the sky rather than adding or removing it." ) )
        float PatchStrength = 0.60f;

        // ---- Layout ---------------------------------------------------------------------------------
        //
        // WHY THIS CATEGORY EXISTS. The owner asked two questions in one breath: "will we have a cloud
        // material editor like Unreal's, so I can load noise textures for the cloud shape", and separately,
        // about the arrangement of clouds in the sky, "is there a parameter to do this by HAND". The first
        // is refused and stays refused — decision D-5, no material graph — and the second is this.
        //
        // They are not the same request. Unreal's `Layout_CloudGlobalPattern` and `Layout_GlobalCloudMask`
        // are DATA, and data is the one part of that material we can take without taking the graph: the
        // same formula this programme has already applied three times (`.decloudtype`, `.dcnv`, `.dcmv`) —
        // Unreal's semantics, our formats. The full account of what Unreal actually does with those
        // textures, and which half of it is verifiable from engine source at all, is in
        // Docs/Clouds/RESEARCH_LAYOUT_TEXTURES.md.
        //
        // THE THIRD TEXTURE IS DELIBERATELY ABSENT. `Layout_CloudHeightProfile` is a table
        // `f(altitude, pattern value)` per type, and Unreal needs it because its placement field is two
        // dimensional and the table is the only vertical structure it has. Ours is geometry — a lump has
        // three radii and an altitude, and `fill` in Engine/Assets/CloudProceduralVolume.cpp already drives
        // the stack's vertical band by how deep inside the coverage threshold a cell fell. A painted table
        // would be a SECOND way to say the same thing (§2.3.1) and would fight the §SIL2 calibration, in
        // which the lump's aspect and the erosion's strength are one quantity guarded by a test that names
        // both numbers.
        //
        // EVERY FIELD BELOW IS READ AT THE BAKE, like Placement above it and for the same reason: where a
        // cloud is gets decided once, when the settings change or the region shifts, and never per frame.
        // The march reads the volume it already read.

        PROPERTY( DisplayName( "Cloud Layout" ), Category( "Layout" ), Summary, Asset<CloudLayoutAsset>,
                  Tooltip( "A PAINTED sky — drag a .dclayout from the Content Browser, or make one from an "
                           "image in View > Cloud Layout. Its four channels say where each of this "
                           "layer's four cloud type slots lives, and its mask adds or removes cloud in "
                           "regions you paint. EMPTY IS THE NORMAL STATE: with no layout the sky is placed "
                           "procedurally exactly as before, and Weather Patch Strength decides which parts "
                           "of it are busy. The painting is read when the clouds are placed, not while they "
                           "are drawn, so it costs the frame nothing." ) )
        // ALL LAYERS SHIP WITH THIS EMPTY, and that is the phase's own acceptance criterion rather than a
        // convenience: an unpainted sky must render the frame it rendered before this field existed, byte
        // for byte, at all six points of the protocol. The default is 0 and not the id of a shipped file
        // for the same reason every other asset default here is — a scene must not depend on a file being
        // present.
        Assets::AssetHandle CloudLayout;

        PROPERTY( DisplayName( "Layout Pattern Strength" ), Category( "Layout" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How strongly the painting decides where clouds are. At 1 the painted pattern "
                           "rules: bright regions of a channel fill with that slot's kind of cloud and dark "
                           "ones empty. AT 0 THE PROCEDURAL PATCH FIELD TAKES OVER INSTEAD — the sky goes "
                           "back to Weather Patch Strength's busy and clear regions, so this end of the "
                           "slider is a live sky and not an absence of one. The painting is applied about "
                           "its OWN AVERAGE, so it moves cloud around the sky rather than adding it and "
                           "Coverage keeps meaning the fraction of sky it delivers." ) )
        // ONE, so that dropping a painting into the slot shows it. A default of zero would be the safer
        // number and the worse one: the artist would bind a layout, see nothing at all, and have no way of
        // telling a slot that is not wired from a slider that is down.
        float LayoutPatternStrength = 1.0f;

        PROPERTY( DisplayName( "Layout Mask Strength" ), Category( "Layout" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How hard the painted mask adds and removes cloud. The mask is the layout's "
                           "alpha: mid-grey changes nothing, brighter adds cloud, darker takes it away. "
                           "Unlike the pattern it is NOT balanced about its own average — adding cloud "
                           "where you paint is what it is for — so it does move the sky's total cover. A "
                           "layout with no mask in it contributes nothing at any setting." ) )
        // ONE, and it diverges from Epic's own default of zero for a reason their arrangement does not
        // have: their mask slot always holds a texture (a stub when nobody set one), so a non-zero default
        // would apply a placeholder to every sky. Ours can be ABSENT, and absent already contributes
        // nothing — so the default can be the value an artist who painted a mask meant.
        float LayoutMaskStrength = 1.0f;

        PROPERTY( DisplayName( "Layout Repeats" ), Category( "Layout" ), Range( 1, 16 ),
                  Tooltip( "How many times the painting repeats across Region Size. At 1 the whole painting "
                           "covers the region once — 48 km at the default — and at 16 it repeats every 3 "
                           "km, which is one cloud lattice cell. A WHOLE NUMBER because the sky repeats at "
                           "Region Size: a painting whose period did not divide it would put a hard seam "
                           "across every region boundary." ) )
        // AN INTEGER IS THE RELATION, NOT A CHECK ON ONE. The modelling volume is periodic over the region
        // and everything past the region is that volume again (Engine/Assets/CloudProceduralVolume.hpp);
        // the wrap seam is measured at 0.950/255 against 1.239/255 between ordinary neighbours. A painting
        // sampled on a period that does not divide the region breaks that, and the defect is an order of
        // magnitude larger than the seam that exists. Expressed as a count of repeats, the divisibility
        // cannot be stated wrongly — which is the difference between a property of the type and a
        // validator somebody has to remember (§2.3.1).
        int32_t LayoutRepeats = 1;

        PROPERTY( DisplayName( "Layout Rotation" ), Category( "Layout" ), Range( 0, 3 ),
                  Tooltip( "Quarter turns of the painting about the vertical axis: 0, 1, 2 or 3 — that is "
                           "0, 90, 180 and 270 degrees. Use it to point a painted band north-south instead "
                           "of east-west without repainting. QUARTER TURNS AND NOT A FREE ANGLE, because "
                           "only a quarter turn maps the sky's own repeat onto itself; any other angle "
                           "would put a seam at the distance the sky repeats." ) )
        // Unreal rotates its layout freely (`Layout_GlobalTexturePlacement.a`) and can afford to: it has no
        // baked volume to keep in step, because it has no baked volume. The divergence is named here and in
        // Docs/Clouds/RESEARCH_LAYOUT_TEXTURES.md §2.3, and its bearing input is the measured wrap seam.
        int32_t LayoutRotation = 0;

        PROPERTY( DisplayName( "Layout Offset" ), Category( "Layout" ), Length,
                  Tooltip( "Where the painting's origin sits in the world, east and north. Slide it to put "
                           "a painted feature over a particular place on the ground. Continuous, unlike the "
                           "rotation — sliding a repeating pattern along its own axis leaves it repeating, "
                           "so there is nothing here for the sky's own period to disagree with." ) )
        glm::vec2 LayoutOffset = { 0.0f, 0.0f };

        // ---- Detail ---------------------------------------------------------------------------------
        //
        // THE NOISE VOLUME SLOT THAT USED TO STAND BETWEEN Weather AND Detail IS GONE FROM HERE, and it
        // moved rather than disappeared: it is a field of the CLOUD TYPE now. The reason is what the slot
        // decides — the character of the cloud's edge, wispy against billowy, fine against coarse — and
        // that is a property of the kind of cloud, not of the weather it is having. A cirrus cut from the
        // same volume as a cumulonimbus is a cirrus with a cumulonimbus' edge; the shipped Cirrus type
        // names CloudNoise_FineWisp.dcnv for exactly that reason. Two slots for it, one here and one on
        // the type, would have been two values that can disagree (§4.2).

        PROPERTY( DisplayName( "Detail Tile Size" ), Category( "Detail" ), Length, Range( 20000.0f, 3000000.0f ),
                  Tooltip( "World size over which the erosion field repeats — the scale of the billows "
                           "and wisps cut into the cloud's edge. It is bounded from BOTH sides and the "
                           "bounds are measured: above by the size of a cloud, because an erosion wave "
                           "longer than a body scales that body instead of texturing it; below by the "
                           "march's own search step, because structure the march cannot find is dither." ) )
        // ONE KILOMETRE, DOWN FROM FOUR, and the correction is a RELATION rather than a taste — the same
        // relation this programme has been bitten by four times, "what is placed against what can resolve
        // it", here between the erosion and the body it cuts into.
        //
        // MEASURED (Desert/Tests/Engine/CloudField, and the numbers are printed by the suite on every
        // run). At the shipped weather the procedural producer's bodies have a mean chord of 1071 m. The
        // erosion field's own full wavelength, walked along the same lines:
        //
        //     tile 8 km -> 1508 m   1.41 body chords     tile 1 km ->  235 m   0.22 body chords
        //     tile 4 km ->  884 m   0.83 body chords     tile 0.5 km-> 119 m   0.11 body chords
        //     tile 2 km ->  433 m   0.40 body chords     tile 0.2 km->  56 m   0.05 body chords
        //
        // At the four kilometres this shipped, the erosion put ONE WAVE ACROSS A WHOLE CLOUD. A field that
        // is nearly constant over a body cannot cut billows into it; it makes the body slightly larger on
        // one side and slightly smaller on the other, which is a smooth blob of a different size. That is
        // why every frame of this subsystem has read as smooth however the Detail Strength was set, and it
        // was true BEFORE phase Э5 as well — the same measurement on the pre-Э5 producer gives an erosion
        // wave of 1.08 body chords.
        //
        // THE FLOOR IS THE MARCH'S. CloudFinestResolvableChordKm is 125 m at the component's Max Steps, and
        // at half a kilometre of tile the erosion's wave is 119 m — BELOW it, so whether a wisp is sampled
        // at all is decided by the ray's jitter and it reads as dither. One kilometre puts the wave at
        // 235 m, 1.88x the chord, which is the smallest tile that still clears it. The slider's own floor
        // of 200 m is far past that bound and is left where it is deliberately: it is an artist's knob and
        // the bound depends on Max Steps, so it is asserted as a RELATION in the suite rather than frozen
        // into a range.
        float DetailTileSize = 100000.0f; // 1 km -> a 235 m erosion wave, 0.22 of a body, 1.88x the march

        PROPERTY( DisplayName( "Detail Strength" ), Category( "Detail" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How deeply the erosion cuts, for the LAYER. At 0 the cloud keeps the smooth "
                           "silhouette of its coverage field; at 1 the edge is eaten away into wisps. The "
                           "cloud type multiplies this by its own factor, so a lenticular stays smooth and "
                           "a cirrus stays wispy at whatever the layer is set to." ) )
        // 0.40, UP FROM 0.10, and the 0.10 it replaces was NOT wrong for the reason it was suspected of.
        //
        // WHAT THE 0.10 WAS. It came from UE's own pair — base noise 0.8, detail 0.08 — read as "the
        // erosion is an order of magnitude weaker than the shape it cuts into", after a 0.5 that was
        // believed to be eating the layer. The suspicion when this was re-opened was that phase Э5 had
        // moved the number's carrying input: that the profile used to be low almost everywhere, so a
        // shallow cut was enough, and that the normalised distance field is high inside bodies, so the
        // same cut now does nothing. MEASURED ON BOTH PRODUCERS, THAT IS BACKWARDS, and by a factor of
        // four. The pre-Э5 field carried 60.8 % of its profile MASS above 0.9; the Э5 field carries
        // 20.7 %. At 0.10 the erosion removed 1.7 % of the old field's mass and removes 7.1 % of this
        // one's. The cut got STRONGER when the producer changed, not weaker.
        //
        // WHY 0.10 IS STILL TOO LOW, and why the answer is exactly 0.40. On the frame the whole slider is
        // a monotone trade with no knee in it — over `Clouds_Demo` at mid elevation away from the sun the
        // silhouette's raggedness runs 0.0032 -> 0.0047 and the texture inside the body 0.00558 -> 0.00727
        // while the cloud loses 6.4 % of its area (Docs/Clouds/CALIBRATION.md §DS). A curve with no knee
        // does not choose a value, so the value is fixed by TWO BOUNDS, and they touch at one number:
        //
        //   BELOW, BY THE MARCH, and this is the measured half. The cut has to move the surface the eye
        //   sees by at least the chord the march can be relied on to find — CloudFinestResolvableChordKm,
        //   125 m — or the structure it carves is finer than the renderer represents and the setting is a
        //   fetch that changes nothing. Measured on the columns, in metres of travel: 0.10 -> 54,
        //   0.20 -> 88, 0.30 -> 113, 0.35 -> 126, 0.40 -> 139, 1.00 -> 257.
        //
        //   ABOVE, BY A CONVENTION, AND IT IS LABELLED AS ONE. The floor does not fix the value on its
        //   own: 0.35 clears it by ONE metre and 0.40 by fourteen. A default is set with headroom over its
        //   floor rather than balanced on it, so this is the first step with real headroom — and the suite
        //   asserts only that it stays inside an octave of the floor, which is what stops it drifting
        //   upward unnoticed. Everything above the floor costs cloud: over the whole slider the layer
        //   loses 6.4 % of its area, and 2.9 % of that is already spent at 0.40.
        //
        // WHAT IT COST THE LIBRARY, because raising this number multiplies EVERY type's cut by the same
        // four. The cut's depth is `clamp(DetailStrength * DetailFactor, 0, 1)`, and two shipped types
        // were authored with a factor above 1 against the old 0.10. Shot against a cloudless frame at the
        // same camera, the cirrus' contribution to the picture fell from 33.3 % of its un-eroded self to
        // 4.3 % and the altocumulus' from 51.5 % to 7.5 %: both are half the density of a cumulus, so a
        // deep cut deletes them rather than shredding them. Their factors were RE-BASED to 0.625 and 0.40,
        // which restores the effective cut their files were authored at exactly.
        //
        // AND "EXACTLY" BELONGS TO THE DEPTH ALONE, because the two settings have different SCOPES: this
        // one and Detail Tile Size are the LAYER's, Detail Factor is the TYPE's. A re-based factor can
        // restore a type's cut depth; it cannot restore the SCALE of the field that cut is taken from,
        // because the tile moved for all nine types at once. Measured, the redistribution is small and
        // the amount of cloud is not: the cirrus' contribution to the frame moves by −0.4 % and the
        // altocumulus' by +1.1 %. Docs/Clouds/CALIBRATION.md §DS carries the frames and the correction.
        //
        // A CONSEQUENCE RECORDED RATHER THAN LEFT TO BE DISCOVERED: at 2.50 the cirrus reached the clamp
        // at this value, so the wispiest type in the library could be driven to the deepest cut the maths
        // allows; at 0.625 it tops out there however far this slider is pushed. That range rendered 4.3 %
        // of the type — its top end deleted the cloud rather than shredding it — so what was removed is
        // range that produced nothing. A more ragged cirrus needs the density to carry a deeper cut first.
        //
        // Desert/Tests/Engine/CloudType asserts the bound that follows from it — no shipped type may be
        // cut deeper than the reference congestus — and Desert/Tests/Engine/CloudField asserts the floor.
        //
        // AND WHAT IT DOES NOT BUY, stated here because the next person will otherwise re-measure it. The
        // ray sees a cloud at a profile of 0.694 (the depth at which the optical depth first reaches 1),
        // and the erosion's weight there is `1 - 0.694 = 0.306`. Even at the slider's maximum only 31 % of
        // the nominal depth reaches the surface the eye is looking at, so NO setting of this number
        // produces a shredded silhouette. That is a property of the `(1 - Profile)` weight in
        // Common/CloudField.glslh, it predates phase Э5, and re-deriving it against the optical surface is
        // a design change rather than a calibration.
        //
        // ---------------------------------------------------------------------------------------------
        // 0.65, UP FROM §DS's 0.40, AND THE REASON IS NOT IN THIS FILE — see CALIBRATION.md §SIL2.
        // ---------------------------------------------------------------------------------------------
        //
        // THIS NUMBER IS ONE HALF OF A PAIR. The other half is Assets::kCloudLumpVerticalOverHorizontal,
        // the shape of the lump a body is built out of. A taller lump makes that body optically thicker per
        // metre, so the surface at which the optical depth first reaches 1 sits at a SHALLOWER profile
        // (0.632 at the 0.45 lump, 0.576 at the 0.75 one) and the same cut moves it a SHORTER distance. The
        // floor above is a floor on that distance. Raise the lump and this number has to follow, or the
        // erosion drops through a bound that is measured in another suite and named in neither file.
        //
        // §SIL RAISED THE LUMP TO 0.75, MEASURED IT, FRAMED IT AND COMMITTED IT, and the full sweep is what
        // found that its travel was 101 m against the 125 m floor. That is the whole argument for the
        // relation test named at the bottom of this comment.
        //
        // THE LADDER, RE-MEASURED AGAINST THE 0.75 LUMP — the same instrument as §DS's, which is
        // Desert/Tests/Engine/CloudField walking 48 x 48 columns and printing what it asserts:
        //
        //     strength   0.40   0.45   0.50   0.55   0.60   0.65   0.70   0.80   1.00
        //     travel     101    109    117    124    131    139    145    157    180   m
        //     vs floor  0.81x  0.87x  0.93x  0.99x  1.05x  1.11x  1.16x  1.26x  1.44x
        //     dissolved 0.054  0.056  0.060  0.061  0.065  0.068  0.075  0.079  0.088
        //
        // 0.65 IS THE FIRST STEP WITH REAL HEADROOM AND THAT IS §DS'S OWN CONVENTION, not a new one: §DS
        // shipped 0.40 because 0.35 cleared its floor by ONE metre and 0.40 cleared it by fourteen — 1.11x.
        // Here 0.60 clears by six metres (1.05x) and 0.65 by fourteen (1.11x). The two calibrations land on
        // the same headroom because they are the same rule applied twice.
        //
        // ⚠️ THE ESTIMATE THIS REPLACES WAS 0.60 AND IT WAS CHECKED RATHER THAN TAKEN. §SIL's report
        // estimated "about 0.60" from two anchor points it measured correctly (116.7 m at 0.50, 157.1 m at
        // 0.80 — both reproduce here to a tenth of a metre) and then interpolated between them badly: even
        // straight-line interpolation of that pair needs 0.66. Measured, 0.60 delivers 1.05x, which is the
        // balanced-on-the-bound case §DS looked at and refused BY NAME, because a one-per-cent margin makes
        // the suite fail on any change to the generator that moves a body by a voxel.
        //
        // WHAT IT COSTS THE LIBRARY, WHICH IS §DS'S QUESTION ASKED A SECOND TIME. The cut's depth is
        // `clamp(DetailStrength * DetailFactor, 0, 1)`, so raising this multiplies EVERY type's cut by
        // 1.625. The two types §DS re-based because a deep cut DELETES them rather than shredding them are
        // re-based again by the same ratio, which holds their cut depth exactly where it has been since
        // before §DS: cirrus 0.625 -> 0.3846154 and altocumulus 0.40 -> 0.2461538, so
        //
        //     cirrus       0.65 x 0.3846154 = 0.250 = 0.40 x 0.625 = 0.10 x 2.50
        //     altocumulus  0.65 x 0.2461538 = 0.160 = 0.40 x 0.40  = 0.10 x 1.60
        //
        // Desert/Tests/Engine/CloudType asserts that identity on the shipped assets rather than trusting
        // the arithmetic in this comment, and §SIL2 verifies it on the FRAME with the lump held fixed, so
        // that the one variable it is about is alone.
        //
        // AND THE PAIR IS A TESTED RELATION NOW. Desert/Tests/Engine/CloudField's
        // `TheLumpsAspectAndTheErosionsStrengthAreOneCalibrationAndNotTwoNumbers` reads this default and
        // Assets::kCloudLumpVerticalOverHorizontal together, bakes the volume they produce and asserts the
        // travel their PRODUCT delivers still clears the march. Moving either one alone is red, and the
        // message names the other.
        float DetailStrength = 0.65f;

        PROPERTY( DisplayName( "Density Scale" ), Category( "Detail" ), Range( 0.0f, 2.0f ),
                  Tooltip( "Multiplies the eroded density, for the LAYER. Below 1 the whole layer thins "
                           "toward haze; above 1 the thin edges fill in. The cloud type multiplies this by "
                           "how much water that kind of cloud is made of, so 1 keeps meaning \"this type as "
                           "it is\" whichever type is in the slot." ) )
        float DensityScale = 1.0f;

        PROPERTY( DisplayName( "Extinction Scale" ), Category( "Detail" ), Units( "/km" ), Range( 0.5f, 60.0f ),
                  Tooltip( "How strongly the medium absorbs and scatters, per kilometre at full density. "
                           "This is what makes a cloud opaque rather than merely visible. The cloud type "
                           "multiplies it: ice at a quarter of a cumulus, a storm at a third above it." ) )
        // EIGHT, NOT FORTY-FIVE, and the difference is the approximation rather than the physics. A real
        // cumulus extinguishes at roughly 45 per kilometre, at which the optical depth toward the sun is
        // ~25 everywhere inside the body and EVERY scattering order arrives at zero — the cloud renders
        // uniformly grey. What makes a real one white is a random walk of photons at an albedo of
        // 0.9999, which three octaves do not reproduce and are not meant to. This is therefore the
        // EFFECTIVE extinction of the approximation: chosen so that a kilometre of cloud is opaque along
        // the view ray while the shadow ray still resolves a lit top and a darker base. First set from a
        // guess of 25 and corrected against the frame, which is the only instrument that measures it.
        float ExtinctionScale = 8.0f;

        PROPERTY( DisplayName( "Near Fade Start Distance" ), Category( "Detail" ), Length,
                  Range( 0.0f, 2000000.0f ), Advanced,
                  Tooltip( "Where the near-camera fade begins. A camera that enters the layer otherwise "
                           "meets a wall of density at arm's length; UE fades the nearest metres out for "
                           "the same reason. The fade is OFF unless End is strictly past Start." ) )
        float NearFadeStartDistance = 0.0f;

        PROPERTY( DisplayName( "Near Fade End Distance" ), Category( "Detail" ), Length, Range( 0.0f, 2000000.0f ),
                  Advanced,
                  Tooltip( "Where the near-camera fade is complete and the cloud is at full density. It "
                           "must lie strictly past Start; at or below it the pair describes no interval "
                           "and the fade is switched OFF rather than guessed at." ) )
        // THE TWO ARE ONE SETTING. Graphic::CloudResolveNearFade repairs them as a pair, because the march
        // evaluates smoothstep(Start, End, t) and GLSL leaves that undefined unless End is strictly past
        // Start — and each of the two is individually legal at any value its own slider allows.
        float NearFadeEndDistance = 0.0f;

        // ---- Lighting -------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Scattering Albedo" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Tooltip( "Fraction of extinguished light that is scattered rather than absorbed. Water "
                           "droplets barely absorb at all, which is why clouds are white; values much "
                           "below 1 read as smoke." ) )
        // 0.98 — the value UE's shipped instance carries (Cloud_AlbedoColor). Water droplets barely
        // absorb, and the difference between 0.95 and 0.98 is not three per cent: it compounds over
        // every scattering order, so the lower value reads as a cloud made of smoke.
        float ScatteringAlbedo = 0.98f;

        PROPERTY( DisplayName( "Phase G" ), Category( "Lighting" ), Range( -0.9f, 0.9f ),
                  Tooltip( "Asymmetry of the Henyey-Greenstein phase function. Positive scatters forward, "
                           "which is what puts the bright rim on a cloud you are looking at through the "
                           "sun." ) )
        // 0.8 — UE's shipped value (Phase_Controls.r), paired with the second lobe below.
        float PhaseG = 0.8f;

        PROPERTY( DisplayName( "Phase G Backward" ), Category( "Lighting" ), Range( -0.9f, 0.9f ),
                  Tooltip( "Asymmetry of the SECOND phase lobe. Near zero it is almost isotropic, which is "
                           "what carries the body of the cloud while the first lobe carries the bright rim "
                           "against the sun. One lobe cannot do both: strong enough for the rim leaves the "
                           "body black, weak enough for the body loses the rim." ) )
        float PhaseGBackward = 0.1667f;

        PROPERTY( DisplayName( "Phase Blend" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How much of the second lobe is mixed in. UE's shipped instance weights it "
                           "toward the BODY at 0.575, so more than half the answer is the near-isotropic "
                           "lobe and the sharp one is a highlight on top." ) )
        float PhaseBlend = 0.575f;

        PROPERTY( DisplayName( "Ambient Occlusion Strength" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How strongly the sky light reaching a sample is occluded. Which occluder is "
                           "measured is Sky Occlusion Volume's choice; this is how much of it is applied, "
                           "either way. At 0 the core of a three-kilometre cumulus is lit as brightly as a "
                           "wisp on its edge, which reads as a flat white cut-out." ) )
        // 0.5 — the amount UE carries in the alpha of Cloud_AlbedoColor.
        //
        // ONE KNOB, TWO GEOMETRIES, and it stays one knob deliberately. The field below chooses whether
        // the occlusion is computed from the sample's own depth inside its body or from the cloud standing
        // over its column; this number is the blend toward whichever answer that is. A second strength
        // beside it would be a parameter whose only job is to say the same thing twice, and the day one of
        // them is at 0.5 and the other at 1.0 nobody can say what the sky is supposed to look like.
        // Read by Editor/Resources/Shaders/Programs/Clouds/CloudRaymarch.shader.
        float AmbientOcclusionStrength = 0.5f;

        PROPERTY( DisplayName( "Sky Occlusion Volume" ), Category( "Lighting" ),
                  Tooltip( "Occlude the sky light by the cloud STANDING OVER a sample instead of by the "
                           "sample's own depth inside its body. Off, a sample under three kilometres of "
                           "congestus receives exactly what a sample under clear sky receives, so a deck "
                           "has no dark side and the clouds read as flat white lobes. On, a second compute "
                           "pass builds a 128x16x128 volume of how much cloud stands over every column and "
                           "the march reads it — which costs one dispatch and two megabytes per view.\n\n"
                           "Off by default: it changes how every cloud in a scene is lit, and no scene "
                           "authored before it existed was lit that way." ) )
        // DEFAULT OFF, and the frame with it off is the frame without this feature — the march's gate is a
        // push-constant flag and the pass is not dispatched, so nothing is allocated and nothing is read.
        // That is the same arrangement Unreal ships its own second volume under and the same one this
        // programme used for per-sample sun transmittance, and it is what makes the A/B in the report a
        // property of one binary rather than of two.
        //
        // WHY IT IS A FLAG AND NOT A REPLACEMENT. The local term is not wrong — Р0 measured it recovering
        // 34 % of the gap at the sunward zenith, which is exactly where the occluder IS the sample's own
        // body — and the volume costs a dispatch that a scene with a thin cirrus veil has no use for.
        // Read by Engine/Graphic/Systems/Scene/Clouds/VolumetricCloudRenderer.cpp, which decides whether
        // to dispatch Editor/Resources/Shaders/Programs/Clouds/CloudSkyOcclusionVolume.shader, and by
        // Editor/Resources/Shaders/Programs/Clouds/CloudRaymarch.shader through CloudPush::SkyOcclusion.
        bool SkyOcclusionVolume = false;

        PROPERTY( DisplayName( "Light March Distance" ), Category( "Lighting" ), Length,
                  Range( 10000.0f, 2000000.0f ),
                  Tooltip( "How far toward the sun the shadow ray marches. Short rays light the cloud "
                           "flatly because they never leave its own body; long ones cost proportionally "
                           "more per sample, and past the thickness of the layer they buy nothing at all "
                           "because the ray has already left it." ) )
        // FIFTEEN KILOMETRES, which is UE's ShadowTracingDistance, and the previous 500 m was the reason
        // the clouds were flat. A shadow ray started inside a two-kilometre cloud and only 500 m long
        // never leaves it: every sample inside the body sees roughly the same optical depth, so the body
        // is shaded uniformly and reads as a cut-out. At fifteen kilometres a sample near the top exits
        // into clear air almost at once and is bright, while one near the base has the whole cloud above
        // it and is dark — and that difference IS the shape.
        //
        // LENGTHENING THIS RAY COARSENS ITS NEAR FIELD BY THE SAME FACTOR, and the sample count has to
        // move with it. The samples are on a SQUARED distribution, so over a march of length M with N
        // samples the FIRST segment is M / N^2 — the resolution nearest the shaded point, which is where
        // almost all of the material is. At the old 500 m and six samples that was 13.9 m; at fifteen
        // kilometres and the same six it is 417 m, thirty times coarser, and a shadow ray whose first
        // step steps straight over the cloud it starts inside reports the cloud transparent to the sun.
        // See LightMarchSamples below and Docs/Clouds/CALIBRATION.md section OE-FIX for the frames.
        float LightMarchDistance = 1500000.0f; // 15 km

        PROPERTY( DisplayName( "Light March Samples" ), Category( "Lighting" ),
                  Range( 1, ::Desert::ECS::kCloudLightMarchMaxSamples ),
                  Tooltip( "Samples along the shadow ray toward the sun. They are placed on a squared "
                           "distribution, so the first segment is the march length over the SQUARE of "
                           "this number — which is why a long ray needs many more of them than a short "
                           "one. Below about 24 the sunward highlights blow out; above 48 nothing "
                           "measurable changes. This is the hottest loop in the subsystem: the ray is "
                           "traced at every sample inside every cloud, and the cost is linear in this "
                           "number." ) )
        // THIRTY-TWO, and the number is a measurement rather than a preference. Rendered p95 of the
        // zenith frame looking INTO the sun, Clouds_Demo, in linear scene radiance after exposure (the
        // 8-bit scale is nearly flat above 0.95 and hides the size of this entirely):
        //
        //     N          6      10      16      24      32      48      64
        //     linear   4.29    2.27    1.29    1.05    0.99    0.97    0.97
        //     x conv   4.42    2.35    1.34    1.08    1.02    1.00    1.00
        //
        // Thirty-two is the first value on the plateau — 2% from converged, and 1% from the 0.979 the UE
        // reference frame's own p95 implies. Sixteen, the ceiling this component used to carry, is still
        // 34% too bright; ten, which is Unreal's BaseShadowRaySampleCount, is still 135% too bright on
        // OUR quadrature. The away-from-sun azimuth converges by ten and hides the whole defect, which is
        // why it went unseen: the error is a multiplier on sun visibility and the sunward phase function
        // is ~16x the away one.
        //
        // THE COST IS REAL AND IS THE ENTRY TO A QUALITY TIER. Measured by the frame-count slope on a
        // debug build at 1280x766 (machine shared): 0.23 ms of frame time per shadow sample, so 6 -> 32
        // is 7.1 -> 12.9 ms/frame, +81%. A tier that wants the speed back should lower THIS number first
        // — 24 costs 4.0 ms less than 32 and is 7% from converged — and it is the reason the range now
        // reaches 64 rather than stopping where the defect lived.
        int32_t LightMarchSamples = 32;

        PROPERTY( DisplayName( "Multiple Scattering Octaves" ), Category( "Lighting" ), Range( 1, 3 ),
                  Tooltip( "How many scattering orders are approximated. ONE IS SINGLE SCATTERING, and a "
                           "cloud lit by single scattering alone is physically grey: the light that makes "
                           "a real cloud white has bounced many times inside it. Two or three is where it "
                           "starts to look like weather." ) )
        int32_t MultiScatterOctaves = 3;

        PROPERTY( DisplayName( "Multiple Scattering Contribution" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Advanced,
                  Tooltip( "How much each successive scattering order contributes. The factor is SQUARED "
                           "at every octave, so the series falls away quickly and the third order is "
                           "already a small correction." ) )
        // 0.667 — the value UE's shipped instance carries (Multiscatter_Controls.r).
        float MultiScatterContribution = 0.667f;

        PROPERTY( DisplayName( "Multiple Scattering Occlusion" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Advanced,
                  Tooltip( "How much less each successive order is absorbed. This is what lets light that "
                           "has already scattered reach the core of a cloud that the direct beam never "
                           "gets into — the reason a thick cumulus glows rather than going black." ) )
        // 0.25 — UE's value (Multiscatter_Controls.g), and the single largest discrepancy the reference
        // exposed. At 0.5 each successive scattering order was absorbed twice as hard as it should be,
        // so light never reached the core and the cloud read grey rather than white. Halving it is what
        // lets the deeper orders light the body from inside.
        float MultiScatterOcclusion = 0.25f;

        PROPERTY( DisplayName( "Multiple Scattering Eccentricity" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Advanced,
                  Tooltip( "How much directionality each successive order keeps. Light that has bounced "
                           "many times has forgotten where it came from, so the higher orders blend "
                           "toward an isotropic phase." ) )
        // 0.18 — UE's value (Multiscatter_Controls.b). The higher orders go almost straight to isotropic,
        // which is physically right: light that has bounced twice has forgotten the sun.
        float MultiScatterEccentricity = 0.18f;

        PROPERTY( DisplayName( "Aerial Perspective Start Distance" ), Category( "Lighting" ), Length,
                  Range( 0.0f, 20000000.0f ), Advanced,
                  Tooltip( "Distance before the atmosphere begins to haze the clouds. At 0 — the physical "
                           "answer, and UE's default — ninety kilometres of air erases a cloud on the "
                           "horizon completely, which is correct and is not always what a sky is wanted to "
                           "look like. Pushing it out keeps the distant band visible." ) )
        float AerialPerspectiveStartDistance = 0.0f;

        PROPERTY( DisplayName( "Aerial Perspective Fade Distance" ), Category( "Lighting" ), Length,
                  Range( 0.0f, 20000000.0f ), Advanced,
                  Tooltip( "Distance over which the haze reaches full strength once it has started. Zero "
                           "applies it in full immediately." ) )
        float AerialPerspectiveFadeDistance = 0.0f;

        PROPERTY( DisplayName( "Ambient Scale" ), Category( "Lighting" ), Color,
                  Tooltip( "Scales the sky's ambient contribution to the clouds. White is the full "
                           "contribution; black lights them by the sun alone and leaves their shadowed "
                           "sides black." ) )
        glm::vec3 AmbientScale = { 1.0f, 1.0f, 1.0f };

        // ---- Shadows --------------------------------------------------------------------------------
        //
        // THE LAYER SHADING THE WORLD UNDER IT, through the map described in
        // Editor/Resources/Shaders/Common/CloudShadowMap.glslh. Two fields and no more, and the two that
        // are absent are absent for a stated reason:
        //
        //   * THE MAP'S EXTENT AND RESOLUTION are engine constants, like the step schedule and for the
        //     same reason (Common/CloudGeometry.glslh): they trade cost against quality identically in
        //     every scene, which is why Unreal carries them as cvars. They are also not free to choose —
        //     the texel is derived from the finest cloud chord the march can resolve, so an artist moving
        //     one of them would be moving a number the producer's own step schedule fixes.
        //   * THE SKY-LIGHT OCCLUSION under a deck is a DIFFERENT quantity with a different geometry (a
        //     hemisphere rather than a direction) and it is still not approximated with this map, because
        //     a directional occlusion applied to an omnidirectional term is wrong in a way that looks
        //     tuned rather than broken. It has its OWN volume now — Sky Occlusion Volume in the Lighting
        //     group above, built by Programs/Clouds/CloudSkyOcclusionVolume.shader — which is where Р4
        //     took the "named as out of scope rather than half-done" note this paragraph used to end on.

        PROPERTY( DisplayName( "Cast Shadows" ), Category( "Shadows" ), Summary,
                  Tooltip( "Whether the layer shades the world beneath it. Off dispatches nothing and "
                           "allocates nothing: a scene with this off pays exactly what a scene with no "
                           "clouds pays for the shadow map, which is zero." ) )
        bool CastShadows = true;

        PROPERTY( DisplayName( "Shadow Strength" ), Category( "Shadows" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How strongly the cloud shadow darkens the sun on the world beneath it. It "
                           "scales the OPTICAL DEPTH, not the resulting light, so 0.5 is half as much "
                           "cloud in the way rather than half the darkness — which is what keeps a thin "
                           "cloud thin and a thick one thick as the dial moves. At 0 the pass is skipped "
                           "entirely, exactly as if Cast Shadows were off." ) )
        // ONE, WHICH IS UNREAL'S DEFAULT (CloudShadowStrength on the directional light) and is also the
        // only value that is physics rather than art: the map holds the medium's own extinction, so 1 is
        // the shadow the cloud that is drawn in the sky actually casts. It is a dial at all because the
        // approximation the layer is lit BY is not energy-exact either, and a sky that wants its ground
        // back should be able to say so with a number rather than by turning the feature off.
        float ShadowStrength = 1.0f;

        // ---- Quality --------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Max Steps" ), Category( "Quality" ), Range( 8, 512 ),
                  Tooltip( "Ceiling on the number of samples along a view ray. The count itself rises "
                           "with the length of the segment inside the layer and saturates at this "
                           "value, so it is a cost ceiling rather than a fixed cost." ) )
        // 256, WHICH IS AFFORDABLE ONLY BECAUSE OF THE SKIP. The march has two step sizes and spends the
        // coarse one — four times longer — on empty sky, so a ray that crosses mostly clear air finishes
        // in a quarter of these iterations. Raising the ceiling therefore buys resolution INSIDE cloud
        // without charging for it outside, which is the opposite of what raising it did before the skip
        // existed.
        int32_t MaxSteps = 256;

        PROPERTY( DisplayName( "Stop Transmittance" ), Category( "Quality" ), Range( 0.0f, 0.2f ), Advanced,
                  Tooltip( "The march stops once this little light is still getting through. Raising it "
                           "is usually the cheapest saving available, because the samples it skips are "
                           "behind material that has already hidden them." ) )
        float StopTransmittance = 0.005f;

        // ---- Animation ------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Wind Direction" ), Category( "Animation" ),
                  Tooltip( "Direction the layer drifts. Normalized by the renderer; a zero vector simply "
                           "leaves the sky still." ) )
        glm::vec3 WindDirection = { 1.0f, 0.0f, 0.0f };

        PROPERTY( DisplayName( "Wind Speed" ), Category( "Animation" ), Length, Range( 0.0f, 50000.0f ),
                  Tooltip( "How fast the layer drifts, in world units per second. The wind moves the "
                           "SAMPLE POSITION rather than the data, which is what makes the motion free and "
                           "seamless." ) )
        float WindSpeed = 3000.0f; // 30 m/s
    };

    /**
     * @brief The layer's PLACEMENT LATTICE in kilometres — Weather Tile Size divided by four.
     *
     * FOUR CELLS TO A TILE is a ratio the component's own tooltip has stated since phase T1 ("12 km ->
     * 3 km cells, a cumulus field") and that Graphic::System::VolumetricCloudRenderer turns into
     * `CloudProceduralSpecies::CellKm`. It is stated HERE, once, because a second reader appeared: the
     * Cloud Layout panel measures a painting's strokes against the cell, and a panel that computed the
     * ratio for itself would quote a resolution the sky does not have the day somebody changes it.
     *
     * A TYPE'S Placement Scale STILL MULTIPLIES IT — this is the layer's own lattice, before the kind of
     * cloud in a slot says how much coarser or finer than the layer it is.
     */
    inline float CloudLayerLatticeKm( const VolumetricCloudData& data )
    {
        // 100 000 world units to the kilometre, the project-wide centimetre convention. Written out
        // rather than taken from Graphic::kCloudWorldUnitsPerKm because Engine/ECS must not depend on
        // Engine/Graphic, and the number is the unit of the whole project rather than the renderer's.
        constexpr float unitsPerKm = 100000.0f;
        return ( data.WeatherTileSize > 1.0f ? data.WeatherTileSize : 1.0f ) / unitsPerKm / 4.0f;
    }

    /// The four cloud type fields of a layer, in the order the Details panel draws them. One statement of
    /// that order, so nobody spells `{ CloudType1, CloudType2, CloudType3, CloudType4 }` a second time.
    constexpr uint32_t kCloudTypeSlots = 4u;

    /**
     * @brief Which of a layer's four cloud type slots become SPECIES, and in what order.
     *
     * WHY THIS IS NOT THE IDENTITY, AND WHY THAT MATTERS OUTSIDE THE RENDERER. The four slots an artist
     * fills in Details are COMPACTED before they reach the field: an empty slot is skipped, and the same
     * type twice is dropped, because two identical placement fields are two skies of one kind of cloud at
     * twice the cost. So a layer whose only authored type sits in `Cloud Type 3` has exactly ONE species,
     * and that species is number ZERO.
     *
     * THAT RENUMBERING IS A CONVENTION THE ARTIST CAN SEE THE CONSEQUENCES OF. A painted layout's channels
     * are indexed by SPECIES, so in the layer above it is the painting's RED channel that decides where
     * `Cloud Type 3` goes — not its blue one. The Cloud Layout panel names the type behind each channel
     * rather than leaving the artist to discover it from a sky, and it can only do that because the rule
     * lives here instead of inside the renderer that used to own it.
     */
    struct CloudSpeciesResolution
    {
        /// How many species the layer has, 1..kCloudTypeSlots. Never zero: see BuiltInDefault.
        uint32_t Count = 0u;

        /// For each species, which of the four Details slots it came from. Meaningless when
        /// BuiltInDefault, which is why that flag exists rather than a sentinel slot index.
        uint32_t AuthoredSlot[kCloudTypeSlots] = { 0u, 0u, 0u, 0u };

        /// True when NO slot is authored. The layer then has one species — the engine's built-in cumulus
        /// congestus — because a scene nobody has chosen a type for still has to have a sky.
        bool BuiltInDefault = false;
    };

    inline CloudSpeciesResolution ResolveCloudSpecies( const VolumetricCloudData& data )
    {
        const Assets::AssetHandle authored[kCloudTypeSlots] = { data.CloudType1, data.CloudType2, data.CloudType3,
                                                                data.CloudType4 };

        CloudSpeciesResolution resolved;

        for ( uint32_t slot = 0; slot < kCloudTypeSlots; ++slot )
        {
            if ( authored[slot] == Assets::AssetHandle::Null() )
                continue;

            bool seen = false;
            for ( uint32_t taken = 0; taken < resolved.Count; ++taken )
                seen = seen || authored[resolved.AuthoredSlot[taken]] == authored[slot];
            if ( seen )
                continue;

            resolved.AuthoredSlot[resolved.Count] = slot;
            ++resolved.Count;
        }

        if ( resolved.Count == 0u )
        {
            resolved.Count          = 1u;
            resolved.BuiltInDefault = true;
        }

        return resolved;
    }

    struct VolumetricCloudComponent
    {
        VolumetricCloudData Data;
    };
} // namespace Desert::ECS
