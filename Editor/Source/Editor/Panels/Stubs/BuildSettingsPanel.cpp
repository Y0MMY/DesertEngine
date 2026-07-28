#include "BuildSettingsPanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void BuildSettingsPanel::OnUIRender()
    {
        ImGui::TextDisabled( "Preview — the packaging pipeline is not implemented yet." );
        ImGui::Separator();

        ImGui::Spacing();
        ImGui::TextUnformatted( "Target platform" );
        const char* platforms[] = { ICON_MDI_APPLE "  macOS (Apple Silicon)", ICON_MDI_MICROSOFT_WINDOWS "  Windows x64",
                                    ICON_MDI_LINUX "  Linux x64 (planned)" };
        for ( int i = 0; i < 3; ++i )
        {
            const bool selectable = i < 2; // Linux is a placeholder row
            ImGui::BeginDisabled( !selectable );
            if ( ImGui::RadioButton( platforms[i], m_Platform == i ) && selectable )
                m_Platform = i;
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted( "Configuration" );
        ImGui::RadioButton( "Debug", &m_Config, 0 );
        ImGui::SameLine();
        ImGui::RadioButton( "Release", &m_Config, 1 );

        ImGui::Spacing();
        ImGui::TextUnformatted( "Output folder" );
        ImGui::SetNextItemWidth( 320.0f );
        Utils::ImGuiUtilities::InputText( m_OutputDir, "##BuildOutputDir" );

        ImGui::Spacing();
        ImGui::TextUnformatted( "Scenes in build" );
        ImGui::BeginChild( "##buildScenes", ImVec2( 0.0f, 90.0f ), true );
        ImGui::TextDisabled( ICON_MDI_MOVIE_OPEN "  <current scene>  (scene list wiring comes with the pipeline)" );
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::BeginDisabled();
        ImGui::Button( ICON_MDI_PACKAGE_VARIANT_CLOSED "  Build", ImVec2( 160.0f, 0.0f ) );
        ImGui::EndDisabled();
        if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
            ImGui::SetTooltip( "Coming soon: cook assets + bundle the runtime for the selected platform." );
    }
} // namespace Desert::Editor
