#include "ImGuiUtilities.hpp"

#include <ImGui/imgui.h>
#include <ImGui/imgui_internal.h>
#include <format>

namespace Desert::Editor::Utils
{
    static int s_UIContextID = 0;

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
        static bool keyboardActive    = false;
        static bool updatedExternally = false;

        ImGui::PushStyleColor( ImGuiStyleVar_FrameBorderSize, 0.0f );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );

        ImGuiUtilities::DrawItemActivityOutline( 2.0f, false, ImColor( 80, 80, 80 ) );
        ImGui::PushID( ID );

        currentText.reserve( 256 );

        bool edited = ImGui::InputText(
             ID, &currentText[0], currentText.capacity(), ImGuiInputTextFlags_CallbackResize,
             []( ImGuiInputTextCallbackData* data ) -> int
             {
                 if ( data->EventFlag == ImGuiInputTextFlags_CallbackResize )
                 {
                     std::string* str = (std::string*)data->UserData;
                     str->resize( data->BufTextLen );
                     data->Buf = &( *str )[0];
                 }
                 return 0;
             },
             &currentText );

        ImGui::PopID();

        if ( ImGui::IsItemActivated() && !keyboardActive )
        {
            keyboardActive = true;
        }

        ImGui::PopStyleColor( 2 );
        return edited || updatedExternally;
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
                if ( ImGui::ColorEdit4( std::format( "##{}", name ).c_str(), &value.x ) )
                    updated = true;
            }
            else if ( ( exposeW ? ImGui::DragFloat4( std::format( "##{}", name ).c_str(), &value.x )
                                : ImGui::DragFloat4( std::format( "##{}", name ).c_str(), &value.x ) ) )
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