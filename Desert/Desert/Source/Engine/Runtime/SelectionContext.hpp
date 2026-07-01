#pragma once

#include <Common/Core/UUID.hpp>

#include <optional>

namespace Desert::Runtime
{
    // Engine-side "which entity should be visually highlighted/outlined" hint. The ENGINE owns and reads this
    // (e.g. MeshECSSystem flags the mesh for the JumpFlood selection outline); higher layers — the editor's
    // SelectionManager — PUSH into it. This keeps the engine free of any editor dependency (modular isolation):
    // the dependency points editor -> engine, never the reverse.
    class SelectionContext final
    {
    public:
        static void                               Set( const Common::UUID& uuid ) { s_Highlighted = uuid; }
        static const std::optional<Common::UUID>& Get() { return s_Highlighted; }
        static void                               Clear() { s_Highlighted.reset(); }

    private:
        static inline std::optional<Common::UUID> s_Highlighted;
    };
} // namespace Desert::Runtime
