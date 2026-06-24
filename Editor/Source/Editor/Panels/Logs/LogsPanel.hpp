#pragma once

#include "../IPanel.hpp"

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
    };
} // namespace Desert::Editor
