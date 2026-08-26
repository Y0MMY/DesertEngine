#pragma once

#include <string>
#include <vector>

namespace Desert::Editor
{
    /**
     * @brief Command-line state that puts the editor in a KNOWN state at boot, other than the camera.
     *
     * WHY IT EXISTS, and it is the same argument ShotOptions carries next door. The editor already has a
     * family of arguments — `--scene`, `--camera`, `--look`, `--shot` — whose whole purpose is to reach a
     * particular state without a person driving the mouse, because the defects that matter most are the
     * ones somebody catches by LOOKING and the people looking are outnumbered by the parameters.
     * `--open-panel` is that family's entry for the panels: it opens a tool by the name the View menu
     * shows it under, so that "here is the Cloud Layout panel on this sky" is one command rather than a
     * sequence of clicks nobody can repeat.
     *
     * It is equally an argument for a person: an artist who lives in one tool can put it on the command
     * line and skip the menu.
     *
     * A NAME NOBODY RECOGNISES IS AN ERROR NAMING THE KNOWN ONES, never a silent no-op — a flag that
     * quietly did nothing would look exactly like a panel that failed to draw.
     */
    struct StartupOptions
    {
        static StartupOptions& Get()
        {
            static StartupOptions s;
            return s;
        }

        /// `--open-panel <name>`, repeatable. Matched against IPanel::GetName() exactly.
        std::vector<std::string> PanelsToOpen;
    };
} // namespace Desert::Editor
