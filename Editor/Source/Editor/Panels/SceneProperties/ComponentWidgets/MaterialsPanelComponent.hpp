#pragma once

#include "IComponentWidget.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"

#include <Engine/Graphic/Materials/Models/Mesh/PBR/MaterialPBRUB.hpp>

namespace Desert::Editor
{
    class MaterialComponentWidget
    {
    public:
        MaterialComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager );

        void Render( ECS::Entity& entity );

    private:
        void RenderMaterialProperties( ECS::StaticMeshComponent& materialComp );
        // void RenderReflectedProperties( const Graphic::Models::PBR::PBRMaterialPropertiesUBData& reflectedObject
        // );

    private:
        std::unique_ptr<Editor::UI::UIHelper> m_UIHelper;
        std::weak_ptr<Assets::AssetManager>   m_AssetManager;
    };

} // namespace Desert::Editor