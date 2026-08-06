#pragma once

#include "../IPanel.hpp"

#include <Editor/Core/LogView.hpp> // LogRun — the cached row type below

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace Desert::Editor
{
    class LogsPanel final : public IPanel
    {
    public:
        LogsPanel();
        void OnUIRender() override;

    private:
        struct LogEntry
        {
            std::string Text;
            int         Level; // 0=trace/debug/info, 1=warning, 2=error/critical
        };

        void Refresh();
        void DrawToolbar();

    private:
        std::vector<LogEntry>                    m_Entries;
        std::filesystem::file_time_type          m_LastWriteTime{};
        bool                                     m_ScrollToBottom = true;
        bool                                     m_ShowInfo       = true;
        bool                                     m_ShowWarnings   = true;
        bool                                     m_ShowErrors     = true;
        bool                                     m_Collapse       = true; // fold consecutive duplicates
        char                                     m_Filter[128]    = {};   // case-insensitive search

        // CACHED VIEW. The row list only changes when the log or the filters change, but it used to be
        // rebuilt from scratch every frame — two std::string copies per entry, so ~10k allocations per
        // frame on a 5k-line log. The ImGuiListClipper below made the DRAWING O(visible), but all of that
        // work happened before the clipper ever ran, and it measured 9.7 ms of a 16 ms frame — more than
        // the entire 3D scene. Rebuilt only when one of the inputs below actually differs.
        std::vector<LogRun> m_Rows;
        size_t              m_RowsBuiltFromCount = static_cast<size_t>( -1 );
        bool                m_RowsInfo           = true;
        bool                m_RowsWarnings       = true;
        bool                m_RowsErrors         = true;
        bool                m_RowsCollapse       = true;
        char                m_RowsFilter[128]    = {};
    };
} // namespace Desert::Editor
