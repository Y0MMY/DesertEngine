#include "CloudTypePanel.hpp"

#include <Editor/Core/ImGuiUtilities.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/CloudNoiseVolumeAsset.hpp>
#include <Engine/Assets/CloudTypeAsset.hpp>
#include <Engine/Graphic/Clouds/CloudProfileTable.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <ImGui/imgui.h>

#include <cstring>
#include <filesystem>

namespace Desert::Editor
{
    // The editor's ImGui lives in the global namespace; unqualified `ImGui::` inside `Desert::` would
    // resolve to `Desert::ImGui`, which is the engine's own runtime UI. Every panel in this folder opens
    // with the same alias for the same reason.
    namespace ImGui = ::ImGui;

    namespace
    {
        // How many points the profile preview is drawn with. 256 is the table's own altitude resolution,
        // so the curve on screen is the curve that will be uploaded rather than a smoother idea of it.
        constexpr int kPreviewSamples = 256;

        void CopyInto( char* buffer, size_t size, const std::string& text )
        {
            const size_t n = text.size() < size - 1 ? text.size() : size - 1;
            std::memcpy( buffer, text.data(), n );
            buffer[n] = '\0';
        }

        // A `.dcnv` path as the type file stores it: relative to the project's assets root, forward
        // slashes. The asset joins it back on load, and doing the relativisation anywhere else is how a
        // library ends up carrying one developer's home directory.
        std::string RelativeToAssets( const Common::Filepath& full )
        {
            std::error_code ec;
            const auto      rel = std::filesystem::relative( full, Common::Constants::Path::ASSETS_PATH, ec );
            if ( ec || rel.empty() )
                return full.generic_string();
            return rel.generic_string();
        }
    } // namespace

    CloudTypePanel::CloudTypePanel( Assets::AssetManager* assets )
         : IPanel( "Cloud Type", /*showPanel=*/false ), m_Assets( assets )
    {
        CopyInto( m_NameBuffer, sizeof( m_NameBuffer ), m_Data.DisplayName.value_or( "" ) );
        CopyInto( m_NotesBuffer, sizeof( m_NotesBuffer ), m_Data.Notes.value_or( "" ) );
    }

    void CloudTypePanel::OnUIRender()
    {
        if ( !m_SowPanel )
            return;

        if ( ImGui::Begin( m_PanelName.c_str(), &m_SowPanel ) )
        {
            ImGui::TextWrapped(
                 "A cloud TYPE is twelve numbers plus the noise its edge is cut from. They generate the "
                 "vertical profile table the march samples (256 x 64), so a type is small, readable and "
                 "editable — there is no baked texture here to go stale against the maths." );
            ImGui::Separator();

            DrawLibrarySection();
            ImGui::Separator();
            DrawShapeSection();
            ImGui::Separator();
            DrawNoiseSection();
            ImGui::Separator();
            DrawPreviewSection();
            ImGui::Separator();
            DrawSaveSection();

            if ( !m_Status.empty() )
            {
                ImGui::Separator();
                if ( m_StatusIsError )
                    ImGui::TextColored( ImVec4( 0.95f, 0.45f, 0.40f, 1.0f ), "%s", m_Status.c_str() );
                else
                    ImGui::TextWrapped( "%s", m_Status.c_str() );
            }
        }
        ImGui::End();
    }

    void CloudTypePanel::OpenType( const Common::Filepath& path )
    {
        if ( !m_Assets )
            return;

        auto asset = m_Assets->FindByPath<Assets::CloudTypeAsset>( path );
        if ( !asset )
            asset = m_Assets->CreateAsset<Assets::CloudTypeAsset>( Assets::AssetPriority::Medium, path );

        if ( !asset || !asset->IsReadyForUse() )
        {
            m_Status        = "'" + path.string() + "' could not be opened — the log says why.";
            m_StatusIsError = true;
            return;
        }

        m_Data       = asset->GetData();
        m_SourcePath = path;
        m_SourceName = path.filename().string();
        CopyInto( m_NameBuffer, sizeof( m_NameBuffer ), m_Data.DisplayName.value_or( "" ) );
        CopyInto( m_NotesBuffer, sizeof( m_NotesBuffer ), m_Data.Notes.value_or( "" ) );

        m_Status        = "Opened " + m_SourceName + ".";
        m_StatusIsError = false;
    }

