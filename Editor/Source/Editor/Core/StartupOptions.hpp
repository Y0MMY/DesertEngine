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

        /// `--open-panel <name-or-asset-path>`, repeatable. Matched against IPanel::GetName() exactly; a
        /// value that names no panel is then tried as a path to an asset with a document editor (a `.demat`
        /// opens the Material Editor on it). An asset DOCUMENT cannot be named at boot the way a tool can —
        /// it does not exist until something opens the asset — and this flag is the only way to put one on
        /// screen unattended, which is what every capture of one depends on. See
        /// EditorLayer::OnAttach for the resolution order and the error that names both kinds.
        std::vector<std::string> PanelsToOpen;

        /// `--select <name-or-uuid>`: the entity that is SELECTED once the scene has settled, as though a
        /// person had clicked it in the outliner. Empty = select nothing, which is the boot the editor has
        /// always had.
        ///
        /// WHY A FLAG. The Details panel draws whatever `SelectionManager` holds, so with nothing selected
        /// it draws an empty column — and nothing could select an entity without a mouse. Synthetic input
        /// is not an option and that is measured, not assumed: `osascript` answers "not allowed to send
        /// keystrokes. (1002)", because a build agent has no macOS assistive access. The consequence was
        /// larger than any one capture: NO change to the Details panel was provable by a frame at all, and
        /// two developers in a row owed a picture they had no way to take.
        ///
        /// Resolved against the entity's Tag first and its UUID second — the name is what a person reads in
        /// the outliner, the UUID is what survives a rename. A value matching NEITHER ends the run
        /// non-zero, naming it; selecting nothing quietly would be the same silent fallback as loading the
        /// wrong scene, just harder to see, because an empty Details panel is what an unselected editor
        /// looks like anyway.
        std::string SelectEntity;
    };
} // namespace Desert::Editor
