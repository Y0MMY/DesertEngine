#pragma once

#include <string_view>

namespace Desert::Editor
{
    // Case-insensitive subsequence fuzzy match, à la a command palette / "Go to anything". Returns
    // true and writes a score to @p outScore (higher = better) when every character of @p query
    // appears in order somewhere in @p text; returns false otherwise. An empty query matches
    // everything with score 0.
    //
    // Scoring rewards prefix matches, consecutive runs, and matches at word starts (after a separator
    // or a camelCase boundary), and mildly prefers shorter text and earlier first matches — so "log"
    // ranks "Logs" above "Backlog". Deterministic and side-effect free, hence unit-tested directly.
    bool FuzzyMatch( std::string_view query, std::string_view text, int& outScore );
} // namespace Desert::Editor
