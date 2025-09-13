#include "ShaderLibraryPanel.hpp"

#include <ImGui/imgui_internal.h>

#include <Common/Utilities/FileSystem.hpp>

#include "../../Core/EditorResources.hpp"
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Core/ThemeManager.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    ShaderLibraryPanel::ShaderLibraryPanel() : IPanel( "Shader Library", false )
    {
    }

    void ShaderLibraryPanel::OnUIRender()
    {
        auto flags = ImGuiWindowFlags_NoCollapse;

        ImRect windowRect = { ImGui::GetWindowContentRegionMin(), ImGui::GetWindowContentRegionMax() };

        ImGui::PushStyleColor( ImGuiCol_MenuBarBg, ImGui::GetStyleColorVec4( ImGuiCol_TabActive ) );

        // Search bar
        ImGui::TextUnformatted( ICON_MDI_MAGNIFY );
        ImGui::SameLine();

        ImGui::PushFont( EditorResources::GetBoldFont() );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );

        m_ShaderFilter.Draw( "##ShaderFilter",
                             ImGui::GetContentRegionAvail().x - ImGui::GetStyle().IndentSpacing );
        Utils::ImGuiUtilities::DrawItemActivityOutline( 2.0f, false, ImColor( 80, 80, 80 ) );

        bool isFilterFocused = ImGui::IsItemActive();

        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        if ( !m_ShaderFilter.IsActive() && !isFilterFocused )
        {
            ImGui::SameLine();
            ImGui::PushFont( EditorResources::GetBoldFont() );
            ImGui::SetCursorPosX( ImGui::GetFontSize() * 4.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0.0f, ImGui::GetStyle().FramePadding.y ) );
            ImGui::TextUnformatted( "Search shaders..." );
            ImGui::PopFont();
            ImGui::PopStyleVar();
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();

        // Shaders list
        /*{
            ImGui::Indent();

            const auto& allShaders = Graphic::ShaderLibrary::GetAll();
            for ( const auto& [shaderName, shader] : allShaders )
            {
                if ( m_ShaderFilter.IsActive() && !m_ShaderFilter.PassFilter( shaderName.c_str() ) )
                {
                    continue;
                }

                DrawShaderNode( shaderName, shader );
            }

            ImGui::Unindent();
        }*/

        if ( m_ShowShaderCode && m_SelectedShader )
        {
            RenderShaderCodeWindow();
        }

        if ( ImGui::IsWindowFocused() )
        {
            // Handle window focus if needed
        }
    }

    void ShaderLibraryPanel::DrawShaderNode( const std::string&                      name,
                                             const std::shared_ptr<Graphic::Shader>& shader )
    {
        bool show = true;

        bool visible = ImGui::IsRectVisible(
             ImVec2( ImGui::GetContentRegionMax().x, ImGui::GetTextLineHeightWithSpacing() ) );
        if ( !visible )
        {
            ImGui::NewLine();
            return;
        }

        if ( show )
        {
            Utils::ImGuiUtilities::PushID();

            const bool isSelected = m_SelectedShader == shader.get();

            ImGuiTreeNodeFlags nodeFlags = ( isSelected ) ? ImGuiTreeNodeFlags_Selected : 0;
            nodeFlags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding |
                         ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanAvailWidth |
                         ImGuiTreeNodeFlags_Leaf;

            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetIconColor() );

            char* icon = (char*)ICON_MDI_CODE_BRACES;

            bool nodeOpen = ImGui::TreeNodeEx( name.c_str(), nodeFlags, "%s", icon );
            {
                if ( ImGui::IsMouseReleased( ImGuiMouseButton_Left ) && ImGui::IsItemHovered() &&
                     !ImGui::IsItemToggledOpen() )
                {
                    m_SelectedShader = shader.get();
                }
            }

            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::TextUnformatted( (const char*)name.c_str() );

            bool hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_None );

            // Context menu
            if ( ImGui::BeginPopupContextItem( (const char*)name.c_str() ) )
            {
                const auto sourceCode = Common::Utils::FileSystem::ReadFileContent( shader->GetFilepath() );
                if ( ImGui::Selectable( "View Code" ) )
                {
                    m_SelectedShader = shader.get();
                    m_ShaderSource   = sourceCode;
                    m_ShowShaderCode = true;
                }

                if ( ImGui::Selectable( "Edit" ) )
                {
                    m_SelectedShader = shader.get();
                    m_ShaderSource   = sourceCode;
                    m_ShowShaderCode = true;
                    m_EditMode       = true;
                }

                if ( ImGui::Selectable( "Reload" ) )
                {
                    shader->Reload();
                }

                ImGui::Separator();

                if ( ImGui::Selectable( "Copy Path" ) )
                {
                    // Implement copy path functionality
                }

                ImGui::EndPopup();
            }

            // Eye button for quick actions
            bool showButton = hovered;
            if ( showButton )
            {
                ImGui::SameLine( ImGui::GetWindowContentRegionMax().x -
                                 ImGui::CalcTextSize( ICON_MDI_EYE ).x * 2.0f );
                ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.7f, 0.7f, 0.7f, 0.0f ) );
                if ( ImGui::Button( ICON_MDI_EYE ) )
                {
                    const auto sourceCode = Common::Utils::FileSystem::ReadFileContent( shader->GetFilepath() );

                    m_SelectedShader = shader.get();
                    m_ShaderSource   = sourceCode;
                    m_ShowShaderCode = true;
                }
                ImGui::PopStyleColor();
            }

            if ( nodeOpen == false )
            {
                Utils::ImGuiUtilities::PopID();
                return;
            }

            // Show shader details
            if ( nodeOpen )
            {
                const auto& defines = shader->GetDefines();
                if ( !defines.empty() )
                {
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.7f, 0.7f, 0.7f, 1.0f ) );
                    ImGui::Indent();
                    for ( const auto& [key, value] : defines )
                    {
                        ImGui::BulletText( "%s = %s", key.c_str(), value.c_str() );
                    }
                    ImGui::Unindent();
                    ImGui::PopStyleColor();
                }

                ImGui::TreePop();
            }

            Utils::ImGuiUtilities::PopID();
        }
    }

    void ShaderLibraryPanel::RenderShaderCodeWindow()
    {
        ImGui::SetNextWindowSize( ImVec2( 800, 600 ), ImGuiCond_FirstUseEver );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10, 10 ) );

        if ( ImGui::Begin( "Shader Code", &m_ShowShaderCode,
                           ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_HorizontalScrollbar ) )
        {
            // Menu bar
            if ( ImGui::BeginMenuBar() )
            {
                if ( ImGui::MenuItem( "Save", "Ctrl+S", false, m_EditMode ) )
                {
                    SaveShaderCode();
                }

                if ( ImGui::MenuItem( "Reload" ) )
                {
                    if ( m_SelectedShader )
                    {
                        m_SelectedShader->Reload();
                    }
                }

                if ( ImGui::MenuItem( "Copy" ) )
                {
                    ImGui::SetClipboardText( m_ShaderSource.c_str() );
                }

                ImGui::EndMenuBar();
            }

            // Shader name and info
            ImGui::TextColored( ImVec4( 0.4f, 0.8f, 1.0f, 1.0f ), "%s",
                                m_SelectedShader ? m_SelectedShader->GetName().c_str() : "Unknown" );
            ImGui::Separator();

            // Code editor/viewer
            ImGui::BeginChild( "CodeEditor", ImVec2( 0, 0 ), true );
            {
                if ( m_EditMode )
                {
                    // Edit mode - text input
                    static char buffer[1024 * 1024]; // 1MB buffer
                    strncpy( buffer, m_ShaderSource.c_str(), sizeof( buffer ) );
                    buffer[sizeof( buffer ) - 1] = '\0';

                    if ( ImGui::InputTextMultiline( "##ShaderCode", buffer, sizeof( buffer ), ImVec2( -1, -1 ),
                                                    ImGuiInputTextFlags_AllowTabInput ) )
                    {
                        m_ShaderSource = buffer;
                    }
                }
                else
                {
                    // View mode - syntax highlighted code
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.9f, 0.9f, 0.9f, 1.0f ) );

                    std::istringstream iss( m_ShaderSource );
                    std::string        line;
                    int                lineNum = 1;

                    while ( std::getline( iss, line ) )
                    {
                        // Basic GLSL syntax highlighting
                        ImVec4 color = ImVec4( 0.9f, 0.9f, 0.9f, 1.0f );

                        // Highlight keywords
                        if ( line.find( "void" ) != std::string::npos ||
                             line.find( "float" ) != std::string::npos ||
                             line.find( "vec" ) != std::string::npos || line.find( "mat" ) != std::string::npos ||
                             line.find( "uniform" ) != std::string::npos ||
                             line.find( "in" ) != std::string::npos || line.find( "out" ) != std::string::npos ||
                             line.find( "return" ) != std::string::npos )
                        {
                            color = ImVec4( 0.4f, 0.8f, 1.0f, 1.0f );
                        }
                        // Highlight numbers
                        else if ( line.find_first_of( "0123456789" ) != std::string::npos )
                        {
                            color = ImVec4( 1.0f, 0.8f, 0.4f, 1.0f );
                        }
                        // Highlight comments
                        else if ( line.find( "//" ) != std::string::npos )
                        {
                            color = ImVec4( 0.5f, 0.5f, 0.5f, 1.0f );
                        }

                        ImGui::TextColored( ImVec4( 0.5f, 0.5f, 0.6f, 1.0f ), "%4d", lineNum++ );
                        ImGui::SameLine();
                        ImGui::TextColored( color, "%s", line.c_str() );
                    }

                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndChild();

            // Edit/Save buttons
            ImGui::Separator();
            if ( m_EditMode )
            {
                if ( ImGui::Button( "Save", ImVec2( 80, 0 ) ) )
                {
                    SaveShaderCode();
                }
                ImGui::SameLine();
                if ( ImGui::Button( "Cancel", ImVec2( 80, 0 ) ) )
                {
                    m_EditMode = false;
                    if ( m_SelectedShader )
                    {
                    }
                }
            }
            else
            {
                if ( ImGui::Button( "Edit", ImVec2( 80, 0 ) ) )
                {
                    m_EditMode = true;
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ShaderLibraryPanel::SaveShaderCode()
    {
        if ( !m_SelectedShader )
            return;

        // Save to file (you'll need to implement this based on your engine's file system)
        // For now, just reload the shader with new source
        //m_SelectedShader->ReloadFromSource( m_ShaderSource );
        m_EditMode = false;
    }

} // namespace Desert::Editor