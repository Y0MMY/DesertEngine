#pragma once

// Native file dialogs. There were none anywhere in the launcher: "Open existing" was a bare text
// field into which the user was expected to TYPE a .deproj path, and New Project's Location was
// the same. That is the loudest gap between this window and a product, and it costs one .mm on
// macOS (NSOpenPanel) and one .cpp on Windows (IFileDialog) — no dependency either side.
//
// Both functions block until the user answers, which is what a modal panel is. The hub's frame
// loop simply stops for that time; there is nothing to animate behind a system dialog.

#include <string>

namespace Hub::FileDialog
{
    // Picks one existing file. `filterName`/`filterExtension` describe the one type offered
    // ("Desert project", "deproj"). Returns "" when the user cancelled — a cancel is not an error
    // and must not produce a status line.
    [[nodiscard]] std::string OpenFile( const std::string& title, const std::string& filterName,
                                        const std::string& filterExtension );

    // Picks one existing directory. `startIn` may be "" — the panel then opens wherever the system
    // last left it. Returns "" when the user cancelled.
    [[nodiscard]] std::string PickDirectory( const std::string& title, const std::string& startIn );
} // namespace Hub::FileDialog
