#pragma once

#include <Engine/Desert.hpp>
#include <Engine/Runtime/SelectionContext.hpp>

namespace Desert::Editor::Core
{
    // Editor-side selection state. It also PUSHES the current selection into the engine-side
    // Runtime::SelectionContext (the "highlight/outline this entity" hint the engine reads), so the engine
    // never depends on the editor for it — the dependency stays editor -> engine.
    class SelectionManager final // maybe singleton?
    {
    public:
        static void SetSelected( const Common::UUID& uuid )
        {
            m_SelctedEntity = std::make_optional( uuid );
            Runtime::SelectionContext::Set( uuid );
        }
        static const auto& GetSelected()
        {
            return m_SelctedEntity;
        }
        /* using CallbackType = std::function<void( ECS::Entity* )>;
         static void Subscribe( CallbackType callback );
         static void UnsubscribeAll();*/

        static void ClearSelection()
        {
            m_SelctedEntity.reset();
            Runtime::SelectionContext::Clear();
        }

    private:
        static inline std::optional<Common::UUID> m_SelctedEntity;
    };
} // namespace Desert::Editor::Core