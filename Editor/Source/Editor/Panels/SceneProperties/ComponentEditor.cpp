#include "ComponentEditor.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Widgets/Controls/Controls.hpp>

#include "ComponentWidgets/TransformComponentWidget.hpp"
#include "ComponentWidgets/StaticMeshComponent.hpp"

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    ComponentEditor::ComponentEditor( const std::shared_ptr<Assets::AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
        RegisterDefaultComponents();
    }

    void ComponentEditor::RegisterComponent( const std::string&                                 name,
                                             std::function<std::unique_ptr<IComponentWidget>()> factory )
    {
        m_AvailableComponents.emplace_back( name, factory );
    }

    void ComponentEditor::RegisterDefaultComponents()
    {
        RegisterComponent( "Transform", []() { return std::make_unique<TransformComponentWidget>(); } );
        RegisterComponent( "3D Model",
                           [this]() { return std::make_unique<StaticMeshComponentWidget>( m_AssetManager ); } );
    }

    void ComponentEditor::Render( ECS::Entity& entity )
    {
        std::vector<std::unique_ptr<IComponentWidget>> activeWidgets;

        for ( const auto& [name, factory] : m_AvailableComponents )
        {
            auto widget = factory();
            if ( widget->EntityHasComponent( entity ) )
            {
                activeWidgets.push_back( std::move( widget ) );
            }
        }

        for ( auto& widget : activeWidgets )
        {
            RenderComponentHeader( *widget, entity );
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

            for ( const auto& [name, factory] : m_AvailableComponents )
            {
                if ( m_ComponentFilter.PassFilter( name.c_str() ) )
                {
                    auto testWidget = factory();
                    bool alreadyHas = testWidget->EntityHasComponent( entity );

                    if ( !alreadyHas && ImGui::Selectable( name.c_str() ) )
                    {
                        auto widget = factory();
                        widget->AddComponentToEntity( entity );
                    }
                }
            }

            ImGui::EndPopup();
        }
    }

    void ComponentEditor::RenderComponentHeader( IComponentWidget& widget, ECS::Entity& entity )
    {
        bool removed = false;

        bool open = ImGui::CollapsingHeader( widget.GetName().c_str(), ImGuiTreeNodeFlags_AllowItemOverlap |
                                                                            ImGuiTreeNodeFlags_DefaultOpen );
        if ( widget.CanRemove() )
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
            widget.RemoveComponentFromEntity( entity );
        }
        else if ( open )
        {
            ImGui::PushID( widget.GetName().c_str() );
            widget.Render( entity );
            ImGui::PopID();
        }
    }

} // namespace Desert::Editor