#include "ComponentEditor.hpp"

#include <cstring>
#include <string>

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

    namespace
    {
        // Bucket a component (by its display name) into an Add-Component menu category. Keyword-based so it is
        // robust to the exact registered names. Order here = menu order.
        struct AddCategory
        {
            const char* Icon;
            const char* Name;
        };
        constexpr AddCategory kAddCategories[] = {
             { ICON_MDI_SHAPE, "Rendering" },   { ICON_MDI_LIGHTBULB, "Lighting" },
             { ICON_MDI_VIEW_DASHBOARD, "UI" }, { ICON_MDI_ATOM, "Physics" },
             { ICON_MDI_RUN, "Animation" },     { ICON_MDI_VOLUME_HIGH, "Audio" },
             { ICON_MDI_VIDEO, "Camera" },      { ICON_MDI_DOTS_HORIZONTAL, "Other" },
        };

        const char* CategoryOf( const std::string& name )
        {
            const auto has = [&]( const char* s ) { return name.find( s ) != std::string::npos; };
            if ( has( "UI " ) || has( "Panel" ) || has( "Button" ) || has( "Layout" ) || has( "Canvas" ) ||
                 has( "Anchor" ) )
                return "UI";
            if ( has( "Light" ) )
                return "Lighting";
            if ( has( "Mesh" ) || has( "Text" ) || has( "Particle" ) || has( "Skybox" ) || has( "Terrain" ) ||
                 has( "Sprite" ) || has( "Decal" ) )
                return "Rendering";
            if ( has( "Collider" ) || has( "Rigid" ) || has( "Character" ) || has( "Physics" ) )
                return "Physics";
            if ( has( "Anim" ) || has( "Skeleton" ) )
                return "Animation";
            if ( has( "Audio" ) || has( "Sound" ) )
                return "Audio";
            if ( has( "Camera" ) )
                return "Camera";
            return "Other";
        }
    } // namespace

    void ComponentEditor::RenderAddComponentPopup( ECS::Entity& entity )
    {
        if ( ImGui::BeginPopup( "addComponent" ) )
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( ICON_MDI_MAGNIFY );
            ImGui::SameLine();

            float filterSize = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().IndentSpacing;
            filterSize       = filterSize < 220 ? 220 : filterSize;
            m_ComponentFilter.Draw( "##ComponentFilter", filterSize );
            ImGui::Separator();

            const bool filtering = m_ComponentFilter.IsActive();

            const auto addEntry = [&]( const ComponentEditorEntry& entry )
            {
                if ( ImGui::Selectable( entry.Name.c_str() ) && entry.Add )
                    Commands::MutateEntityUndoable( entity.GetComponent<ECS::UUIDComponent>().UUID,
                                                    [&] { entry.Add( entity ); } );
            };

            if ( filtering )
            {
                // Flat filtered list while searching — categories only get in the way of a text query.
                for ( const auto& entry : ComponentWidgetRegistry::Get().Entries() )
                {
                    if ( entry.Has && entry.Has( entity ) )
                        continue;
                    if ( m_ComponentFilter.PassFilter( entry.Name.c_str() ) )
                        addEntry( entry );
                }
            }
            else
            {
                // Grouped into collapsible category submenus (Rendering / Lighting / UI / Physics / ...).
                for ( const auto& cat : kAddCategories )
                {
                    // Does this category have any addable entries? (skip empty submenus)
                    bool any = false;
                    for ( const auto& entry : ComponentWidgetRegistry::Get().Entries() )
                        if ( ( !entry.Has || !entry.Has( entity ) ) &&
                             std::strcmp( CategoryOf( entry.Name ), cat.Name ) == 0 )
                        {
                            any = true;
                            break;
                        }
                    if ( !any )
                        continue;

                    if ( ImGui::BeginMenu( ( std::string( cat.Icon ) + "  " + cat.Name ).c_str() ) )
                    {
                        for ( const auto& entry : ComponentWidgetRegistry::Get().Entries() )
                        {
                            if ( entry.Has && entry.Has( entity ) )
                                continue;
                            if ( std::strcmp( CategoryOf( entry.Name ), cat.Name ) == 0 )
                                addEntry( entry );
                        }
                        ImGui::EndMenu();
                    }
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
