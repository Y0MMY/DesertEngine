#include "Internal/ScriptRuntime.hpp"

namespace Desert::Scripting
{
    // Material API — the unified material protocol (params by shader-schema name). Extends the
    // base Entity usertype from its own module.
    void RegisterMaterialBindings( ScriptEngine::Impl& impl )
    {
        sol::table entity = impl.Lua["Entity"];
        entity["setMaterialParam"]    = &ScriptEntity::SetMaterialParam;
        entity["getMaterialParam"]    = &ScriptEntity::GetMaterialParam;
        entity["clearMaterialParams"] = &ScriptEntity::ClearMaterialParams;
        entity["setShader"]           = &ScriptEntity::SetShader;
        entity["getShader"]           = &ScriptEntity::GetShader;
    }
} // namespace Desert::Scripting
