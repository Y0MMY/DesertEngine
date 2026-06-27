#include "ComponentWidgetRegistry.hpp"

namespace Desert::Editor
{
    ComponentWidgetRegistry& ComponentWidgetRegistry::Get()
    {
        static ComponentWidgetRegistry instance;
        return instance;
    }

    int ComponentWidgetRegistry::Register( ComponentEditorEntry entry )
    {
        m_Entries.push_back( std::move( entry ) );
        return static_cast<int>( m_Entries.size() );
    }
} // namespace Desert::Editor
