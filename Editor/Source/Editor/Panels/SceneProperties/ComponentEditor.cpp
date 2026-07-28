#include "ComponentEditor.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/Commands/SceneCommands.hpp>
#include <Editor/Widgets/Controls/Controls.hpp>

#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    ComponentEditor::ComponentEditor( const std::shared_ptr<Assets::AssetManager>& assetManager,
                                      const Animation::AnimationLibrary*           animationLibrary )
         : m_AssetManager( assetManager ), m_AnimationLibrary( animationLibrary )
    {
    }

    ComponentEditContext ComponentEditor::MakeContext() const
    {
        ComponentEditContext ctx;
        ctx.AssetManager     = m_AssetManager;
        ctx.AnimationLibrary = m_AnimationLibrary;
        ctx.UIHelper         = nullptr;
        return ctx;
    }

    void ComponentEditor::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        const auto ctx = MakeContext();

        for ( const auto& entry : ComponentWidgetRegistry::Get().Entries() )
        {
            if ( entry.Has && entry.Has( entity ) )
            {
                RenderComponentHeader( entry, entity, scene, ctx );
            }
        }

        if ( ImGui::Button( ICON_MDI_PLUS_BOX_OUTLINE " Add Component",
                            ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
        {
            ImGui::OpenPopup( "addComponent" );
        }

        RenderAddComponentPopup( entity );
    }

    void ComponentEditor::RenderAddComponentPopup( ECS::Entity& entity )
    {
        if ( ImGui::BeginPopup( "addComponent" ) )
        {
            ImGui::Separator();

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( ICON_MDI_MAGNIFY );
            ImGui::SameLine();

            float filterSize = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().IndentSpacing;
            filterSize       = filterSize < 200 ? 200 : filterSize;
            m_ComponentFilter.Draw( "##ComponentFilter", filterSize );

            for ( const auto& entry : ComponentWidgetRegistry::Get().Entries() )
            {
                if ( entry.Has && entry.Has( entity ) )
                    continue;
                if ( !m_ComponentFilter.PassFilter( entry.Name.c_str() ) )
                    continue;

                if ( ImGui::Selectable( entry.Name.c_str() ) && entry.Add )
                {
                    // Undoable: Ctrl+Z removes the just-added component again.
                    Commands::MutateEntityUndoable( entity.GetComponent<ECS::UUIDComponent>().UUID,
                                                    [&] { entry.Add( entity ); } );
                }
            }

            ImGui::EndPopup();
        }
    }

    void ComponentEditor::RenderComponentHeader( const ComponentEditorEntry& entry, ECS::Entity& entity,
                                                 ::Desert::Core::Scene* scene, const ComponentEditContext& ctx )
    {
        bool removed = false;

        bool open = ImGui::CollapsingHeader( entry.Name.c_str(), ImGuiTreeNodeFlags_AllowItemOverlap |
                                                                      ImGuiTreeNodeFlags_DefaultOpen );
        if ( entry.CanRemove )
        {
            ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() -
                             ImGui::GetStyle().ItemSpacing.x );
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.7f, 0.7f, 0.7f, 0.0f ) );

            if ( ImGui::Button( ICON_MDI_TUNE "##RemoveButton" ) )
            {
                ImGui::OpenPopup( "RemoveComponentPopup" );
            }

            ImGui::PopStyleColor();

            if ( ImGui::BeginPopup( "RemoveComponentPopup" ) )
            {
                if ( ImGui::Selectable( "Remove" ) )
                {
                    removed = true;
                }
                ImGui::EndPopup();
            }
        }

        if ( removed )
        {
            if ( entry.Remove )
            {
                // Undoable: Ctrl+Z brings the component (with its data) back.
                Commands::MutateEntityUndoable( entity.GetComponent<ECS::UUIDComponent>().UUID,
                                                [&] { entry.Remove( entity ); } );
            }
        }
        else if ( open )
        {
            // Indent every component body uniformly so nested widgets read as children of the header
            // (consistent tabulation across Transform / lights / mesh / skybox / reflected components).
            ImGui::PushID( entry.Name.c_str() );
            ImGui::Indent();
            if ( entry.Draw )
                entry.Draw( entity, scene, ctx );
            ImGui::Unindent();
            ImGui::PopID();
        }
    }

} // namespace Desert::Editor
