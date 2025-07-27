#pragma once

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"

namespace Desert::Editor
{
    class MaterialsPanel
    {
    public:
        MaterialsPanel( const std::weak_ptr<Runtime::ResourceResolver>& resourceResolver )
             : m_ResourceResolver( resourceResolver ), m_UIHelper( std::make_unique<Editor::UI::UIHelper>() )
        {
            m_UIHelper->Init();
        }

        void DrawMaterialEditor( const std::shared_ptr<Graphic::MaterialPBR>& material );
        void DrawMaterialEntity( const ECS::Entity& entity );
        void DrawMaterialInfo( const std::shared_ptr<Graphic::MaterialPBR>& material );

    private:
        void DrawTextureSlot( const char* label, Assets::TextureAsset::Type type,
                              const std::shared_ptr<Graphic::MaterialPBR>& material );
        void DrawMaterialProperties( const std::shared_ptr<Graphic::MaterialPBR>& material );

    private:
        const std::weak_ptr<Runtime::ResourceResolver> m_ResourceResolver;
        std::unique_ptr<Editor::UI::UIHelper>          m_UIHelper;
    };

} // namespace Desert::Editor