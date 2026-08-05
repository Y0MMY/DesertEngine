#pragma once

#include <Editor/Core/Selection/SelectionManager.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>

#include <memory>

namespace Desert::Editor
{
    // Shared relevance test for CONTEXTUAL panels (IPanel::IsContextual): does the current selection in
    // `scene` carry component T? Keeps every such panel's IsRelevant() a one-liner instead of repeating
    // the selection -> entity -> component dance.
    template <typename T>
    bool SelectionHas( const std::shared_ptr<::Desert::Core::Scene>& scene )
    {
        if ( !scene )
            return false;
        const auto& sel = Core::SelectionManager::GetSelected();
        if ( !sel )
            return false;
        auto ref = scene->FindEntityByID( *sel );
        return ref && ref->get().HasComponent<T>();
    }
} // namespace Desert::Editor
