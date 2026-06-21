#pragma once

#include <string_view>

namespace Desert::Graphic
{
    enum class RenderPhase
    {
        None = 0,

        DepthPrePass,
        Sky,
        Geometry,
        Outline,
        Decals,
        Lighting,
        Transparency,
        PostProcess,
        Overlay,
        UI,
        Debug,

        Count
    };

    inline std::string_view RenderPhaseToString( RenderPhase phase )
    {
        switch ( phase )
        {
            case RenderPhase::DepthPrePass:  return "DepthPrePass";
            case RenderPhase::Sky:           return "Sky";
            case RenderPhase::Geometry:      return "Geometry";
            case RenderPhase::Outline:       return "Outline";
            case RenderPhase::Decals:        return "Decals";
            case RenderPhase::Lighting:      return "Lighting";
            case RenderPhase::Transparency:  return "Transparency";
            case RenderPhase::PostProcess:   return "PostProcess";
            case RenderPhase::Overlay:       return "Overlay";
            case RenderPhase::UI:            return "UI";
            case RenderPhase::Debug:         return "Debug";
            default:                         return "None";
        }
    }
}