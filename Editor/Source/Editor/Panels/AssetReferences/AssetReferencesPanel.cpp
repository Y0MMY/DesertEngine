#include "AssetReferencesPanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>

#include <algorithm>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        // Leaf asset types worth flagging as unused (roots like scenes/prefabs are never "orphans").
        const std::vector<std::string> kLeafExts = { ".demat", ".png", ".jpg", ".jpeg",
                                                     ".tga",   ".hdr", ".bmp" };
    } // namespace

    void AssetReferencesPanel::Rebuild()
    {
        BuildProjectAssetReferenceIndex( m_Index );

        m_AllPaths.clear();
        for ( const auto& e : m_Index.Entries() )
            m_AllPaths.push_back( e.Path );
        std::sort( m_AllPaths.begin(), m_AllPaths.end() );

        m_Orphans  = m_Index.Orphans( kLeafExts );
        m_Selected = -1;
        m_Built    = true;
    }

    void AssetReferencesPanel::OnUIRender()
    {
        if ( ImGui::Button( ICON_MDI_REFRESH "  Rebuild index" ) )
            Rebuild();
        ImGui::SameLine();
        if ( m_Built )
            ImGui::TextDisabled( "%zu assets indexed", m_Index.Entries().size() );
        else
            ImGui::TextDisabled( "not scanned yet" );

        if ( !m_Built )
        {
            ImGui::Spacing();
            ImGui::TextWrapped( "Scans the project's Assets for cross-references (handle- and "
                                "path-based). It is a usage search, not a proof — use it to find "
                                "references and unused assets before deleting." );
            return;
        }

        ImGui::Separator();
        ImGui::TextUnformatted( "Referenced by" );

        const char* preview = m_Selected >= 0 && m_Selected < static_cast<int>( m_AllPaths.size() )
                                   ? m_AllPaths[m_Selected].c_str()
                                   : "select an asset...";
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::BeginCombo( "##refAsset", preview ) )
        {
            for ( int i = 0; i < static_cast<int>( m_AllPaths.size() ); ++i )
                if ( ImGui::Selectable( m_AllPaths[i].c_str(), i == m_Selected ) )
                    m_Selected = i;
            ImGui::EndCombo();
        }

        if ( m_Selected >= 0 && m_Selected < static_cast<int>( m_AllPaths.size() ) )
        {
            const auto refs = m_Index.ReferencersOf( m_AllPaths[m_Selected] );
            ImGui::BeginChild( "##referencers", ImVec2( 0.0f, 140.0f ), true );
            if ( refs.empty() )
                ImGui::TextDisabled( ICON_MDI_ALERT_OUTLINE "  Nothing references this asset." );
            else
                for ( const auto& r : refs )
                    ImGui::BulletText( "%s", r.c_str() );
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text( ICON_MDI_DELETE_SWEEP "  Unused leaf assets (%zu)", m_Orphans.size() );
        ImGui::BeginChild( "##orphans", ImVec2( 0.0f, 0.0f ), true );
        if ( m_Orphans.empty() )
            ImGui::TextDisabled( "None — every texture/material is referenced." );
        else
            for ( const auto& o : m_Orphans )
                ImGui::BulletText( "%s", o.c_str() );
        ImGui::EndChild();
    }
} // namespace Desert::Editor
