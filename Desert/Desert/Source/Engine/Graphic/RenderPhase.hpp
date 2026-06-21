#pragma once

#include <cstdint>
#include <string_view>

namespace Desert::Graphic
{
    // Opaque phase identifier. Built-in phases are spaced by 100 so user phases
    // can be inserted between them without collisions.
    using RenderPhaseID = uint32_t;

    namespace RenderPhase
    {
        constexpr RenderPhaseID None         = 0;
        constexpr RenderPhaseID DepthPrePass = 100;
        constexpr RenderPhaseID Sky          = 200;
        constexpr RenderPhaseID Geometry     = 300;
        constexpr RenderPhaseID Outline      = 400;
        constexpr RenderPhaseID Decals       = 500;
        constexpr RenderPhaseID Lighting     = 600;
        constexpr RenderPhaseID Transparency = 700;
        constexpr RenderPhaseID PostProcess  = 800;
        constexpr RenderPhaseID Overlay      = 900;
        constexpr RenderPhaseID UI           = 1000;
        constexpr RenderPhaseID Debug        = 1100;

        // User-defined phases must start at k_UserBase.
        constexpr RenderPhaseID k_UserBase   = 10000;

        // Canonical declaration order used as tie-breaker seed in topological sort.
        inline constexpr RenderPhaseID k_BuiltinOrder[] = {
            DepthPrePass, Sky, Geometry, Outline, Decals,
            Lighting, Transparency, PostProcess, Overlay, UI, Debug
        };
        inline constexpr std::size_t k_BuiltinCount = std::size( k_BuiltinOrder );
    }

    // Returns the human-readable name for a phase ID.
    // Delegates to RenderPhaseRegistry, so user-registered phases are supported.
    std::string_view RenderPhaseToString( RenderPhaseID id );

} // namespace Desert::Graphic
