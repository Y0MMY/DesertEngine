#pragma once

#include <Engine/Desert.hpp>

namespace Desert::Editor::Core
{
    class SelectionManager final // maybe singleton?
    {
    public:
        static void SetSelected( Common::Memory::not_null<ECS::Entity>&& entity )
        {
            m_SelctedEntity = std::make_optional( std::move( entity ) );
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
        }

    private:
        static inline std::optional<Common::Memory::not_null<ECS::Entity>> m_SelctedEntity;
    };
} // namespace Desert::Editor::Core