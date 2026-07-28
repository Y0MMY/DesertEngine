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

    // A Ctrl+P "go to anything" overlay. The owner calls Open() on the hotkey, fills SetCommands() with
    // the frame's candidates (panels, entities, actions), and calls Draw() once per frame — Draw only
    // renders while open. Filtering/ranking uses FuzzyMatch; Up/Down move, Enter runs, Esc closes.
    class CommandPalette
    {
    public:
        void Open();
        bool IsOpen() const
        {
            return m_Open;
        }

        void SetCommands( std::vector<PaletteCommand> commands )
        {
            m_Commands = std::move( commands );
        }

        void Draw();

    private:
        bool                        m_Open       = false;
        bool                        m_JustOpened = false;
        int                         m_Selected   = 0;
        char                        m_Query[128] = {};
        std::vector<PaletteCommand> m_Commands;
    };
} // namespace Desert::Editor
