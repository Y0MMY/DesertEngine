#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Desert::Editor
{
    // Named editor layouts ("workspaces"). A layout is just the ImGui docking/window state captured with
    // SaveIniSettingsToMemory() and stored as <config>/Layouts/<name>.ini; applying one feeds it back
    // through LoadIniSettingsFromMemory(). User-level (shared across projects), like editor.json.
    class LayoutManager
    {
    public:
        static std::filesystem::path LayoutsDir();

        // Names of saved layouts (the .ini file stems), sorted.
        static std::vector<std::string> List();

        // Capture the current ImGui layout under @p name (overwrites). Returns false on an empty/invalid name.
        static bool Save( const std::string& name );

        // Apply a saved layout. Returns false if it does not exist.
        static bool Load( const std::string& name );

        static void Delete( const std::string& name );

        // Filename-safe form of a user-typed name (letters/digits/space/_/-); "" if nothing usable.
        static std::string Sanitize( const std::string& name );
    };
} // namespace Desert::Editor
