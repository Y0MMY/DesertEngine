#include "ShaderLibraryPanel.hpp"

#include "../../Core/FontAwesomeDefinitions.hpp"

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    ShaderLibraryPanel::ShaderLibraryPanel() : IPanel( "Shader Library", false )
    {
    }

    void ShaderLibraryPanel::OnUIRender()
    {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10, 10 ) );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) );

        ImGui::Begin( m_PanelName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar );

        static char searchQuery[128] = "";

        ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.15f, 0.15f, 0.17f, 1.0f ) );
        ImGui::BeginChild( "SearchBar", ImVec2( 0, 50 ), true );
        {
            ImGui::SetCursorPosY( ImGui::GetCursorPosY() + 5 );
            ImGui::Text( ICON_FA_SEARCH );
            ImGui::SameLine();

            ImGui::PushItemWidth( -1 );
            ImGui::InputTextWithHint( "##Search", "Search shaders...", searchQuery, IM_ARRAYSIZE( searchQuery ) );
            ImGui::PopItemWidth();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::BeginChild( "ShadersList", ImVec2( 0, -ImGui::GetFrameHeightWithSpacing() ) );
        {
            const auto& allShaders = Graphic::ShaderLibrary::GetAll();

            for ( const auto& [shaderName, shader] : allShaders )
            {
                if ( searchQuery[0] != '\0' && shaderName.find( searchQuery ) == std::string::npos )
                {
                    continue;
                }

                ImGui::PushID( shaderName.c_str() );
                ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 5.0f );
                ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.18f, 0.18f, 0.20f, 1.0f ) );

                if ( ImGui::BeginChild( "Card", ImVec2( 0, 80 ), true, ImGuiWindowFlags_NoScrollbar ) )
                {
                    ImGui::BeginGroup();
                    ImGui::TextColored( ImVec4( 0.4f, 0.8f, 1.0f, 1.0f ), "%s", shaderName.c_str() );

                    const auto& defines = shader->GetDefines();
                    if ( !defines.empty() )
                    {
                        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.7f, 0.7f, 0.7f, 1.0f ) );
                        for ( const auto& [key, value] : shader->GetDefines() )
                        {
                            ImGui::BulletText( "%s = %s", key.c_str(), value.c_str() );
                        }
                        ImGui::PopStyleColor();
                    }
                    ImGui::EndGroup();

                    ImGui::SameLine( ImGui::GetWindowWidth() - 50 );
                    ImGui::SetCursorPosY( ImGui::GetCursorPosY() + 20 );
                    if ( ImGui::Button( "View##Shader", ImVec2( 40, 40 ) ) )
                    {
                        m_SelectedShader = shader.get();
                        // m_ShaderSource   = shader->GetSourceCode();
                        m_ShowShaderCode = true;
                    }
                    if ( ImGui::IsItemHovered() )
                    {
                        ImGui::SetTooltip( "View shader code" );
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                ImGui::PopID();

                ImGui::Dummy( ImVec2( 0, 5 ) );
            }
        }
        ImGui::EndChild();

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        if ( m_ShowShaderCode && m_SelectedShader )
        {
            RenderShaderCodeWindow();
        }
    }

    void ShaderLibraryPanel::RenderShaderCodeWindow()
    {
        ImGui::SetNextWindowSize( ImVec2( 800, 600 ), ImGuiCond_FirstUseEver );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 15, 15 ) );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.09f, 0.09f, 0.10f, 1.0f ) );

        if ( ImGui::Begin( "Shader Code", &m_ShowShaderCode,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar ) )
        {
            if ( ImGui::BeginMenuBar() )
            {
                if ( ImGui::MenuItem( "Copy to Clipboard" ) )
                {
                    ImGui::SetClipboardText( m_ShaderSource.c_str() );
                }
                ImGui::EndMenuBar();
            }

            ImGui::TextColored( ImVec4( 0.4f, 0.8f, 1.0f, 1.0f ), "%s", m_SelectedShader->GetName().c_str() );
            ImGui::Separator();

            ImGui::BeginChild( "CodeView", ImVec2( 0, 0 ), true, ImGuiWindowFlags_HorizontalScrollbar );
            {
                //  ImGui::PushFont( GetMonoFont() );
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.9f, 0.9f, 0.9f, 1.0f ) );

                std::istringstream iss( m_ShaderSource );
                std::string        line;
                int                lineNum = 1;

                while ( std::getline( iss, line ) )
                {
                    ImGui::TextColored( ImVec4( 0.5f, 0.5f, 0.6f, 1.0f ), "%4d", lineNum++ );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( line.c_str() );
                }

                ImGui::PopStyleColor();
                // ImGui::PopFont();
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

} // namespace Desert::Editor