#include "Controls.hpp"

#include <ImGui/imgui_internal.h>

namespace Desert::Editor
{

    namespace ImGui = ::ImGui;

    void Widgets::DrawVec3Control( const std::string& label, glm::vec3& values, float resetValue )
    {
        ImGui::PushID( label.c_str() );

        ImGui::Columns( 2 );
        ImGui::SetColumnWidth( 0, 100.0f );

        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.9f, 0.9f, 0.9f, 0.9f ) );
        ImGui::TextUnformatted( label.c_str() );
        ImGui::PopStyleColor();

        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths( 3, ImGui::CalcItemWidth() );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 4, 2 } );

        float  lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

        {
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.65f, 0.15f, 0.15f, 1.0f } );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.75f, 0.2f, 0.2f, 1.0f } );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4{ 0.55f, 0.1f, 0.1f, 1.0f } );
            if ( ImGui::Button( "X", buttonSize ) )
                values.x = resetValue;
            ImGui::PopStyleColor( 3 );

            ImGui::SameLine();
            ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.13f, 0.13f, 0.14f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.09f, 0.09f, 0.10f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.95f, 0.95f, 0.95f, 1.0f ) );
            ImGui::DragFloat( "##X", &values.x, 0.1f );
            ImGui::PopStyleColor( 4 );
            ImGui::PopItemWidth();
            ImGui::SameLine();
        }

        {
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.15f, 0.55f, 0.15f, 1.0f } );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.65f, 0.2f, 1.0f } );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.45f, 0.1f, 1.0f } );
            if ( ImGui::Button( "Y", buttonSize ) )
                values.y = resetValue;
            ImGui::PopStyleColor( 3 );

            ImGui::SameLine();
            ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.13f, 0.13f, 0.14f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.09f, 0.09f, 0.10f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.95f, 0.95f, 0.95f, 1.0f ) );
            ImGui::DragFloat( "##Y", &values.y, 0.1f );
            ImGui::PopStyleColor( 4 );
            ImGui::PopItemWidth();
            ImGui::SameLine();
        }

        {
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.15f, 0.15f, 0.65f, 1.0f } );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.2f, 0.75f, 1.0f } );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.1f, 0.55f, 1.0f } );
            if ( ImGui::Button( "Z", buttonSize ) )
                values.z = resetValue;
            ImGui::PopStyleColor( 3 );

            ImGui::SameLine();
            ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.13f, 0.13f, 0.14f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.09f, 0.09f, 0.10f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.95f, 0.95f, 0.95f, 1.0f ) );
            ImGui::DragFloat( "##Z", &values.z, 0.1f );
            ImGui::PopStyleColor( 4 );
            ImGui::PopItemWidth();
        }

        ImGui::PopStyleVar();
        ImGui::Columns( 1 );
        ImGui::PopID();
    }

    void Widgets::DrawDirectionWidget( const std::string& label, glm::vec3& direction )
    {
        ImGui::PushID( label.c_str() );

        const float widgetSize    = 120.0f;
        const float markerRadius  = 8.0f;
        const float widgetPadding = 8.0f;

        const float inputHeight = ImGui::GetFrameHeight();
        const float buttonWidth = 44.0f;
        const float spacing     = 28.0f;

        if ( glm::length( glm::vec2( direction.x, direction.y ) ) < 0.001f )
        {
            direction = glm::vec3( 0.0f, 1.0f, -1.0f );
        }
        ImVec2 startCursorPos = ImGui::GetCursorPos();
        ImVec2 startScreenPos = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton( "##DirectionWidget", ImVec2( widgetSize, widgetSize ) );
        ImVec2 widgetMin = ImGui::GetItemRectMin();
        ImVec2 widgetMax = ImGui::GetItemRectMax();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2      center = ImVec2( ( widgetMin.x + widgetMax.x ) * 0.5f, ( widgetMin.y + widgetMax.y ) * 0.5f );
        float       radius = widgetSize * 0.5f - markerRadius;

        drawList->AddCircleFilled( center, radius + markerRadius,
                                   ImGui::GetColorU32( ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) ), 32 );
        drawList->AddCircle( center, radius + markerRadius,
                             ImGui::GetColorU32( ImVec4( 0.2f, 0.2f, 0.25f, 1.0f ) ), 32, 1.0f );

        drawList->AddLine( ImVec2( center.x - radius, center.y ), ImVec2( center.x + radius, center.y ),
                           ImGui::GetColorU32( ImVec4( 0.5f, 0.5f, 0.5f, 0.15f ) ), 0.3f );
        drawList->AddLine( ImVec2( center.x, center.y - radius ), ImVec2( center.x, center.y + radius ),
                           ImGui::GetColorU32( ImVec4( 0.5f, 0.5f, 0.5f, 0.15f ) ), 0.3f );

        if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( 0 ) )
        {
            ImVec2    mousePos = ImGui::GetMousePos();
            glm::vec2 delta    = glm::vec2( mousePos.x - center.x, mousePos.y - center.y );
            float     length   = glm::length( delta );
            if ( length > radius )
                delta = delta / length * radius;

            direction.x = delta.x / radius;
            direction.y = delta.y / radius;
        }

        glm::vec2 dirNormalized = glm::normalize( glm::vec2( direction.x, direction.y ) );
        drawList->AddCircleFilled(
             ImVec2( center.x + dirNormalized.x * radius, center.y + dirNormalized.y * radius ), markerRadius,
             ImGui::GetColorU32( ImVec4( 0.22f, 0.42f, 0.69f, 1.0f ) ), 12 );
        ImGui::SetCursorScreenPos( ImVec2( widgetMax.x + spacing, startScreenPos.y ) );

        float totalInputHeight = inputHeight * 3 + ImGui::GetStyle().ItemSpacing.y * 2;
        float verticalOffset   = ( widgetSize - totalInputHeight ) * 0.5f;

        ImGui::BeginGroup();
        {
            ImGui::SetCursorScreenPos( ImVec2( widgetMax.x + spacing, startScreenPos.y + verticalOffset ) );

            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.65f, 0.15f, 0.15f, 1.0f ) );
            ImGui::Button( "X", ImVec2( buttonWidth, 0 ) );
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x );
            ImGui::DragFloat( "##X", &direction.x, 0.01f, -1.0f, 1.0f, "%.2f" );

            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.15f, 0.55f, 0.15f, 1.0f ) );
            ImGui::Button( "Y", ImVec2( buttonWidth, 0 ) );
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x );
            ImGui::DragFloat( "##Y", &direction.y, 0.01f, -1.0f, 1.0f, "%.2f" );

            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.15f, 0.15f, 0.55f, 1.0f ) );
            ImGui::Button( "Z", ImVec2( buttonWidth, 0 ) );
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x );
            ImGui::DragFloat( "##Z", &direction.z, 0.01f, -1.0f, 1.0f, "%.2f" );
        }
        ImGui::EndGroup();

        ImGui::SetCursorPos( ImVec2( startCursorPos.x, startCursorPos.y + widgetSize ) );

        ImGui::PopID();
    }

   
} // namespace Desert::Editor