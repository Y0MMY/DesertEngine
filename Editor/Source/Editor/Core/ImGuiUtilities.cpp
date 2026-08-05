#include "ImGuiUtilities.hpp"

#include <ImGui/imgui.h>
#include <ImGui/imgui_internal.h>
#include <algorithm>
#include <format>

namespace Desert::Editor::Utils
{
    static int s_UIContextID = 0;

    bool ImGuiUtilities::SectionHeader( const char* label, bool defaultOpen )
    {
        // ONE look for every section in the editor. Modelled on UE's Details panel: a flat, full-width
        // grey bar with a disclosure triangle — not a tinted rounded pill. A section header is furniture;
        // when it is coloured and rounded it competes with the actual controls for attention, and when
        // every panel invents its own the whole editor reads as unfinished.
        ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.16f, 0.17f, 0.19f, 1.00f ) );
        ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( 0.21f, 0.22f, 0.25f, 1.00f ) );
        ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( 0.24f, 0.26f, 0.29f, 1.00f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 6.0f, 5.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 0.0f );

        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                                         ImGuiTreeNodeFlags_AllowItemOverlap |
                                         ( defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0 );

        // NOTE: keep the blank line above — it stops clang-format's consecutive-declaration alignment from
        // padding `open` across the multi-line `flags` init (v18 and v22 disagree on that, tripping the gate).
        const bool open = ImGui::CollapsingHeader( label, flags );

        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor( 3 );
        return open;
    }

    namespace
    {
        // Alternating row index, continuous down a component's rows and reset when one starts.
        int s_PropertyRowIndex = 0;
    } // namespace

    void ImGuiUtilities::ResetPropertyRows()
    {
        s_PropertyRowIndex = 0;
    }

    bool ImGuiUtilities::PropertyRowBackground()
    {
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const ImVec2 rowMax( rowMin.x + ImGui::GetContentRegionAvail().x, rowMin.y + ImGui::GetFrameHeight() );
        const bool   hovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) &&
                             ImGui::IsMouseHoveringRect( rowMin, rowMax, /*clip*/ true );

        // Edge to edge, past the window padding: a stripe that stops short of the border reads as a box
        // instead of a row.
        const ImVec2 bandMin( rowMin.x - ImGui::GetStyle().WindowPadding.x, rowMin.y );
        const ImVec2 bandMax( rowMax.x + ImGui::GetStyle().WindowPadding.x, rowMax.y );
        ImDrawList*  dl = ImGui::GetWindowDrawList();
        if ( hovered )
            dl->AddRectFilled( bandMin, bandMax, ImGui::GetColorU32( ImGuiCol_Header, 0.30f ) );
        else if ( ( s_PropertyRowIndex & 1 ) != 0 )
            dl->AddRectFilled( bandMin, bandMax, IM_COL32( 255, 255, 255, 8 ) );

        ++s_PropertyRowIndex;
        return hovered;
    }

    void ImGuiUtilities::BeginPropertyRow( const char* label, const char* tooltip )
    {
        PropertyRowBackground();

        ImGui::Columns( 2 );
        // The label column follows the panel instead of a fixed width: docked narrow, a fixed column eats
        // the editor and every value box collapses; docked wide, the labels strand far from their values.
        const float labelW = std::clamp( ImGui::GetWindowWidth() * 0.42f, 110.0f, 230.0f );
        ImGui::SetColumnWidth( 0, labelW );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( label );
        if ( tooltip )
            Tooltip( tooltip );

        ImGui::NextColumn();
        ImGui::PushItemWidth( -1.0f );
    }

    void ImGuiUtilities::EndPropertyRow()
    {
        ImGui::PopItemWidth();
        ImGui::NextColumn();
        ImGui::Columns( 1 );
    }

    bool ImGuiUtilities::AccentButton( const char* label, float height )
    {
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.44f, 0.72f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.26f, 0.52f, 0.82f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.16f, 0.38f, 0.64f, 1.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 5.0f );
        const bool clicked = ImGui::Button( label, ImVec2( ImGui::GetContentRegionAvail().x, height ) );
        ImGui::PopStyleVar();
        ImGui::PopStyleColor( 3 );
        return clicked;
    }

    void ImGuiUtilities::PushID()
    {
        ImGui::PushID( s_UIContextID++ );
    }

    void ImGuiUtilities::PopID()
    {
        ImGui::PopID();
        s_UIContextID--;
    }

    bool ImGuiUtilities::InputText( std::string& currentText, const char* ID )
    {
        ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
        ImGuiUtilities::DrawItemActivityOutline( 2.0f, false, ImColor( 80, 80, 80 ) );

        ImGui::PushID( ID );

        bool edited = ImGui::InputText(
             ID, currentText.data(), currentText.size() + 1, ImGuiInputTextFlags_CallbackResize,
             []( ImGuiInputTextCallbackData* data ) -> int
             {
                 if ( data->EventFlag == ImGuiInputTextFlags_CallbackResize )
                 {
                     std::string* str = static_cast<std::string*>( data->UserData );
                     str->resize( data->BufTextLen );
                     data->Buf = str->data();
                 }
                 return 0;
             },
             &currentText );

        ImGui::PopID();
        ImGui::PopStyleColor();

        return edited;
    }

    void ImGuiUtilities::DrawItemActivityOutline( float rounding, bool drawWhenInactive, ImColor colourWhenActive )
    {
        auto* drawList = ImGui::GetWindowDrawList();

        ImRect expandedRect = ImRect( ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
        expandedRect.Min.x -= 1.0f;
        expandedRect.Min.y -= 1.0f;
        expandedRect.Max.x += 1.0f;
        expandedRect.Max.y += 1.0f;

        const ImRect rect = expandedRect;
        if ( ImGui::IsItemHovered() && !ImGui::IsItemActive() )
        {
            drawList->AddRect( rect.Min, rect.Max, ImColor( 60, 60, 60 ), rounding, 0, 1.5f );
        }
        if ( ImGui::IsItemActive() )
        {
            drawList->AddRect( rect.Min, rect.Max, colourWhenActive, rounding, 0, 1.0f );
        }
        else if ( !ImGui::IsItemHovered() && drawWhenInactive )
        {
            drawList->AddRect( rect.Min, rect.Max, ImColor( 50, 50, 50 ), rounding, 0, 1.0f );
        }
    };

    void ImGuiUtilities::Tooltip( const char* text )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 5, 5 ) );

        if ( ImGui::IsItemHovered() )
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted( text );
            ImGui::EndTooltip();
        }

        ImGui::PopStyleVar();
    }

    bool ImGuiUtilities::Property( const char* name, uint32_t& value, ImGuiUtilities::PropertyFlag flags )
    {
        bool updated = false;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( name );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        if ( (int)flags & (int)PropertyFlag::ReadOnly )
        {
            ImGui::Text( "%d", value );
        }
        else
        {
            std::string id = "##" + std::string( name );
            updated        = ImGui::DragScalar( id.c_str(), ImGuiDataType_U32, &value );
        }
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        return updated;
    }

    bool ImGuiUtilities::Property( const char* name, bool& value, PropertyFlag flags )
    {
        bool updated = false;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( name );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        if ( (int)flags & (int)PropertyFlag::ReadOnly )
        {
            ImGui::TextUnformatted( value ? "True" : "False" );
        }
        else
        {
            std::string id = "##" + std::string( name );
            if ( ImGui::Checkbox( id.c_str(), &value ) )
                updated = true;
        }

        ImGui::PopItemWidth();
        ImGui::NextColumn();

        return updated;
    }

    bool ImGuiUtilities::Property( const char* name, std::string& value, PropertyFlag flags )
    {
        bool updated = false;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( name );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );
        ImGui::AlignTextToFramePadding();

        if ( (int)flags & (int)PropertyFlag::ReadOnly )
        {
            ImGui::TextUnformatted( value.c_str() );
        }
        else
        {
            if ( ImGuiUtilities::InputText( value, name ) )
            {
                updated = true;
            }
        }
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        return updated;
    }

    bool ImGuiUtilities::Property( const char* name, float& value, float min /*= -1.0f*/, float max /*= 1.0f*/,
                                   float delta /*= 1.0f*/, PropertyFlag flags /*= PropertyFlag::None */ )
    {
        bool updated = false;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( name );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        if ( (int)flags & (int)PropertyFlag::ReadOnly )
        {
            ImGui::Text( "%.2f", value );
        }
        else if ( (int)flags & (int)PropertyFlag::DragValue )
        {
            if ( ImGui::DragFloat( std::format( "##{}", name ).c_str(), &value, delta, min, max ) )
                updated = true;
        }
        else if ( (int)flags & (int)PropertyFlag::SliderValue )
        {
            if ( ImGui::SliderFloat( std::format( "##{}", name ).c_str(), &value, min, max ) )
                updated = true;
        }
        else
        {
            if ( ImGui::InputFloat( std::format( "##{}", name ).c_str(), &value, delta ) )
                updated = true;
        }
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        return updated;
    }

    bool ImGuiUtilities::Property( const char* name, glm::vec3& value, bool exposeW, PropertyFlag flags )
    {
        return Property( name, value, -1.0f, 1.0f, exposeW, flags );
    }

    bool ImGuiUtilities::Property( const char* name, glm::vec3& value, float min, float max,
                                   bool exposeW /*= false*/, PropertyFlag flags )
    {
        bool updated = false;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( name );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );
        if ( (int)flags & (int)PropertyFlag::ReadOnly )
        {
            ImGui::Text( "%.2f , %.2f, %.2f", value.x, value.y, value.z );
        }
        else
        {

            // std::string id = "##" + name;
            if ( (int)flags & (int)PropertyFlag::ColorProperty )
            {
                if ( ImGui::ColorEdit3( std::format( "##{}", name ).c_str(), &value.x ) )
                    updated = true;
            }
            else if ( ( exposeW ? ImGui::DragFloat3( std::format( "##{}", name ).c_str(), &value.x )
                                : ImGui::DragFloat3( std::format( "##{}", name ).c_str(), &value.x ) ) )
                updated = true;
        }
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        return updated;
    }

    ImRect ImGuiUtilities::RectExpanded( const ImRect& rect, float x, float y )
    {
        ImRect result = rect;
        result.Min.x -= x;
        result.Min.y -= y;
        result.Max.x += x;
        result.Max.y += y;
        return result;
    }

    void ImGuiUtilities::DrawBorder( ImVec2 rectMin, ImVec2 rectMax, const ImVec4& borderColour, float thickness,
                                     float offsetX, float offsetY )
    {
        auto min = rectMin;
        min.x -= thickness;
        min.y -= thickness;
        min.x += offsetX;
        min.y += offsetY;
        auto max = rectMax;
        max.x += thickness;
        max.y += thickness;
        max.x += offsetX;
        max.y += offsetY;

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRect( min, max, ImGui::ColorConvertFloat4ToU32( borderColour ), 0.0f, 0, thickness );
    }

    void ImGuiUtilities::DrawBorder( ImRect rect, float thickness, float rounding, float offsetX, float offsetY )
    {
        auto min = rect.Min;
        min.x -= thickness;
        min.y -= thickness;
        min.x += offsetX;
        min.y += offsetY;
        auto max = rect.Max;
        max.x += thickness;
        max.y += thickness;
        max.x += offsetX;
        max.y += offsetY;

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRect( min, max, ImGui::ColorConvertFloat4ToU32( ImGui::GetStyleColorVec4( ImGuiCol_Border ) ),
                           rounding, 0, thickness );
    }

    ImRect ImGuiUtilities::GetItemRect()
    {
        return ImRect( ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
    }
} // namespace Desert::Editor::Utils