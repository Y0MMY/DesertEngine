#include "CloudTypePanel.hpp"

#include <Editor/Core/ImGuiUtilities.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/CloudNoiseVolumeAsset.hpp>
#include <Engine/Assets/CloudProceduralVolume.hpp>
#include <Engine/Assets/CloudTypeAsset.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Graphic/Clouds/CloudTypeShape.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <ImGui/imgui.h>

#include <algorithm>
#include <cmath>
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
        // How many points the READ-BACK preview below the editor is drawn with. It is deliberately far
        // finer than the sixteen the profile is authored at: the preview reads the field the lumps
        // actually make, and the whole question it answers is what the sixteen turned into.
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
        // NO ImGui::Begin HERE, and it is not an omission. EditorLayer's panel loop already wraps
        // OnUIRender in Begin/End for this panel's name, and it owns the p_open bool the title-bar X
        // writes to. A Begin for the SAME name nested inside that one is not appending -- ImGui only
        // supports appending between Begin/End PAIRS -- and the window comes out with its title bar
        // and nothing else. Every panel in this folder had it and drew nothing; no panel outside it
        // does. See CALIBRATION.md §PTP.
        ImGui::TextWrapped(
             "A cloud TYPE is thirteen numbers, a vertical profile curve, and the noise its edge is cut "
             "from. Together they place the pile of lumps this kind of cloud is made of, so a type stays "
             "small, readable and editable - there is no baked texture here to go stale against the "
             "maths." );
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

    void CloudTypePanel::OpenType( const Common::Filepath& path )
    {
        if ( !m_Assets )
            return;

        auto asset = m_Assets->FindByPath<Assets::CloudTypeAsset>( path );
        if ( !asset )
            asset = m_Assets->CreateAsset<Assets::CloudTypeAsset>( Assets::AssetPriority::Medium, path );

        if ( !asset || !asset->IsReadyForUse() )
        {
            m_Status        = "'" + path.string() + "' could not be opened - the log says why.";
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
            ImGui::SetTooltip( "Where this kind of cloud's base sits - its lifting condensation level. "
                               "ABSOLUTE, not a fraction of a layer: the shell the march intersects is "
                               "computed from this and the top, so moving it moves the clouds." );

        ImGui::SliderFloat( "Top Altitude (km)", &s.TopAltitudeKm, 0.0f, 16.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "The top this kind reaches at the CORE of a placement patch." );

        ImGui::SliderFloat( "Edge Top Fraction", &s.EdgeTopFraction, 0.0f, 1.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "How much of its full height it reaches where a patch has only just begun. "
                               "Near 1 is a sheet - the same everywhere. Near 0.1 is a cumulus field: a "
                               "flat pad at the rim of a patch and a tower in its middle." );

        ImGui::SliderFloat( "Base Ramp", &s.BaseRampFraction, 0.001f, 1.0f, "%.3f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "How much of the cloud's own height the base takes to reach full density. "
                               "Small is the flat bottom of a cumulus; as long as the taper it becomes the "
                               "symmetric section of a lenticular." );

        DrawProfileEditor();

        ImGui::SliderFloat( "Anvil Strength", &s.AnvilStrength, 0.0f, 1.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "A SECOND lobe of cloud, above the tower and spreading wider than it, with a "
                               "GAP between the two. Zero means this kind has none. The profile above "
                               "cannot express it and is not meant to: one curve is one connected body, and "
                               "what makes a cumulonimbus recognisable is the gap." );

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
                               "Size. Below 1 gives many small cells - a stratocumulus deck of one-kilometre "
                               "lumps; above 1 gives few large ones - a storm cell, or a sheet that never "
                               "ends. Each kind of cloud in the layer has its own, which is why a low deck "
                               "and a tall tower can be different sizes in the same sky." );

        ImGui::SliderFloat( "Placement Anisotropy", &s.PlacementAnisotropy, 0.1f, 16.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "How much longer the patches are along the wind than across it. 1 is round. "
                               "Above 1 combs them out downwind, which is what makes cirrus fibrous instead "
                               "of blotchy; BELOW 1 stretches them ACROSS the wind, which is what a wave "
                               "cloud is - a lenticular's crest lies perpendicular to the flow." );
    }

    void CloudTypePanel::DrawProfileEditor()
    {
        Utils::ImGuiUtilities::SectionHeader( "Vertical profile" );

        Graphic::CloudVerticalProfile& profile = m_Data.Shape.Profile;

        ImGui::TextWrapped( "Drag inside the box to shape the cloud. Height runs up the box - the bottom "
                            "edge is this type's base altitude and the top edge its top - and the width of "
                            "the silhouette is how wide the cloud is there." );

        // THE SILHOUETTE IS DRAWN MIRRORED AND EDITED ON EITHER SIDE, because that is the thing an artist
        // is thinking about: the outline of a cloud seen from the side. A single-sided plot of half-width
        // against height is the same sixteen numbers and reads as a graph, which is what the panel used to
        // show as a RESULT. This is the input, so it looks like the cloud.
        ImGui::InvisibleButton( "##profileCanvas", ImVec2( ImGui::GetContentRegionAvail().x, 220.0f ) );

        const bool   active = ImGui::IsItemActive();
        const ImVec2 min    = ImGui::GetItemRectMin();
        const ImVec2 max    = ImGui::GetItemRectMax();
        const float  width  = max.x - min.x;
        const float  height = max.y - min.y;
        const float  centre = min.x + width * 0.5f;

        // THE FULL-SCALE HALF-WIDTH THE BOX IS DRAWN AGAINST. It is the validator's ceiling of 4 divided
        // by four rather than the widest sample present: a box that rescaled itself to its own content
        // would make every profile look the same and hide exactly the comparison the artist is making.
        constexpr float kFullScaleHalfWidth = 1.0f;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled( min, max, IM_COL32( 24, 26, 30, 255 ) );

        // The base and top edges, so "which way is up" is not something to remember.
        draw->AddLine( ImVec2( min.x, max.y - 1.0f ), ImVec2( max.x, max.y - 1.0f ),
                       IM_COL32( 90, 96, 110, 255 ) );
        draw->AddLine( ImVec2( min.x, min.y + 1.0f ), ImVec2( max.x, min.y + 1.0f ),
                       IM_COL32( 90, 96, 110, 255 ) );
        draw->AddLine( ImVec2( centre, min.y ), ImVec2( centre, max.y ), IM_COL32( 60, 64, 74, 255 ) );

        constexpr uint32_t kLast = Graphic::kCloudProfileSamples - 1;

        // WHERE SAMPLE `i` SITS IN THE BOX. Sample 0 is the base, so it is at the BOTTOM — the y axis is
        // inverted against ImGui's, which is the one place this widget has to remember that.
        const auto sampleY = [&]( uint32_t i )
        { return max.y - ( static_cast<float>( i ) / static_cast<float>( kLast ) ) * height; };

        const auto sampleHalfPixels = [&]( uint32_t i )
        { return ( profile.HalfWidth[i] / kFullScaleHalfWidth ) * ( width * 0.5f ); };

        // THE BODY AS ONE FILLED SHAPE rather than sixteen bars: the artist is authoring a continuous
        // silhouette and the layout interpolates linearly between samples, so a filled quad strip between
        // consecutive samples is literally what the generator will read.
        for ( uint32_t i = 0; i < kLast; ++i )
        {
            const float lowY  = sampleY( i );
            const float highY = sampleY( i + 1 );
            const float lowW  = sampleHalfPixels( i );
            const float highW = sampleHalfPixels( i + 1 );

            const ImVec2 quad[4] = { ImVec2( centre - lowW, lowY ), ImVec2( centre + lowW, lowY ),
                                     ImVec2( centre + highW, highY ), ImVec2( centre - highW, highY ) };
            draw->AddConvexPolyFilled( quad, 4, IM_COL32( 168, 186, 214, 210 ) );
        }

        for ( uint32_t i = 0; i <= kLast; ++i )
        {
            const float y = sampleY( i );
            const float w = sampleHalfPixels( i );
            draw->AddCircleFilled( ImVec2( centre + w, y ), 2.5f, IM_COL32( 250, 250, 250, 235 ) );
            draw->AddCircleFilled( ImVec2( centre - w, y ), 2.5f, IM_COL32( 250, 250, 250, 120 ) );
        }

        // THE DRAG WRITES EVERY SAMPLE IT CROSSES, not just the nearest one to where the mouse ended up.
        // A mouse moving faster than one sample per frame would otherwise leave gaps in the curve — the
        // same defect a painting brush has when it stamps instead of sweeping, which is why Р1's brush
        // draws a stroke as a capsule rather than a dot. Here the stroke is one-dimensional, so the
        // capsule degenerates to the closed interval between the last position and this one.
        if ( active )
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ImVec2 prev( mouse.x - ImGui::GetIO().MouseDelta.x, mouse.y - ImGui::GetIO().MouseDelta.y );

            const auto sampleAt = [&]( float y )
            {
                const float fraction = std::clamp( ( max.y - y ) / std::max( height, 1.0f ), 0.0f, 1.0f );
                return static_cast<uint32_t>( std::lround( fraction * static_cast<float>( kLast ) ) );
            };

            const uint32_t from = sampleAt( std::max( prev.y, mouse.y ) );
            const uint32_t to   = sampleAt( std::min( prev.y, mouse.y ) );

            const float halfWidth =
                 std::clamp( std::abs( mouse.x - centre ) / std::max( width * 0.5f, 1.0f ), 0.0f, 1.0f ) *
                 kFullScaleHalfWidth;

            for ( uint32_t i = from; i <= to && i <= kLast; ++i )
                profile.HalfWidth[i] = halfWidth;
        }

        // The numbers, because a curve that can only be dragged cannot be typed and an artist reproducing
        // a reference needs to be able to type. Sixteen floats in four rows of four.
        if ( ImGui::TreeNode( "Samples (base first)" ) )
        {
            for ( uint32_t row = 0; row < Graphic::kCloudProfileSamples; row += 4 )
            {
                ImGui::PushID( static_cast<int>( row ) );
                ImGui::SetNextItemWidth( -1.0f );
                ImGui::DragFloat4( "##profileRow", &profile.HalfWidth[row], 0.005f, 0.0f, 4.0f, "%.3f" );
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        // THE PRESETS ARE THE ENGINE'S OWN FUNCTIONS, not a second set of numbers written out here. The
        // tower and the deck are what the delivery frames were shot with and what the tests assert
        // against, so a preset that drifted from them would make the panel and the evidence disagree.
        if ( ImGui::Button( "Flat deck" ) )
            profile = Graphic::CloudProfileFlatDeck();
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "The same width from base to top - a sheet seen edge on." );

        ImGui::SameLine();
        if ( ImGui::Button( "Tower" ) )
            profile = Graphic::CloudProfileTower();
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Pinched at the base, swelling through the upper half. NOT reachable under "
                               "the old Top Taper knob at any setting: that law was a product of two "
                               "falling lines, so it narrowed all the way up whatever it was set to." );

        ImGui::SameLine();
        if ( ImGui::Button( "Classic taper" ) )
            profile = Graphic::CloudProfileDefault();
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "The shape every type had before the curve existed - the built-in "
                               "congestus, which stood at a Top Taper of 0.5." );
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

        // THE PREVIEW IS THE FIELD ITSELF, NOT A CURVE THAT DESCRIBES IT. It used to plot
        // Graphic::CloudProfileCurve, which was the generator of a table the march sampled; there is no
        // table now — the profile is the normalised distance field of a joined pile of lumps
        // (Engine/Assets/CloudProceduralVolume.hpp). So the panel places the lumps this type would place,
        // with the SAME function the bake calls, and reads the field down two vertical lines. "The preview
        // shows what will be baked" is then true by construction rather than by vigilance, which is the
        // property the sculpting panel's slice preview was built for too.
        Assets::CloudProceduralFieldParams params;
        params.LayerBottomKm    = bottomKm;
        params.LayerThicknessKm = spanKm;
        params.Seed             = 1u;
        params.CoverageContrast = 1.0f;
        params.WindAxis         = glm::vec2( 1.0f, 0.0f );

        // A CELL, and the region is a handful of them. The lattice is the component's shipped default —
        // 12 km over four cells — scaled by this type's own Placement Scale, so the preview is the type at
        // the settings a scene gets before anybody touches a slider.
        const float latticeKm = 3.0f * std::max( s.PlacementScale, 1e-3f );

        params.RegionSizeKm      = std::max( latticeKm * 6.0f, 16.1f );
        params.BlendRadiusKm     = std::max( 0.02f * latticeKm, 1e-3f );
        params.ProfileDepthKm    = std::max( 0.12f * latticeKm, 1e-3f );
        params.ResolvableChordKm = Graphic::CloudFinestResolvableChordKm( 256.0f );

        // EVERY CELL ALIVE, because the preview is about the SHAPE of one cloud and not about how many
        // there are. Coverage is a layer setting and it has its own slider in the Details panel.
        params.Coverage = 1.0f;

        Assets::CloudProceduralSpecies species;
        species.Shape      = s;
        species.CellKm     = latticeKm;
        species.Anisotropy = std::max( s.PlacementAnisotropy, 1e-3f );
        params.Species.push_back( species );

        const glm::vec2 origin( 0.0f, 0.0f );

        const std::vector<Assets::CloudModellingBlob> blobs =
             Assets::GenerateCloudProceduralBlobs( params, 0u, origin );

        if ( blobs.empty() )
        {
            ImGui::TextDisabled( "This type places no lumps at all, so there is nothing to draw." );
            return;
        }

        // THE FULLEST CLUSTER IN THE PREVIEW REGION, found by its own lumps rather than chosen: a cell's
        // fullness is a hash, so there is no "the core one" to ask for. The tallest stack is the one an
        // artist means when they ask what this type looks like.
        glm::vec3 tallest = blobs.front().CentreKm;
        for ( const Assets::CloudModellingBlob& blob : blobs )
        {
            if ( blob.CentreKm.y > tallest.y )
                tallest = blob.CentreKm;
        }

        // How far to step sideways for the second curve: past the lumps of this cluster, into the skirt.
        float widest = 0.0f;
        for ( const Assets::CloudModellingBlob& blob : blobs )
        {
            if ( std::abs( blob.CentreKm.y - tallest.y ) < spanKm )
                widest = std::max( widest, blob.RadiiKm.x );
        }

        m_ProfileEdge.resize( kPreviewSamples );
        m_ProfileCore.resize( kPreviewSamples );

        for ( int i = 0; i < kPreviewSamples; ++i )
        {
            const float fraction   = ( static_cast<float>( i ) + 0.5f ) / static_cast<float>( kPreviewSamples );
            const float altitudeKm = bottomKm + fraction * spanKm;

            m_ProfileCore[i] = Assets::EvaluateCloudProceduralProfile(
                 params, blobs, glm::vec3( tallest.x, altitudeKm, tallest.z ) );
            m_ProfileEdge[i] = Assets::EvaluateCloudProceduralProfile(
                 params, blobs, glm::vec3( tallest.x + widest, altitudeKm, tallest.z ) );
        }

        ImGui::PlotLines( "##profileCore", m_ProfileCore.data(), kPreviewSamples, 0, "through the core", 0.0f,
                          1.0f, ImVec2( -1.0f, 90.0f ) );
        ImGui::PlotLines( "##profileEdge", m_ProfileEdge.data(), kPreviewSamples, 0, "through the flank", 0.0f,
                          1.0f, ImVec2( -1.0f, 90.0f ) );

        ImGui::TextDisabled( "Left is the base at %.2f km, right is the top of the shell at %.2f km "
                             "(%.2f km thick), on a %.2f km lattice.",
                             bottomKm, topKm, spanKm, latticeKm );
        ImGui::TextDisabled( "TWO LINES THROUGH ONE CLOUD: the profile is a distance field now, so the "
                             "core reaches 1 and the flank is whatever the lumps out there leave." );
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
