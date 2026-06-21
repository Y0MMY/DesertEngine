#include "LogsPanel.hpp"

#include <ImGui/imgui.h>

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

        for ( const auto& entry : m_Entries )
        {
            if ( entry.Level == 0 && !m_ShowInfo )     continue;
            if ( entry.Level == 1 && !m_ShowWarnings ) continue;
            if ( entry.Level == 2 && !m_ShowErrors )   continue;

            const ImVec4& color =
                 ( entry.Level == 2 ) ? s_ColorError :
                 ( entry.Level == 1 ) ? s_ColorWarning : s_ColorInfo;

            ImGui::PushStyleColor( ImGuiCol_Text, color );
            ImGui::TextUnformatted( entry.Text.c_str() );
            ImGui::PopStyleColor();
        }

        if ( m_ScrollToBottom )
        {
            ImGui::SetScrollHereY( 1.0f );
            m_ScrollToBottom = false;
        }

        ImGui::EndChild();
    }

} // namespace Desert::Editor