    void CloudTypePanel::DrawLibrarySection()
    {
        Utils::ImGuiUtilities::SectionHeader( "Library" );

        ImGui::Text( "Editing: %s", m_SourceName.c_str() );

        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::BeginCombo( "##openType", "Open a type..." ) )
        {
            if ( m_Assets )
            {
                for ( const auto& [handle, type] : m_Assets->FindAllByType<Assets::CloudTypeAsset>() )
                {
                    if ( ImGui::Selectable( type->GetDisplayName().c_str() ) )
                        OpenType( type->GetMetadata().Filepath );
                }
            }
            ImGui::EndCombo();
        }

        if ( ImGui::Button( "New from built-in" ) )
        {
            // A NEW TYPE STARTS FROM THE BUILT-IN DEFAULT rather than from zeros, because a type of zeros
            // is a type that renders nothing and the first thing an artist would have to do is discover
            // twelve plausible numbers. Starting from the cumulus everything else is measured against means
            // the first edit is already a cloud.
            m_Data = Assets::CloudTypeDefault();
            m_SourcePath.clear();
            m_SourceName = "(new, unsaved)";
            CopyInto( m_NameBuffer, sizeof( m_NameBuffer ), "New cloud type" );
            CopyInto( m_NotesBuffer, sizeof( m_NotesBuffer ), "" );
            m_Status        = "Started a new type from the built-in cumulus congestus.";
            m_StatusIsError = false;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Open File..." ) )
        {
            const std::filesystem::path picked =
                 Common::Utils::FileSystem::OpenFileDialog( "Cloud Type\0*.decloudtype\0" );
            if ( !picked.empty() )
                OpenType( picked );
        }

        if ( ImGui::InputText( "Display Name", m_NameBuffer, sizeof( m_NameBuffer ) ) )
            m_Data.DisplayName = std::string( m_NameBuffer );
        if ( ImGui::InputTextMultiline( "Notes", m_NotesBuffer, sizeof( m_NotesBuffer ), ImVec2( -1.0f, 54.0f ) ) )
            m_Data.Notes = std::string( m_NotesBuffer );
    }

