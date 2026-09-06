#pragma once

#include <cstddef>
#include <string_view>

namespace Desert::Editor
{
    /**
     * @brief NAMED VIEWPOINTS for a document's preview — the replacement for `--preview-orbit yaw,pitch`.
     *
     * WHY NAMES AND NOT NUMBERS. The flag took a continuous angle pair, and a continuous parameter is the
     * one thing the command palette cannot express: a PaletteCommand is a group, a label and a closure,
     * with nowhere for an argument to go. Giving it one would have been a second way to invoke a command
     * — the exact shape this channel exists to remove — so the capability is decomposed into the handful
     * of viewpoints anybody actually asked for instead.
     *
     * The trade is smaller than it looks, and it runs the RIGHT WAY. "160,15" has to be remembered,
     * written down and passed between people to reproduce a shot; "Front" is Front in every run, on every
     * machine, for every developer, and this table is what makes that a fact rather than a habit. What is
     * given up is an arbitrary angle, not repeatability.
     *
     * It is also, plainly, what a person wants: every modelling tool ships these six buttons, and the
     * reason the preview did not have them is that nobody had needed them from a keyboard before.
     *
     * Pure data, so the table is assertable: the suite checks the names are unique and the poles are
     * inside the pitch limit the mouse itself obeys.
     */
    struct PreviewViewpoint
    {
        std::string_view Name;
        float            YawDegrees;
        float            PitchDegrees;
    };

    /// The pitch a preview may be put at, in degrees. The same limit PreviewViewport::SetOrbit clamps a
    /// dragged orbit to; naming it here is what stops "Top" being authored outside it and silently
    /// becoming something else on arrival.
    inline constexpr float kPreviewPitchLimitDegrees = 89.0f;

    inline constexpr PreviewViewpoint kPreviewViewpoints[] = {
         { "Front", 0.0f, 0.0f },
         { "Back", 180.0f, 0.0f },
         { "Left", 90.0f, 0.0f },
         { "Right", -90.0f, 0.0f },
         { "Top", 0.0f, 89.0f },
         { "Bottom", 0.0f, -89.0f },
         // The angle a product shot is taken from, and the one `--preview-orbit` was most often given:
         // enough of a turn to separate the object from the background, enough tilt to show its top.
         { "Three-Quarter", 35.0f, 20.0f },
    };

    /// The viewpoint called @p name, or nullptr. Null is a caller naming one that does not exist, which
    /// its own refusal must report — never a quiet fall back to Front, which would look exactly like a
    /// preview that ignored the request.
    [[nodiscard]] inline const PreviewViewpoint* FindPreviewViewpoint( std::string_view name )
    {
        for ( const PreviewViewpoint& viewpoint : kPreviewViewpoints )
            if ( viewpoint.Name == name )
                return &viewpoint;
        return nullptr;
    }
} // namespace Desert::Editor
