#pragma once

#include <Common/Core/UUID.hpp>

#include <optional>
#include <vector>

namespace Desert::Runtime
{
    // Engine-side "which entities should be visually highlighted/outlined" hint. The ENGINE owns and reads
    // this (e.g. MeshECSSystem flags meshes for the JumpFlood selection outline); higher layers — the
    // editor's SelectionManager — PUSH into it. This keeps the engine free of any editor dependency
    // (modular isolation): the dependency points editor -> engine, never the reverse.
    //
    // Holds the full multi-selection; Get() returns the PRIMARY (most recently selected) entity for
    // consumers that only care about one.
    class SelectionContext final
    {
    public:
        static void Set( const Common::UUID& uuid )
        {
            s_Selected.assign( 1, uuid );
        }

        static void SetAll( std::vector<Common::UUID> uuids )
        {
            s_Selected = std::move( uuids );
        }

        // The primary selection (last selected), or nullopt when nothing is selected.
        static std::optional<Common::UUID> Get()
        {
            if ( s_Selected.empty() )
                return std::nullopt;
            return s_Selected.back();
        }

        static const std::vector<Common::UUID>& GetAll()
        {
            return s_Selected;
        }

        static bool Contains( const Common::UUID& uuid )
        {
            for ( const auto& u : s_Selected )
                if ( u == uuid )
                    return true;
            return false;
        }

        static void Clear()
        {
            s_Selected.clear();
        }

    private:
        static inline std::vector<Common::UUID> s_Selected;
    };
} // namespace Desert::Runtime