    void CloudTypePanel::DrawShapeSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "Shape" );

        Graphic::CloudTypeShape& s = m_Data.Shape;

        ImGui::SliderFloat( "Base Altitude (km)", &s.BaseAltitudeKm, 0.0f, 14.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Where this kind of cloud's base sits — its lifting condensation level. "
                               "ABSOLUTE, not a fraction of a layer: the shell the march intersects is "
                               "computed from this and the top, so moving it moves the clouds." );

        ImGui::SliderFloat( "Top Altitude (km)", &s.TopAltitudeKm, 0.0f, 16.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "The top this kind reaches at the CORE of a placement patch." );

        ImGui::SliderFloat( "Edge Top Fraction", &s.EdgeTopFraction, 0.0f, 1.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "How much of its full height it reaches where a patch has only just begun. "
                               "Near 1 is a sheet — the same everywhere. Near 0.1 is a cumulus field: a "
                               "flat pad at the rim of a patch and a tower in its middle." );

        ImGui::SliderFloat( "Base Ramp", &s.BaseRampFraction, 0.001f, 1.0f, "%.3f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "How much of the cloud's own height the base takes to reach full density. "
                               "Small is the flat bottom of a cumulus; as long as the taper it becomes the "
                               "symmetric section of a lenticular." );

        ImGui::SliderFloat( "Top Taper", &s.TopTaper, 0.001f, 1.0f, "%.3f" );
        ImGui::SliderFloat( "Anvil Strength", &s.AnvilStrength, 0.0f, 1.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "A SECOND lobe of cloud, above the tower and spreading wider than it. Zero "
                               "means this kind has none. It is what makes a cumulonimbus recognisable, and "
                               "it is the reason the profile is a table rather than a curve — no "
                               "single-humped curve has two maxima." );

        ImGui::BeginDisabled( s.AnvilStrength <= 0.0f );
        ImGui::SliderFloat( "Anvil Altitude (km)", &s.AnvilAltitudeKm, 0.0f, 16.0f, "%.2f" );
        ImGui::SliderFloat( "Anvil Thickness (km)", &s.AnvilThicknessKm, 0.0f, 5.0f, "%.2f" );
        ImGui::EndDisabled();

        Utils::ImGuiUtilities::SectionHeader( "Matter and edge" );

        ImGui::SliderFloat( "Detail Character", &s.DetailCharacter, 0.0f, 1.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "0 is wispy erosion, 1 is billowy. This is the type's EDGE, not its "
                               "silhouette: a cirrus is 0 and a stratocumulus is nearly 1." );

        ImGui::SliderFloat( "Detail Factor", &s.DetailFactor, 0.0f, 8.0f, "%.2f" );
        ImGui::SliderFloat( "Density Factor", &s.DensityFactor, 0.0f, 8.0f, "%.2f" );
        ImGui::SliderFloat( "Extinction Factor", &s.ExtinctionFactor, 0.0f, 8.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "The three factors MULTIPLY the layer's own Detail Strength, Density Scale "
                               "and Extinction Scale rather than replacing them, so 1 means \"this kind as "
                               "it is\" and the layer's sliders keep meaning what they meant." );

        Utils::ImGuiUtilities::SectionHeader( "Where it sits in the sky" );

        ImGui::SliderFloat( "Placement Scale", &s.PlacementScale, 0.05f, 8.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "How big this kind's patches are, as a MULTIPLE of the layer's Weather Tile "
                               "Size. Below 1 gives many small cells — a stratocumulus deck of one-kilometre "
                               "lumps; above 1 gives few large ones — a storm cell, or a sheet that never "
                               "ends. Each kind of cloud in the layer has its own, which is why a low deck "
                               "and a tall tower can be different sizes in the same sky." );

        ImGui::SliderFloat( "Placement Anisotropy", &s.PlacementAnisotropy, 0.1f, 16.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "How much longer the patches are along the wind than across it. 1 is round. "
                               "Above 1 combs them out downwind, which is what makes cirrus fibrous instead "
                               "of blotchy; BELOW 1 stretches them ACROSS the wind, which is what a wave "
                               "cloud is — a lenticular's crest lies perpendicular to the flow." );
    }

    void CloudTypePanel::DrawNoiseSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "Noise volume" );

        const std::string current = m_Data.NoiseVolume.value_or( std::string{} );
        const std::string preview = current.empty() ? "Default (built-in volume)" : current;

        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::BeginCombo( "##typeNoise", preview.c_str() ) )
        {
            if ( ImGui::Selectable( "Default (built-in volume)", current.empty() ) )
                m_Data.NoiseVolume.reset();

            if ( m_Assets )
            {
                for ( const auto& [handle, volume] : m_Assets->FindAllByType<Assets::CloudNoiseVolumeAsset>() )
                {
                    const std::string relative = RelativeToAssets( volume->GetMetadata().Filepath );
                    if ( ImGui::Selectable( relative.c_str(), relative == current ) )
                        m_Data.NoiseVolume = relative;
                }
            }
            ImGui::EndCombo();
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "The 3D noise this kind of cloud's edge is cut from. It belongs to the TYPE "
                               "rather than to the layer because the character of an edge is a property of "
                               "the kind of cloud: the shipped Cirrus names the finer of the two volumes, "
                               "and a cirrus cut from a cumulonimbus' noise is not a cirrus." );
    }

    void CloudTypePanel::DrawPreviewSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "Profile" );

        const Graphic::CloudTypeShape& s = m_Data.Shape;

        const float bottomKm = Graphic::CloudTypeBaseKm( s );
        const float topKm    = Graphic::CloudTypeTopKm( s );
        const float spanKm   = topKm - bottomKm;

        if ( !( spanKm > 0.0f ) )
        {
            ImGui::TextDisabled( "The top is not above the base, so there is no profile to draw." );
            return;
        }

        m_ProfileEdge.resize( kPreviewSamples );
        m_ProfileCore.resize( kPreviewSamples );

        for ( int i = 0; i < kPreviewSamples; ++i )
        {
            // The same half-texel convention the table itself is generated with, so the curve on screen is
            // the row that will be uploaded rather than a differently sampled version of it.
            const float fraction   = ( static_cast<float>( i ) + 0.5f ) / static_cast<float>( kPreviewSamples );
            const float altitudeKm = bottomKm + fraction * spanKm;

            m_ProfileEdge[i] = Graphic::CloudProfileCurve( s, altitudeKm, 0.0f );
            m_ProfileCore[i] = Graphic::CloudProfileCurve( s, altitudeKm, 1.0f );
        }

        ImGui::PlotLines( "##profileCore", m_ProfileCore.data(), kPreviewSamples, 0, "core of a patch (pattern 1)",
                          0.0f, 1.0f, ImVec2( -1.0f, 90.0f ) );
        ImGui::PlotLines( "##profileEdge", m_ProfileEdge.data(), kPreviewSamples, 0, "rim of a patch (pattern 0)",
                          0.0f, 1.0f, ImVec2( -1.0f, 90.0f ) );

        ImGui::TextDisabled( "Left is the base at %.2f km, right is the top of the shell at %.2f km "
                             "(%.2f km thick).",
                             bottomKm, topKm, spanKm );
        ImGui::TextDisabled( "TWO CURVES BECAUSE THE PROFILE IS A TABLE: if they look the same, this type "
                             "is the same shape everywhere in the sky." );
    }

    void CloudTypePanel::DrawSaveSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "Save" );

        // Validated BEFORE the button rather than after it, so an illegal set is refused where the artist
        // is looking and with the number that is wrong in the message. The same function the loader uses,
        // so the panel and the file format cannot disagree about what is legal.
        const auto valid = Assets::ValidateCloudTypeShape( m_Data.Shape );
        if ( !valid )
            ImGui::TextColored( ImVec4( 0.95f, 0.45f, 0.40f, 1.0f ), "Not saveable: %s",
                                valid.GetError().c_str() );

        ImGui::BeginDisabled( !valid );
        const bool save = ImGui::Button( "Save", ImVec2( 100.0f, 0.0f ) ) && !m_SourcePath.empty();
        ImGui::SameLine();
        const bool saveAs = ImGui::Button( "Save As...", ImVec2( 120.0f, 0.0f ) );
        ImGui::EndDisabled();

        std::filesystem::path target;
        if ( save )
        {
            target = m_SourcePath;
        }
        else if ( saveAs )
        {
            target = Common::Utils::FileSystem::SaveFileDialog( "Cloud Type\0*.decloudtype\0" );
            if ( !target.empty() && target.extension() != Assets::kCloudTypeExtension )
                target.replace_extension( Assets::kCloudTypeExtension );
        }

        if ( target.empty() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "Types live in %s", Common::Constants::Path::CLOUD_TYPE_PATH.string().c_str() );
            return;
        }

        const auto written = Assets::CloudTypeAsset::Save( target, m_Data );
        if ( !written )
        {
            m_Status        = "Save failed: " + written.GetError();
            m_StatusIsError = true;
            return;
        }

        m_SourcePath = target;
        m_SourceName = target.filename().string();

        // Re-registered straight away so a layer already pointing at this type shows the new numbers in
        // the viewport this frame. A tool whose output only appears after a restart is a tool nobody
        // iterates in — and the renderer needs the service's GENERATION to move, which Register is what
        // does.
        if ( m_Assets )
        {
            auto asset = m_Assets->FindByPath<Assets::CloudTypeAsset>( target );
            if ( asset )
                asset->Load(); // overwritten in place: re-read so the cached numbers are the new ones
            else
                asset = m_Assets->CreateAsset<Assets::CloudTypeAsset>( Assets::AssetPriority::Medium, target );

            if ( asset )
            {
                asset->ResolveDependencies( *m_Assets );
                if ( const auto registered = Runtime::ResourceRegistry::GetCloudTypeService()->Register( asset );
                     !registered )
                {
                    m_Status        = "Saved, but the type could not be registered: " + registered.GetError();
                    m_StatusIsError = true;
                    return;
                }
                m_Data = asset->GetData(); // re-read from the file, so the buffer is what is on disk
            }
        }

        m_Status        = "Saved to " + target.string();
        m_StatusIsError = false;
    }
} // namespace Desert::Editor
