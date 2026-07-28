#include "CommandPalette.hpp"

#include "FuzzyMatch.hpp"

#include <ImGui/imgui.h>

#include <algorithm>
#include <cstring>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void CommandPalette::Open()
    {
        m_Open       = true;
        m_JustOpened = true;
        m_Selected   = 0;
        m_Query[0]   = '\0';
    }

    void CommandPalette::Draw()
    {
        if ( !m_Open )
            return;

        constexpr const char* kPopupId = "##CommandPalette";
        if ( m_JustOpened )
            ImGui::OpenPopup( kPopupId );

        // Centered near the top of the main viewport, Sublime/VSCode style.
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos( ImVec2( center.x, center.y * 0.5f ), ImGuiCond_Always,
                                 ImVec2( 0.5f, 0.5f ) );
        ImGui::SetNextWindowSize( ImVec2( 560.0f, 0.0f ), ImGuiCond_Always );

        if ( !ImGui::BeginPopup( kPopupId ) )
        {
            m_Open = false; // popup dismissed (click outside)
            return;
        }

        // Rank the commands against the current query.
        struct Scored
        {
            const PaletteCommand* Cmd;
            int                   Score;
        };
        std::vector<Scored> hits;
        hits.reserve( m_Commands.size() );
        for ( const auto& c : m_Commands )
        {
            int score = 0;
            if ( FuzzyMatch( m_Query, c.Label, score ) )
                hits.push_back( { &c, score } );
        }
        std::stable_sort( hits.begin(), hits.end(),
                          []( const Scored& a, const Scored& b ) { return a.Score > b.Score; } );

        if ( hits.empty() )
            m_Selected = 0;
        else
            m_Selected = std::clamp( m_Selected, 0, static_cast<int>( hits.size() ) - 1 );

        // Keyboard navigation (read before the InputText eats the frame's key state).
        if ( ImGui::IsKeyPressed( ImGuiKey_DownArrow, true ) )
            ++m_Selected;
        if ( ImGui::IsKeyPressed( ImGuiKey_UpArrow, true ) )
            --m_Selected;
        if ( !hits.empty() )
            m_Selected = ( m_Selected + static_cast<int>( hits.size() ) ) % static_cast<int>( hits.size() );

        if ( m_JustOpened )
        {
            ImGui::SetKeyboardFocusHere();
            m_JustOpened = false;
        }
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputTextWithHint( "##paletteQuery", "Go to panel, entity, action...", m_Query,
                                  sizeof( m_Query ) );

        const bool enter  = ImGui::IsKeyPressed( ImGuiKey_Enter, false ) ||
                           ImGui::IsKeyPressed( ImGuiKey_KeypadEnter, false );
        const bool escape = ImGui::IsKeyPressed( ImGuiKey_Escape, false );

        ImGui::Separator();
        ImGui::BeginChild( "##paletteResults", ImVec2( 0.0f, 320.0f ) );
        for ( int i = 0; i < static_cast<int>( hits.size() ); ++i )
        {
            const PaletteCommand& c        = *hits[i].Cmd;
            const bool            selected = ( i == m_Selected );
            if ( ImGui::Selectable( ( c.Label + "##" + std::to_string( i ) ).c_str(), selected ) )
            {
                c.Run();
                ImGui::CloseCurrentPopup();
                m_Open = false;
            }
            if ( selected )
                ImGui::SetScrollHereY( 0.5f );
            ImGui::SameLine();
            ImGui::TextDisabled( "  %s", c.Group.c_str() );
        }
        ImGui::EndChild();

        if ( enter && !hits.empty() )
        {
            hits[m_Selected].Cmd->Run();
            ImGui::CloseCurrentPopup();
            m_Open = false;
        }
        if ( escape )
        {
            ImGui::CloseCurrentPopup();
            m_Open = false;
        }

        ImGui::EndPopup();
    }
} // namespace Desert::Editor
