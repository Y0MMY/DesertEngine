#include "LogsPanel.hpp"

#include <Editor/Core/LogView.hpp>

#include <ImGui/imgui.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    static constexpr const char* k_LogFile = "engine_log.txt";

    LogsPanel::LogsPanel()
         : IPanel( "Logs" )
    {
        Refresh();
    }

    void LogsPanel::Refresh()
    {
        if ( !std::filesystem::exists( k_LogFile ) )
            return;

        auto writeTime = std::filesystem::last_write_time( k_LogFile );
        if ( writeTime == m_LastWriteTime )
            return;

        m_LastWriteTime = writeTime;
        m_Entries.clear();

        std::ifstream file( k_LogFile );
        std::string   line;
        while ( std::getline( file, line ) )
        {
            if ( line.empty() )
                continue;

            int level = 0;
            if ( line.find( "[warning]" ) != std::string::npos )
                level = 1;
            else if ( line.find( "[error]" ) != std::string::npos ||
                      line.find( "[critical]" ) != std::string::npos )
                level = 2;

            m_Entries.push_back( { std::move( line ), level } );
        }

        m_ScrollToBottom = true;
    }

    void LogsPanel::DrawToolbar()
    {
        if ( ImGui::Button( "Clear" ) )
        {
            m_Entries.clear();
            std::ofstream( k_LogFile, std::ios::trunc ).close();
            m_LastWriteTime = {};
        }
        ImGui::SameLine();

        ImGui::Separator();
        ImGui::SameLine();

        const auto pushToggle = []( const char* label, bool& flag, ImVec4 activeColor )
        {
            if ( !flag )
                ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyleColorVec4( ImGuiCol_FrameBg ) );
            else
                ImGui::PushStyleColor( ImGuiCol_Button, activeColor );

            if ( ImGui::Button( label ) )
                flag = !flag;
            ImGui::PopStyleColor();
            ImGui::SameLine();
        };

        pushToggle( "Info",  m_ShowInfo,     ImVec4( 0.3f, 0.3f, 0.3f, 1.0f ) );
        pushToggle( "Warn",  m_ShowWarnings, ImVec4( 0.6f, 0.5f, 0.1f, 1.0f ) );
        pushToggle( "Error", m_ShowErrors,   ImVec4( 0.6f, 0.1f, 0.1f, 1.0f ) );

        // Search box.
        ImGui::SetNextItemWidth( 220.0f );
        ImGui::InputTextWithHint( "##logFilter", "search...", m_Filter, sizeof( m_Filter ) );
        if ( m_Filter[0] != '\0' )
        {
            ImGui::SameLine();
            if ( ImGui::Button( "x##clearFilter" ) )
                m_Filter[0] = '\0';
        }
        ImGui::SameLine();
        ImGui::Checkbox( "Collapse", &m_Collapse );

        ImGui::SameLine( ImGui::GetContentRegionMax().x - 90.0f );
        ImGui::Checkbox( "Auto-scroll", &m_ScrollToBottom );
    }

    void LogsPanel::OnUIRender()
    {
        Refresh();

        DrawToolbar();
        ImGui::Separator();

        ImGui::BeginChild( "##logscroll", ImVec2( 0.0f, 0.0f ), false,
                           ImGuiWindowFlags_HorizontalScrollbar );

        static const ImVec4 s_ColorInfo     = ImVec4( 0.85f, 0.85f, 0.85f, 1.0f );
        static const ImVec4 s_ColorWarning  = ImVec4( 0.95f, 0.85f, 0.20f, 1.0f );
        static const ImVec4 s_ColorError    = ImVec4( 0.95f, 0.35f, 0.35f, 1.0f );

        // Build the filtered subset (level toggles + case-insensitive search), optionally collapse runs of
        // consecutive duplicates, then render only the on-screen rows via a clipper. The clipper keeps this
        // O(visible) even for a huge log; all rows are single-line, so it is exact.
        // Rebuild the row list ONLY when something it depends on changed — see the note on m_Rows. Doing
        // this per frame cost more than rendering the scene.
        const bool viewDirty = m_RowsBuiltFromCount != m_Entries.size() || m_RowsInfo != m_ShowInfo ||
                               m_RowsWarnings != m_ShowWarnings || m_RowsErrors != m_ShowErrors ||
                               m_RowsCollapse != m_Collapse ||
                               std::strncmp( m_RowsFilter, m_Filter, sizeof( m_RowsFilter ) ) != 0;
        if ( viewDirty )
        {
            std::vector<std::pair<std::string, int>> filtered;
            filtered.reserve( m_Entries.size() );
            for ( const auto& entry : m_Entries )
            {
                if ( entry.Level == 0 && !m_ShowInfo )     continue;
                if ( entry.Level == 1 && !m_ShowWarnings ) continue;
                if ( entry.Level == 2 && !m_ShowErrors )   continue;
                if ( !LogMatches( entry.Text, m_Filter ) ) continue;
                filtered.emplace_back( entry.Text, entry.Level );
            }

            if ( m_Collapse )
            {
                m_Rows = CollapseConsecutive( filtered );
            }
            else
            {
                m_Rows.clear();
                m_Rows.reserve( filtered.size() );
                for ( auto& [text, level] : filtered )
                    m_Rows.push_back( { std::move( text ), level, 1 } );
            }

            m_RowsBuiltFromCount = m_Entries.size();
            m_RowsInfo           = m_ShowInfo;
            m_RowsWarnings       = m_ShowWarnings;
            m_RowsErrors         = m_ShowErrors;
            m_RowsCollapse       = m_Collapse;
            std::memcpy( m_RowsFilter, m_Filter, sizeof( m_RowsFilter ) );
        }

        const std::vector<LogRun>& rows = m_Rows;

        ImGuiListClipper clipper;
        clipper.Begin( static_cast<int>( rows.size() ) );
        while ( clipper.Step() )
        {
            for ( int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row )
            {
                const LogRun& entry = rows[row];
                const ImVec4& color = ( entry.Level == 2 )   ? s_ColorError
                                      : ( entry.Level == 1 ) ? s_ColorWarning
                                                             : s_ColorInfo;

                ImGui::PushStyleColor( ImGuiCol_Text, color );
                if ( entry.Count > 1 )
                    ImGui::Text( "%s  (x%d)", entry.Text.c_str(), entry.Count );
                else
                    ImGui::TextUnformatted( entry.Text.c_str() );
                ImGui::PopStyleColor();
            }
        }
        clipper.End();

        // Right-click the log area to copy the currently visible lines.
        if ( ImGui::BeginPopupContextWindow() )
        {
            if ( ImGui::MenuItem( "Copy visible" ) )
            {
                std::string all;
                for ( const auto& r : rows )
                {
                    all += r.Text;
                    if ( r.Count > 1 )
                        all += "  (x" + std::to_string( r.Count ) + ")";
                    all += '\n';
                }
                ImGui::SetClipboardText( all.c_str() );
            }
            ImGui::EndPopup();
        }

        if ( m_ScrollToBottom )
        {
            ImGui::SetScrollHereY( 1.0f );
            m_ScrollToBottom = false;
        }

        ImGui::EndChild();
    }

} // namespace Desert::Editor
