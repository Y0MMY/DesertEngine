#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Desert::Editor
{
    // One entry in the command palette. Group is a short category ("View", "Entity", "Action") shown
    // dimmed beside the label; Run is invoked when the entry is chosen.
    struct PaletteCommand
    {
        std::string           Group;
        std::string           Label;
        std::function<void()> Run;
    };

    // A Ctrl+P "go to anything" overlay. The owner calls Open() on the hotkey, fills SetCommands() ONCE
    // per opening with that moment's candidates (panels, entities, actions, openable files), and calls
    // Draw() every frame — Draw only renders while open. Filtering/ranking uses FuzzyMatch; Up/Down move,
    // Enter runs, Esc closes.
    class CommandPalette
    {
    public:
        void Open();
        bool IsOpen() const
        {
            return m_Open;
        }

        /**
         * @brief "Has this been opened since you last filled it?" — true exactly once per opening.
         *
         * THE OWNER USED TO REBUILD THE DICTIONARY EVERY FRAME the overlay was up, sixty times a second
         * while somebody typed, and the dictionary is not cheap: it enumerates the scene's entities, the
         * open documents, the levels on disk and — since A6-1 — every openable file under the project's
         * content root. Two recursive directory walks per frame, for a list that cannot change while the
         * palette holds the keyboard.
         *
         * A SEPARATE FLAG FROM m_JustOpened, which Draw() consumes for the ImGui popup and the keyboard
         * focus. Sharing one would make the two consumers race: whichever ran first would clear it, and
         * either the palette would open unfocused or it would open showing the PREVIOUS session's list.
         */
        [[nodiscard]] bool TakeJustOpened()
        {
            const bool opened = m_NeedsCommands;
            m_NeedsCommands   = false;
            return opened;
        }

        void SetCommands( std::vector<PaletteCommand> commands )
        {
            m_Commands = std::move( commands );
        }

        void Draw();

    private:
        bool                        m_Open       = false;
        bool                        m_JustOpened = false;
        // Set by Open(), cleared by TakeJustOpened(). See there for why it is not m_JustOpened.
        bool                        m_NeedsCommands = false;
        int                         m_Selected   = 0;
        char                        m_Query[128] = {};
        std::vector<PaletteCommand> m_Commands;
    };
} // namespace Desert::Editor
