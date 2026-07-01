#pragma once

#include <string>
#include <vector>

namespace Desert::Scripting
{
    // A single editor-exposed script variable. Declared by a script via its global `Properties` table; the
    // editor reads the schema (names/types/defaults), the user can override per entity, and the values are
    // written back into the script's environment before it runs. POD only (NO sol/Lua types) so this can
    // live in ScriptComponent / Components.hpp without leaking the scripting backend.
    enum class PropertyType
    {
        Number = 0,
        Bool   = 1,
        String = 2
    };

    struct ScriptProperty
    {
        std::string  Name;
        PropertyType Type   = PropertyType::Number;
        double       Number = 0.0;
        bool         Bool   = false;
        std::string  Str;
    };
} // namespace Desert::Scripting
