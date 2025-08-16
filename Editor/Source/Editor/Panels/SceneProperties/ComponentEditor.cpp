#include "ComponentEditor.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Widgets/Controls/Controls.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    ComponentEditor::ComponentEditor( const std::shared_ptr<Assets::AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
    }

    template <typename T>
    void RenderComponent( const std::string& name, std::function<void()> widgetFunc )
    {
        ImGui::PushID( typeid( T ).hash_code() );

        bool open    = ImGui::CollapsingHeader( name.c_str(), ImGuiTreeNodeFlags_AllowItemOverlap |
                                                                   ImGuiTreeNodeFlags_DefaultOpen );
        bool removed = false;

        ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() -
                         ImGui::GetStyle().ItemSpacing.x );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.7f, 0.7f, 0.7f, 0.0f ) );

        static std::string removeName = "Remove Component";
        if ( ImGui::Button( ICON_MDI_TUNE "##RemoveButton" ) )
        {
            ImGui::OpenPopup( removeName.c_str() );
        }

        ImGui::PopStyleColor();

        if ( ImGui::BeginPopup( removeName.c_str(), 3 ) )
        {
            if ( ImGui::Selectable( "Remove" ) )
            {
                removed = true;
            }
            ImGui::EndPopup();
        }

        if ( !removed && open )
        {
            ImGui::PushID( "Widget" );
            widgetFunc();
            ImGui::PopID();
        }

        ImGui::PopID();
    }

    void ComponentEditor::RenderTransformComponent( ECS::Entity& entity )
    {
        std::string header     = "Transform";
        auto        widgetFunc = [&]()
        {
            if ( !entity.HasComponent<ECS::DirectionLightComponent>() )
            {
                ImGui::Dummy( ImVec2( 0, 4 ) );
                auto& transform = entity.GetComponent<ECS::TransformComponent>();

                Widgets::DrawVec3Control( "Position", transform.Translation );
                ImGui::Dummy( ImVec2( 0, 6 ) );
                Widgets::DrawVec3Control( "Rotation", transform.Rotation );
                ImGui::Dummy( ImVec2( 0, 6 ) );
                Widgets::DrawVec3Control( "Scale", transform.Scale, 1.0f );
            }

            else
            {
                header = "Direction";

                ImGui::Dummy( ImVec2( 0, 4 ) );
                auto& transform = entity.GetComponent<ECS::TransformComponent>();

                Widgets::DrawDirectionWidget( "Direction", transform.Translation );
            }
        };

        RenderComponent<ECS::TransformComponent>( header, widgetFunc );
    }

    void ComponentEditor::RenderStaticMeshComponent( ECS::Entity& entity )
    {
        std::string header     = "3D Model";
        auto        widgetFunc = [&]()
        {
            if ( !entity.HasComponent<ECS::StaticMeshComponent>() )
            {
                return;
            }

            const auto assetManager = m_AssetManager.lock();
            auto       meshAssets   = assetManager->FindAllByType<Assets::MeshAsset>();

            if ( !meshAssets.empty() )
            {
               
            }
        };

        RenderComponent<ECS::StaticMeshComponent>( header, widgetFunc );
    }

    void ComponentEditor::Render( ECS::Entity& entity )
    {
        static bool addComponentPopupOpen = false;
        RenderTransformComponent( entity );
        RenderStaticMeshComponent( entity );

        if ( ImGui::Button( ICON_MDI_PLUS_BOX_OUTLINE " Add Component",
                            ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
        {
            ImGui::OpenPopup( "addComponent" );
        }

        if ( ImGui::BeginPopup( "addComponent" ) )
        {
            addComponentPopupOpen = true;
            ImGui::Separator();

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( ICON_MDI_MAGNIFY );
            ImGui::SameLine();

            float filterSize = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().IndentSpacing;
            filterSize       = filterSize < 200 ? 200 : filterSize;
            m_ComponentFilter.Draw( "##ComponentFilter", filterSize );

            if ( !entity.HasComponent<ECS::SkyboxComponent>() && m_ComponentFilter.PassFilter( "Skybox" ) )
            {
                if ( ImGui::Selectable( "Skybox" ) )
                {
                }
            }

            ImGui::EndPopup();
        }
        else
        {
            addComponentPopupOpen = false;
        }
    }

} // namespace Desert::Editor