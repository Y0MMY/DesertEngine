#pragma once

#include <Engine/Assets/Common.hpp>
#include <Engine/Reflection/ReflectionMacros.hpp>

#include <Common/Core/Units.hpp>

namespace Desert::ECS
{
    // A placed HERO CLOUD: one baked `.dvol` volume, positioned by the entity's own transform, whose
    // density is unioned over the procedural cloud deck (VOXEL_CLOUD_PATH.md §1.5, teamlead Q1 —
    // `max(procedural, voxel)`). The deck stays the default and the fallback; a scene with no Cloud
    // Volume entity pays nothing.
    //
    // THE ENTITY'S TRANSFORM IS THE CLOUD'S SIZE AND POSITION, and this component deliberately has no
    // extent, radius or altitude field of its own. That is the teamlead's Q2 answer: the world extent a
    // tile covers is a per-instance transform, so a closer fly-by is a different SCALE on the same
    // asset, not a different atlas. The `.dvol` header carries the extent it was baked to cover
    // (1024 x 1024 x 512 m by default, 8 m per voxel); the transform's scale multiplies it, the
    // transform's translation places its centre, and the transform's rotation turns it.
    //
    // THE VOLUME'S LOCAL AXES: X and Y are the two wide axes, and LOCAL Z IS UP. That is not the
    // engine's Y-up convention and it is not an accident — the `.dvol` is a 3D texture whose third
    // dimension is the small one (128 x 128 x 64), which is what makes a cloud wide and thin rather
    // than tall and narrow, and it matches the reference deck's own 512 x 512 x 64 layout (p. 85). The
    // instance transform is what maps local Z onto world up.
    //
    // WHAT THIS COMPONENT DOES NOT DO. A baked cloud does not grow or dissipate: the reference gives
    // the voxel method "Evolution: Pseudomotion Only" (p. 187), and the teamlead accepted that trade
    // (Q7). The detail noise still scrolls over it and the transform can be animated; the SHAPE is
    // frozen. A cloud that must visibly build stays procedural.
    struct CloudVolumeData
    {
        REFLECT()

        PROPERTY( DisplayName( "Enabled" ), Category( "Cloud Volume" ), Summary,
                  Tooltip( "Master switch for this hero cloud. Off means the volume is not gathered, not "
                           "uploaded and not marched — the procedural cloud deck is unaffected either way." ) )
        bool Enabled = true;

        PROPERTY( DisplayName( "Volume" ), Category( "Cloud Volume" ), Asset<CloudVolumeAsset>,
                  EditCondition( "Enabled" ),
                  Tooltip( "The baked .dvol volume. Bake one from a shape description with the "
                           "CloudVolumeBaker tool; several entities may reference the same volume, and "
                           "they then share one atlas tile." ) )
        Assets::AssetHandle Volume;

        PROPERTY( DisplayName( "Density Scale" ), Category( "Cloud Volume" ), Range( 0.0f, 4.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Multiplies the volume's baked Density Scale channel. Lets one baked shape be "
                           "placed twice at different opacities without re-baking it." ) )
        float DensityScale = 1.0f;

        PROPERTY( DisplayName( "Detail Type Bias" ), Category( "Cloud Volume" ), Range( -1.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Shifts the volume's baked Detail Type channel toward wispy (negative) or "
                           "billowy (positive) before the erosion reads it. 0 uses the bake unchanged." ) )
        float DetailTypeBias = 0.0f;

        PROPERTY( DisplayName( "Casts Cloud Shadow" ), Category( "Cloud Volume" ), EditCondition( "Enabled" ),
                  Tooltip( "Whether this cloud is marched into the cloud shadow map as well as into the "
                           "view. Off is cheaper and is the right choice for a cloud far from anything "
                           "the shadow would fall on." ) )
        bool CastsCloudShadow = true;

        // THE DISTANCE LOD, and it is a hand-back rather than a resolution ladder. Phase 1b's measured
        // limit: at distance a hero reads SMOOTHER than the deck around it, because a hero is one
        // analytic body the erosion nibbles while a deck cloud is many weather cells intrinsically
        // shredded — and past a few kilometres the nibbling is sub-pixel for both, so what is left is the
        // silhouette, and the hero's is a blob. That is not an aliasing problem and no amount of
        // filtering fixes it; the answer is to give the frame back to the deck, which under the union is
        // already there behind the hero and already looks right at that distance.
        //
        // The fade is applied per CLOUD, from its centre's distance to the camera, and not per sample:
        // a cloud fading across its own body would leave one edge denser than the other. The renderer
        // folds the weight into the instance's density scale and drops the record entirely at zero
        // (Graphic::CloudVolumeFadeWeight, Graphic::VolumetricCloudRenderer::UpdateVolumeInstances), so a
        // faded hero costs no samples at all.
        // THE DEFAULTS ARE THE MEASURED CROSSOVER, not a round number. Clouds_HeroVolumes' congestus
        // tower — 2.2 km across, the largest thing the shipped content places — was rendered side-on at
        // 3, 6, 9, 13, 20 and 30 km against the deck beside it: it holds its cauliflower rim to 6 km,
        // is noticeably smoother at 9, is a smooth lozenge at 13 and is a featureless wall at 20 while
        // the weather-driven clouds either side of it are still shredded. 12 km to 18 km puts the hand-
        // back through the middle of that and completes it before the frame where it is unmistakable.
        PROPERTY( DisplayName( "Fade Start Distance" ), Category( "Cloud Volume" ), Length,
                  Range( 0.0f, Common::Units::Metres( 200000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance from the camera at which this hero cloud begins dissolving into the "
                           "procedural deck behind it. Inside this it is drawn at full density. The "
                           "default is where a kilometre-class baked cloud measurably stops holding up "
                           "against the deck around it; a larger or denser one can afford more." ) )
        float FadeStartDistance = Common::Units::Metres( 12000.0f );

        PROPERTY( DisplayName( "Fade End Distance" ), Category( "Cloud Volume" ), Length,
                  Range( 0.0f, Common::Units::Metres( 200000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance at which this hero cloud is gone and only the procedural deck is "
                           "marched there — it then costs no samples at all. Set it beyond the layer's "
                           "own view distance to disable the fade; set it equal to Fade Start Distance "
                           "for a hard cut." ) )
        float FadeEndDistance = Common::Units::Metres( 18000.0f );
    };

    struct CloudVolumeComponent
    {
        CloudVolumeData Data;
    };
} // namespace Desert::ECS
