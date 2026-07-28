#pragma once

#include <string>
#include <utility>
#include <vector>

namespace Desert::Editor
{
    // Pure helpers behind the Logs panel's search + duplicate-collapse. Kept std-only so the filtering
    // and collapsing logic is unit-tested directly, away from ImGui and the log file.

    // Case-insensitive substring test. An empty query matches everything.
    bool LogMatches( const std::string& line, const std::string& query );

    // A run of consecutive identical log lines collapsed into one entry.
    struct LogRun
    {
        std::string Text;
        int         Level = 0;
        int         Count = 1;
    };

    // Collapse runs of CONSECUTIVE identical (text, level) lines into LogRuns with a count. Order is
    // preserved; non-adjacent duplicates are NOT merged (they are separate events in the timeline).
    std::vector<LogRun> CollapseConsecutive( const std::vector<std::pair<std::string, int>>& lines );
} // namespace Desert::Editor
