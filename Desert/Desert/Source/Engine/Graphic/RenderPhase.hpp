#pragma once

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
}