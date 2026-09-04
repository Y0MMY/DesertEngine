#include "PBRSceneFrame.hpp"

namespace Desert::Graphic
{
    void PBRSceneFrame::ApplyTo( MaterialInstance* instance ) const
    {
        if ( !instance )
            return;

        MaterialPBRBase::UpdateCamera( instance, Camera );
        if ( PointLights && SpotLights && DirectionLights )
            MaterialPBRBase::UpdateLights( instance, *PointLights, *SpotLights, *DirectionLights );

        // The map array is handed over as-is (`Image2D* const*`) rather than copied into a local: a copy
        // is where a fifth cascade would get lost, because a hand-written brace list does not grow with
        // kMaxCascades and does not fail to compile when it stops matching.
        MaterialPBRBase::UpdateShadow( instance, CascadeViewProj, CascadeMaps, MaterialPBRBase::kMaxCascades,
                                       ShadowBias, ShadowsEnabled, ShadowDebugMode, ShowNormals, CascadeTexelWorld,
                                       LightingDebug );

        MaterialPBRBase::UpdateEnvironment( instance, IrradianceMap, PrefilteredMap, BrdfLut );
        MaterialPBRBase::UpdateCloudShadow( instance, CloudShadow );
    }
} // namespace Desert::Graphic
