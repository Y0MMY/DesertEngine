#include "CloudLayoutPanel.hpp"

#include <Editor/Core/DragPayloads.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/CloudLayoutAsset.hpp>
#include <Engine/Assets/CloudTypeAsset.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/VolumetricCloudComponent.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/CloudType/CloudTypeService.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <ImGui/imgui.h>

#include <stb_image/stb_image.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>

namespace Desert::Editor
{
    // The editor's ImGui lives in the global namespace; unqualified `ImGui::` inside `Desert::` would
    // resolve to `Desert::ImGui`, which is the engine's own runtime UI. Every panel in this folder opens
    // with the same alias for the same reason.
    namespace ImGui = ::ImGui;

    namespace
    {
        /// The panel's own UIHelper, created on first use exactly as the noise volume panel's is: the
        /// helper caches ImGui texture ids by image view, and a panel that never opens should not build
        /// one.
        std::unique_ptr<UI::UIHelper>& LayoutPreviewHelper()
        {
            static std::unique_ptr<UI::UIHelper> helper;
            if ( !helper )
            {
                helper = std::make_unique<UI::UIHelper>();
                helper->Init();
            }
            return helper;
        }

        bool LooksLikeAnImage( const std::filesystem::path& path )
        {
            std::string extension = path.extension().string();
            std::transform( extension.begin(), extension.end(), extension.begin(),
                            []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

            return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
                   extension == ".bmp";
        }

        const ImVec4 kErrorColour( 0.95f, 0.45f, 0.40f, 1.0f );
        const ImVec4 kWarnColour( 0.95f, 0.78f, 0.35f, 1.0f );
        const ImVec4 kGoodColour( 0.55f, 0.85f, 0.55f, 1.0f );
    } // namespace

    CloudLayoutPanel::CloudLayoutPanel( std::shared_ptr<::Desert::Core::Scene> scene,
                                        Assets::AssetManager*                  assets )
         : IPanel( "Cloud Layout", /*showPanel=*/false ), m_Scene( std::move( scene ) ), m_Assets( assets )
    {
    }

    // -------------------------------------------------------------------------------------------------
    // The layer
    // -------------------------------------------------------------------------------------------------

    bool CloudLayoutPanel::LayerContext::Matches( const LayerContext& other ) const
    {
        if ( FromScene != other.FromScene || RegionSizeKm != other.RegionSizeKm || LatticeKm != other.LatticeKm ||
             PatchTileKm != other.PatchTileKm || Coverage != other.Coverage ||
             PatchStrength != other.PatchStrength || ResolvableChordKm != other.ResolvableChordKm ||
             Seed != other.Seed || SpeciesCount != other.SpeciesCount )
            return false;

        // THE SPECIES ARE COMPARED TOO, and they are the ones that moved most often in practice: dropping a
        // .decloudtype into a free slot renumbers every channel after it, and a map that did not redraw
        // would go on describing the sky the layer had a type ago.
        for ( uint32_t species = 0; species < SpeciesCount; ++species )
        {
            if ( Species[species].AuthoredSlot != other.Species[species].AuthoredSlot ||
                 Species[species].BuiltIn != other.Species[species].BuiltIn ||
                 Species[species].Scale != other.Species[species].Scale ||
                 Species[species].Anisotropy != other.Species[species].Anisotropy )
                return false;
        }

        return Assets::CloudLayoutPlacementEqual( Placement, other.Placement );
    }

    CloudLayoutPanel::LayerContext CloudLayoutPanel::ReadLayer() const
    {
        LayerContext layer;

        // THE NO-LAYER ANSWER IS FILLED IN, not left blank. With no cloud component in the scene the panel
        // still draws a map, against the shipped defaults, and the one species it has must have a NAME —
        // otherwise the channel labels read "-> " and the artist is told nothing by a control that is
        // trying to tell them everything.
        layer.SpeciesCount        = 1u;
        layer.Species[0].BuiltIn  = true;
        layer.Species[0].TypeName = "the built-in cumulus congestus";

        if ( !m_Scene )
            return layer;

        auto view = m_Scene->GetRegistry().view<ECS::VolumetricCloudComponent>();
        if ( view.begin() == view.end() )
            return layer;

        const ECS::VolumetricCloudData& data = view.get<ECS::VolumetricCloudComponent>( *view.begin() ).Data;

        layer.FromScene     = true;
        layer.RegionSizeKm  = std::max( data.RegionSize, 1.0f ) / Graphic::kCloudWorldUnitsPerKm;
        layer.Coverage      = std::clamp( data.Coverage, 0.0f, 1.0f );
        layer.PatchStrength = std::clamp( data.PatchStrength, 0.0f, 1.0f );
        layer.Seed          = static_cast<uint32_t>( std::max( data.Seed, 0 ) );
        layer.ResolvableChordKm =
             Graphic::CloudFinestResolvableChordKm( static_cast<float>( std::clamp( data.MaxSteps, 8, 512 ) ) );

        layer.LatticeKm = ECS::CloudLayerLatticeKm( data );

        // THE LAYER'S OWN WEATHER PATCH TILE, and not a constant. It used to be a hard-coded 21 km here,
        // which is the component's DEFAULT — so the map agreed with the sky in every scene that had never
        // touched the field and disagreed silently in every scene that had. The patch is what decides a
        // cell's coverage whenever the painting is not the source (no layout bound, or Layout Pattern
        // Strength at zero), so getting it wrong draws a plausible sky that is not this layer's.
        layer.PatchTileKm = std::max( data.PatchTileSize, 1.0f ) / Graphic::kCloudWorldUnitsPerKm;

        // WHICH SLOT IS WHICH SPECIES IS ASKED OF THE ENGINE, never worked out here — the renderer resolves
        // the same call, so a channel this panel labels "Cirrus" is the channel the sky gives to cirrus.
        const ECS::CloudSpeciesResolution resolved = ECS::ResolveCloudSpecies( data );
        layer.SpeciesCount                         = resolved.Count;

        auto* types = Runtime::ResourceRegistry::GetCloudTypeService();

        const Assets::AssetHandle authored[ECS::kCloudTypeSlots] = { data.CloudType1, data.CloudType2,
                                                                     data.CloudType3, data.CloudType4 };

        for ( uint32_t species = 0; species < resolved.Count; ++species )
        {
            LayerContext::SpeciesSlot& slot = layer.Species[species];

            slot.BuiltIn      = resolved.BuiltInDefault;
            slot.AuthoredSlot = resolved.AuthoredSlot[species];

            const Assets::AssetHandle handle =
                 resolved.BuiltInDefault ? Assets::AssetHandle::Null() : authored[slot.AuthoredSlot];

            if ( types )
            {
                const Graphic::CloudTypeShape& shape = types->GetShape( handle );
                slot.Scale                           = std::max( shape.PlacementScale, 1e-3f );
                slot.Anisotropy                      = std::max( shape.PlacementAnisotropy, 1e-3f );
            }

            if ( slot.BuiltIn )
                slot.TypeName = "the built-in cumulus congestus";
            else if ( m_Assets )
            {
                if ( auto type = m_Assets->FindByHandle<Assets::CloudTypeAsset>( handle ) )
                    slot.TypeName = type->GetMetadata().Filepath.stem().string();
            }

            // NAMED BY ITS HANDLE WHEN NOTHING CLAIMS IT, rather than left blank: a slot whose type the
            // asset manager does not know is a scene pointing at a file that is not there, and a blank
            // label would read as an empty slot — which is the one thing it is not.
            if ( slot.TypeName.empty() )
                slot.TypeName = "an unregistered type " + std::to_string( static_cast<uint64_t>( handle ) );
        }

        layer.Placement.RepeatsPerRegion = static_cast<uint32_t>( std::clamp( data.LayoutRepeats, 1, 16 ) );
        layer.Placement.QuarterTurns     = static_cast<uint32_t>( std::clamp( data.LayoutRotation, 0, 3 ) );
        layer.Placement.OffsetKm =
             glm::vec2( data.LayoutOffset.x, data.LayoutOffset.y ) / Graphic::kCloudWorldUnitsPerKm;
        layer.Placement.PatternStrength = std::clamp( data.LayoutPatternStrength, 0.0f, 1.0f );
        layer.Placement.MaskStrength    = std::clamp( data.LayoutMaskStrength, 0.0f, 1.0f );

        layer.BoundLayout = static_cast<uint64_t>( data.CloudLayout );

        return layer;
    }

    void CloudLayoutPanel::OnUIRender()
    {
        // NO ImGui::Begin HERE, and it is not an omission. EditorLayer's panel loop already wraps
        // OnUIRender in Begin/End for this panel's name, and it owns the p_open bool the title-bar X
        // writes to. A Begin for the SAME name nested inside that one is not appending -- ImGui only
        // supports appending between Begin/End PAIRS -- and the window comes out with its title bar
        // and nothing else. Every panel in this folder had it and drew nothing; no panel outside it
        // does. See CALIBRATION.md §PTP.
        const LayerContext layer = ReadLayer();

        // THE LAYER IS COMPARED FIELD BY FIELD rather than by a dirty flag somebody has to set: the
        // numbers live on a component the Details panel edits, and there is no notification. Comparing is
        // a few dozen floats a frame and it is what makes moving Layout Repeats in Details redraw this map.
        if ( !layer.Matches( m_LastLayer ) )
            m_PreviewDirty = true;
        m_LastLayer = layer;

        // A SLOT THIS LAYER DOES NOT HAVE CANNOT BE MAPPED. Dropping a type out of Details shortens the
        // species list under the panel's feet, and BuildCloudLayoutPreview refuses a slot past the end —
        // correctly, and as an error the artist would have to read rather than a map they could look at.
        if ( m_PreviewSlot >= static_cast<int>( layer.SpeciesCount ) )
        {
            m_PreviewSlot  = static_cast<int>( layer.SpeciesCount ) - 1;
            m_PreviewDirty = true;
        }

        // THE PANEL OPENS ON THE SKY YOU ARE LOOKING AT. A layer with a painting bound hands it to the
        // panel once, when it changes — once and not every frame, so that an artist who then opens a
        // different picture is not overwritten by the scene on the next frame.
        if ( layer.BoundLayout != m_AdoptedLayout )
        {
            m_AdoptedLayout = layer.BoundLayout;
            if ( layer.BoundLayout != 0u && m_Assets )
            {
                if ( auto painting =
                          m_Assets->FindByHandle<Assets::CloudLayoutAsset>( Common::UUID( layer.BoundLayout ) );
                     painting && painting->IsReadyForUse() )
                {
                    m_Layout    = painting->GetLayout();
                    m_HasLayout = true;
                    m_SourcePixels.clear();
                    m_SourceWidth    = 0u;
                    m_SourceHeight   = 0u;
                    m_SourceName     = painting->GetMetadata().Filepath.filename().string();
                    m_LayoutFromFile = true;
                    m_PreviewDirty   = true;
                    m_Status         = "Showing the painting this scene's cloud layer has bound. Point at a "
                                       "picture to author a new one.";
                    m_StatusIsError  = false;
                }
            }
        }

        ImGui::TextWrapped( "A PAINTED SKY. Point at a picture, say which of its channels feeds which of "
                            "this layer's cloud species, and bake it to a .dclayout the cloud component's "
                            "Cloud Layout slot accepts. The painting is read when the clouds are PLACED, "
                            "so it costs the frame nothing." );
        ImGui::Separator();

        DrawSourceSection();
        ImGui::Separator();
        DrawChannelSection();
        ImGui::Separator();
        DrawLayerSection();
        ImGui::Separator();

        if ( m_PreviewDirty )
            RefreshPreview( layer );

        DrawPreviewSection();
        ImGui::Separator();
        DrawVerdictSection();
        ImGui::Separator();
        DrawSaveSection();

        if ( !m_Status.empty() )
        {
            ImGui::Separator();
            if ( m_StatusIsError )
                ImGui::TextColored( kErrorColour, "%s", m_Status.c_str() );
            else
                ImGui::TextWrapped( "%s", m_Status.c_str() );
        }
    }

    // -------------------------------------------------------------------------------------------------
    // Source
    // -------------------------------------------------------------------------------------------------

    void CloudLayoutPanel::LoadImage( const std::filesystem::path& path )
    {
        int      width = 0, height = 0, sourceChannels = 0;
        stbi_uc* decoded = stbi_load( path.string().c_str(), &width, &height, &sourceChannels, 4 );
        if ( !decoded )
        {
            // NAMED, WITH THE REASON stb gives. A picture that silently failed to load would leave the
            // previous painting on screen and the artist would bake the wrong file.
            m_Status = "'" + path.filename().string() + "' could not be read as an image: " +
                       ( stbi_failure_reason() ? stbi_failure_reason() : "unknown" );
            m_StatusIsError = true;
            LOG_ERROR( "[CloudLayout] {}", m_Status );
            return;
        }

        m_SourcePixels.assign( decoded, decoded + static_cast<size_t>( width ) * height * 4 );
        stbi_image_free( decoded );

        m_SourceWidth    = static_cast<uint32_t>( width );
        m_SourceHeight   = static_cast<uint32_t>( height );
        m_SourceName     = path.filename().string();
        m_LayoutFromFile = false;

        m_Status = "Read '" + m_SourceName + "': " + std::to_string( width ) + "x" + std::to_string( height ) +
                   ", " + std::to_string( sourceChannels ) + " channels in the file.";
        m_StatusIsError = false;

        RebuildLayout();
    }

    void CloudLayoutPanel::RebuildLayout()
    {
        m_HasLayout = false;

        if ( m_SourcePixels.empty() )
            return;

        // THE TOOL'S OWN FUNCTION, and this is the line that makes the panel not a second path: what a
        // picture means is stated once, in Engine/Assets/CloudLayout.cpp, and Tools/CloudLayoutBaker calls
        // the same one. A panel with its own reading of a channel mapping would produce files that differ
        // from the tool's for reasons nobody could see.
        auto made = Assets::MakeCloudLayoutFromImage( m_SourcePixels, m_SourceWidth, m_SourceHeight,
                                                      m_ChannelForSlot, m_TakeMask );
        if ( !made )
        {
            m_Status        = made.GetError();
            m_StatusIsError = true;
            return;
        }

        m_Layout        = made.ExtractValue();
        m_HasLayout     = true;
        m_PreviewDirty  = true;
        m_StatusIsError = false;
    }

    void CloudLayoutPanel::DrawSourceSection()
    {
        if ( !Utils::ImGuiUtilities::SectionHeader( "Source" ) )
            return;

        if ( ImGui::Button( "Image...", ImVec2( 120.0f, 0.0f ) ) )
        {
            const std::filesystem::path picked =
                 Common::Utils::FileSystem::OpenFileDialog( "Image\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0" );
            if ( !picked.empty() )
                LoadImage( picked );
        }

        // The same generic Content Browser payload the Details slot accepts, filtered HERE by extension
        // for the same reason it filters there: the browser emits one AssetFile payload for everything it
        // has no icon for, so without this the panel would hand a dropped .desce to an image decoder.
        if ( ImGui::BeginDragDropTarget() )
        {
            if ( const ImGuiPayload* payload =
                      ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::AssetFile ) )
            {
                const std::string dropped( static_cast<const char*>( payload->Data ),
                                           payload->DataSize > 0 ? payload->DataSize - 1 : 0 );
                if ( !dropped.empty() && LooksLikeAnImage( dropped ) )
                    LoadImage( dropped );
            }
            ImGui::EndDragDropTarget();
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "A SQUARE picture, 4 to 1024 a side. Non-square and oversized sources are "
                               "refused by name rather than resampled - resampling would be an opinion "
                               "about your painting, and one taken silently is the worst kind." );

        ImGui::SameLine();
        ImGui::TextDisabled( "or drag a .png here from the Content Browser" );

        // Opening a finished painting is the other half of the tool: it is how an artist sees what a
        // shipped layout does to their sky before binding it, and how they check one somebody else baked.
        if ( m_Assets )
        {
            const auto paintings = m_Assets->FindAllByType<Assets::CloudLayoutAsset>();
            ImGui::BeginDisabled( paintings.empty() );
            if ( ImGui::BeginCombo( "Open", paintings.empty() ? "(no paintings on disk)" : "Pick a .dclayout" ) )
            {
                for ( const auto& [handle, painting] : paintings )
                {
                    const std::string name = painting->GetMetadata().Filepath.filename().string();
                    if ( ImGui::Selectable( name.c_str() ) && painting->IsReadyForUse() )
                    {
                        m_Layout    = painting->GetLayout();
                        m_HasLayout = true;
                        m_SourcePixels.clear();
                        m_SourceWidth    = 0u;
                        m_SourceHeight   = 0u;
                        m_SourceName     = name;
                        m_LayoutFromFile = true;
                        m_PreviewDirty   = true;
                        m_Status         = "Opened '" + name +
                                   "'. It has no source picture here, so the channel mapping below is "
                                   "what it was baked with and cannot be changed - point at the picture "
                                   "again to re-map it.";
                        m_StatusIsError = false;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        }

        if ( m_HasLayout )
        {
            ImGui::Text( "%s - %ux%u, pattern %s, mask %s, content %08x", m_SourceName.c_str(),
                         m_Layout.Resolution, m_Layout.Resolution, m_Layout.HasPattern() ? "yes" : "no",
                         m_Layout.HasMask() ? "yes" : "no", m_Layout.ContentHash );
            ImGui::TextDisabled( "channel means %.4f %.4f %.4f %.4f", m_Layout.PatternMean[0],
                                 m_Layout.PatternMean[1], m_Layout.PatternMean[2], m_Layout.PatternMean[3] );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "The painting is applied about its OWN average, so it moves cloud "
                                   "around the sky rather than adding it and Coverage keeps meaning the "
                                   "fraction of sky it delivers. A mean near 0 or near 1 leaves very "
                                   "little room to redistribute anything." );
        }
        else
        {
            ImGui::TextDisabled( "Nothing loaded yet." );
        }
    }

    // -------------------------------------------------------------------------------------------------
    // Channels — the convention, made visible
    // -------------------------------------------------------------------------------------------------

    void CloudLayoutPanel::DrawChannelSection()
    {
        if ( !Utils::ImGuiUtilities::SectionHeader( "Channels" ) )
            return;

        ImGui::TextWrapped( "Which channel of the picture feeds which of the layer's cloud SPECIES. A "
                            "species' channel is its own field of 'is there cloud of this kind here' - "
                            "they overlap freely and the sky takes whichever wins." );

        // THE RENUMBERING, SAID OUT LOUD. Details has four type slots; the layer has as many SPECIES as it
        // has distinct filled ones, and the painting's channels are indexed by species. So a layer whose
        // only type sits in Cloud Type 3 is driven by the painting's RED channel, and nothing in Details
        // hints at it — the symptom is a channel an artist swears they painted that does nothing.
        const LayerContext& layer = m_LastLayer;

        ImGui::BeginDisabled( m_LayoutFromFile || m_SourcePixels.empty() );

        bool remap = false;
        for ( uint32_t slot = 0; slot < Assets::kCloudLayoutChannels; ++slot )
        {
            std::string label;
            if ( slot < layer.SpeciesCount )
            {
                const LayerContext::SpeciesSlot& species = layer.Species[slot];
                label                                    = "-> " + species.TypeName +
                        ( species.BuiltIn ? std::string()
                                          : " (Cloud Type " + std::to_string( species.AuthoredSlot + 1u ) + ")" );
            }
            else
            {
                label = "-> nothing in this scene";
            }

            // The visible text is a TYPE NAME and two slots can hold the same words, so the widget's
            // identity is the slot number after `##` rather than the label. Without it two channels
            // mapped to one unfilled slot would be one control wearing two labels.
            label += "##slot" + std::to_string( slot );

            // A THIRD OF THE ROW FOR THE COMBO, because the LABEL is the part that carries the answer here
            // and ImGui draws it after the widget. At the default full width the type name ran off the
            // panel's right edge and read "(Cloud Type" — found by looking at the panel, which is the only
            // way this class of defect is ever found.
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.33f );

            int channel = static_cast<int>( m_ChannelForSlot[slot] );
            if ( ImGui::Combo( label.c_str(), &channel, "Red\0Green\0Blue\0Alpha\0" ) )
            {
                m_ChannelForSlot[slot] = static_cast<uint32_t>( channel );
                remap                  = true;
            }
            if ( ImGui::IsItemHovered() )
            {
                if ( slot < layer.SpeciesCount )
                    ImGui::SetTooltip( "Species %u of this layer. Details calls it Cloud Type %u; the "
                                       "painting calls it channel %u, because empty and repeated type "
                                       "slots are dropped before the sky is placed.",
                                       slot, layer.Species[slot].AuthoredSlot + 1u, slot );
                else
                    ImGui::SetTooltip( "This layer has %u species, so channel %u is carried in the file and "
                                       "placed by nothing. Fill another Cloud Type slot in Details and it "
                                       "comes alive - the painting does not have to be baked again.",
                                       layer.SpeciesCount, slot );
            }
        }

        if ( ImGui::Button( "One drawing on every channel" ) )
        {
            for ( uint32_t slot = 0; slot < Assets::kCloudLayoutChannels; ++slot )
                m_ChannelForSlot[slot] = 0u;
            remap = true;
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Red into all four channels - what a greyscale painting usually wants, and "
                               "the tool's `--channels 0,0,0,0`." );

        ImGui::SameLine();
        if ( ImGui::Button( "Straight RGBA" ) )
        {
            for ( uint32_t slot = 0; slot < Assets::kCloudLayoutChannels; ++slot )
                m_ChannelForSlot[slot] = slot;
            remap = true;
        }

        if ( ImGui::Checkbox( "Take the alpha as the add/remove mask", &m_TakeMask ) )
            remap = true;
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "OFF by default, and not because a mask is unusual: an opaque PNG has "
                               "alpha 255 everywhere, and under the signed convention (mid-grey neutral, "
                               "brighter adds, darker removes) that would be a mask adding cloud to the "
                               "whole sky. Turn it on when your alpha means something." );

        ImGui::EndDisabled();

        if ( m_LayoutFromFile )
            ImGui::TextDisabled( "Opened from a .dclayout - the mapping is already baked into its pixels." );

        if ( remap )
            RebuildLayout();
    }

    // -------------------------------------------------------------------------------------------------
    // The layer, READ and not edited
    // -------------------------------------------------------------------------------------------------

    void CloudLayoutPanel::DrawLayerSection()
    {
        const LayerContext& layer = m_LastLayer;

        // THE TWO THINGS THAT MUST NOT FOLD AWAY ARE DRAWN OUTSIDE THE SECTION. A map built against the
        // shipped defaults instead of the artist's layer, and a placement the bake will refuse, are both
        // reasons not to trust the picture below — and a reason not to trust a picture that is only
        // visible when a header happens to be open is a reason nobody reads.
        if ( !layer.FromScene )
            ImGui::TextColored( kWarnColour, "This scene has NO cloud layer, so the map below is drawn "
                                             "against the shipped defaults." );

        // The SAME validator the bake runs, so the panel refuses for the same reason the bake refuses
        // rather than the two disagreeing about what is legal.
        if ( const auto valid = Assets::ValidateCloudLayoutPlacement( layer.Placement ); !valid )
            ImGui::TextColored( kErrorColour, "%s", valid.GetError().c_str() );

        // CLOSED UNTIL ASKED FOR, and the reason is a screenshot: with it open the verdict under the two
        // panes — the part of this panel an artist opens it for — fell below the window's bottom edge on a
        // 1289-point display. These numbers are reference and the map is the tool, so the reference is the
        // one that folds away. Nothing here is editable, so nothing is hidden that could be changed by
        // accident.
        if ( !Utils::ImGuiUtilities::SectionHeader( "The layer this hangs in", /*defaultOpen=*/false ) )
            return;

        if ( layer.FromScene )
            ImGui::TextWrapped( "Read from this scene's cloud layer. Change any of it in Details and the "
                                "map below follows on the same frame - there is deliberately no copy of "
                                "these numbers here." );

        ImGui::Text( "Region Size %.1f km      placement lattice %.2f km      weather patch tile %.1f km",
                     layer.RegionSizeKm, layer.LatticeKm, layer.PatchTileKm );
        ImGui::Text( "Coverage %.2f      Weather Patch Strength %.2f      Seed %u", layer.Coverage,
                     layer.PatchStrength, layer.Seed );

        // THE CELL PER SPECIES, because the lattice above is the layer's and every type multiplies it by
        // its own Placement Scale. A stroke has to clear the cell of the species it is painted for, and
        // these are the numbers that differ between them.
        for ( uint32_t species = 0; species < layer.SpeciesCount; ++species )
        {
            ImGui::Text( "  channel %u -> %s: cell %.2f km", species, layer.Species[species].TypeName.c_str(),
                         layer.LatticeKm * layer.Species[species].Scale );
            if ( layer.Species[species].Anisotropy != 1.0f )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "stretched %.2fx along the wind", layer.Species[species].Anisotropy );
            }
        }
        ImGui::Text( "Layout Repeats %u      Rotation %u quarter turns      Offset (%.2f, %.2f) km",
                     layer.Placement.RepeatsPerRegion, layer.Placement.QuarterTurns, layer.Placement.OffsetKm.x,
                     layer.Placement.OffsetKm.y );
        ImGui::Text( "Layout Pattern Strength %.2f      Layout Mask Strength %.2f",
                     layer.Placement.PatternStrength, layer.Placement.MaskStrength );
    }

    // -------------------------------------------------------------------------------------------------
    // Preview
    // -------------------------------------------------------------------------------------------------

    Assets::CloudProceduralFieldParams CloudLayoutPanel::BuildParams( const LayerContext& layer ) const
    {
        Assets::CloudProceduralFieldParams params;

        params.RegionSizeKm      = std::max( layer.RegionSizeKm, 1e-3f );
        params.Coverage          = layer.Coverage;
        params.Seed              = layer.Seed;
        params.PatchStrength     = layer.PatchStrength;
        params.ResolvableChordKm = layer.ResolvableChordKm;
        params.LayoutPlacement   = layer.Placement;

        if ( m_HasLayout )
            params.Layout = std::make_shared<const Assets::CloudLayoutData>( m_Layout );

        // EACH SPECIES ON ITS OWN LATTICE, exactly as VolumetricCloudRenderer builds them: the layer's
        // lattice times the type's Placement Scale, stretched by its Placement Anisotropy. The first draft
        // gave all four one square cell taken from the FINEST type, which draws a coarse species' map at a
        // resolution it does not have and quotes the most permissive legibility bound in the layer for
        // every channel — the artist is then told a 1.2 km stroke is safe on a 4 km cell.
        params.Species.resize( layer.SpeciesCount );
        for ( uint32_t species = 0; species < layer.SpeciesCount; ++species )
        {
            params.Species[species].CellKm     = std::max( layer.LatticeKm * layer.Species[species].Scale, 1e-3f );
            params.Species[species].Anisotropy = std::max( layer.Species[species].Anisotropy, 1e-3f );
        }

        // THE PATCH TILE IS THE LAYER'S OWN, THEN FLOORED AGAINST THE CELL exactly as the renderer floors
        // it, because a modulation finer than three cells decides cells one at a time and reads as a
        // checkerboard. A preview drawn with an unfloored tile would show a sky the layer cannot produce.
        params.PatchTileKm = layer.PatchTileKm;
        for ( const Assets::CloudProceduralSpecies& species : params.Species )
        {
            const glm::vec2 extent = Assets::CloudProceduralCellExtentKm( params, species );
            params.PatchTileKm     = std::max( params.PatchTileKm, 3.0f * std::max( extent.x, extent.y ) );
        }

        return params;
    }

    void CloudLayoutPanel::RefreshPreview( const LayerContext& layer )
    {
        m_PreviewDirty = false;
        m_HasPreview   = false;
        m_PaintingImage.reset();
        m_SkyImage.reset();
        m_Strokes = Assets::CloudLayoutStrokeStats{};

        if ( !m_HasLayout )
            return;

        const Assets::CloudProceduralFieldParams params = BuildParams( layer );
        const uint32_t                           slot =
             static_cast<uint32_t>( std::clamp( m_PreviewSlot, 0, static_cast<int>( layer.SpeciesCount ) - 1 ) );

        const float spanKm = params.RegionSizeKm * static_cast<float>( std::clamp( m_SpanRegions, 1, 4 ) );

        // 256 cells a side is the ceiling: it is about what the pane is drawn at, so a finer map could
        // not be seen, and it bounds the work behind a slider at 65 536 evaluations whatever the cell.
        auto mapped = Assets::BuildCloudLayoutPreview( params, slot, spanKm, 256u );
        if ( !mapped )
        {
            m_Status        = "The sky preview could not be built: " + mapped.GetError();
            m_StatusIsError = true;
            return;
        }

        m_Preview    = mapped.ExtractValue();
        m_HasPreview = true;

        // THE STROKE LIMIT IS THE CELL EXPRESSED IN TEXELS OF THIS PAINTING. One texel spans
        // `period / resolution` kilometres with `period = RegionSize / Repeats`, so a cell is
        // `cell / texelKm` texels wide. That conversion is what turns a fact about pixels into a fact
        // about the sky, and it is the whole reason the measure takes a limit rather than a verdict.
        m_PeriodKm = params.RegionSizeKm / static_cast<float>( params.LayoutPlacement.RepeatsPerRegion );
        m_TexelKm  = m_PeriodKm / static_cast<float>( std::max( m_Layout.Resolution, 1u ) );

        const float limitTexels = m_TexelKm > 0.0f ? m_Preview.CellKm / m_TexelKm : 0.0f;

        m_Strokes = Assets::MeasureCloudLayoutStrokes( m_Layout, slot, limitTexels );

        // ---- the painting pane -------------------------------------------------------------------

        const uint32_t             resolution = m_Layout.Resolution;
        std::vector<unsigned char> painting( static_cast<size_t>( resolution ) * resolution * 4u, 255u );

        const bool wantsMask = m_PaintingView == PaintingView::Mask;
        for ( uint32_t y = 0; y < resolution; ++y )
        {
            for ( uint32_t x = 0; x < resolution; ++x )
            {
                const size_t texel  = static_cast<size_t>( y ) * resolution + x;
                const size_t target = texel * 4u;

                unsigned char value = 0u;
                if ( wantsMask )
                    value = m_Layout.HasMask() ? m_Layout.Mask[texel] : 128u;
                else if ( m_Layout.HasPattern() )
                    value = m_Layout.Pattern[texel * 4u + static_cast<size_t>( m_PaintingView )];

                painting[target + 0] = value;
                painting[target + 1] = value;
                painting[target + 2] = value;
                // The alpha is folded in rather than left to the compositor: an image drawn with a real
                // alpha would show the window behind it, which reads as the painting being empty exactly
                // where it is darkest.
                painting[target + 3] = 255u;
            }
        }

        {
            const Core::Formats::Image2DSpecification spec{
                 .Tag        = "CloudLayoutPainting",
                 .Width      = resolution,
                 .Height     = resolution,
                 .Format     = Core::Formats::ImageFormat::RGBA8F,
                 .Mips       = 1,
                 .Data       = std::move( painting ),
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Sample,
            };
            m_PaintingImage = Graphic::Image2D::Create( spec, nullptr );
        }

        // ---- the sky pane ------------------------------------------------------------------------

        const uint32_t             side = m_Preview.Side;
        std::vector<unsigned char> sky( static_cast<size_t>( side ) * side * 4u, 255u );

        for ( uint32_t iv = 0; iv < side; ++iv )
        {
            for ( uint32_t iu = 0; iu < side; ++iu )
            {
                const float coverage = m_Preview.Coverage[static_cast<size_t>( iv ) * side + iu];

                // NORTH IS UP. The map's v runs north and an image's first row is its top, so the row is
                // flipped here — otherwise the picture would be the sky seen from BELOW, which is the one
                // orientation an artist cannot check against a top-down frame.
                const size_t target = ( static_cast<size_t>( side - 1u - iv ) * side + iu ) * 4u;

                // Clear sky to cloud, so an empty region reads as sky rather than as black. The ramp is
                // linear in coverage because coverage is a FRACTION OF SKY (decision D-20) and a gamma on
                // it would misreport how much cloud a region has.
                const float t = std::clamp( coverage, 0.0f, 1.0f );

                sky[target + 0] = static_cast<unsigned char>( ( 0.24f + 0.74f * t ) * 255.0f + 0.5f );
                sky[target + 1] = static_cast<unsigned char>( ( 0.43f + 0.55f * t ) * 255.0f + 0.5f );
                sky[target + 2] = static_cast<unsigned char>( ( 0.74f + 0.26f * t ) * 255.0f + 0.5f );
                sky[target + 3] = 255u;
            }
        }

        {
            const Core::Formats::Image2DSpecification spec{
                 .Tag        = "CloudLayoutSky",
                 .Width      = side,
                 .Height     = side,
                 .Format     = Core::Formats::ImageFormat::RGBA8F,
                 .Mips       = 1,
                 .Data       = std::move( sky ),
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Sample,
            };
            m_SkyImage = Graphic::Image2D::Create( spec, nullptr );
        }

        if ( !m_PaintingImage || !m_SkyImage )
        {
            m_Status        = "A preview image could not be created on the device.";
            m_StatusIsError = true;
        }
    }

    void CloudLayoutPanel::DrawPreviewSection()
    {
        if ( !Utils::ImGuiUtilities::SectionHeader( "Preview" ) )
            return;

        if ( !m_HasLayout )
        {
            ImGui::TextDisabled( "Load a picture, or open a .dclayout, to see what it does to a sky." );
            return;
        }

        // BOUNDED BY THE SPECIES THIS LAYER HAS, not by the four a file can carry: a slider that offers a
        // slot the layer cannot place would answer with an error message where a map should be.
        const int lastSlot = static_cast<int>( m_LastLayer.SpeciesCount ) - 1;
        ImGui::BeginDisabled( lastSlot == 0 );
        if ( ImGui::SliderInt( "Channel to map", &m_PreviewSlot, 0, lastSlot, "channel %d" ) )
        {
            m_PaintingView = static_cast<PaintingView>( m_PreviewSlot );
            m_PreviewDirty = true;
        }
        ImGui::EndDisabled();
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Which of this layer's %u species to map - %s. Each is its own channel of "
                               "the painting, its own lattice and its own field of clouds.",
                               m_LastLayer.SpeciesCount,
                               m_LastLayer.Species[std::clamp( m_PreviewSlot, 0, lastSlot )].TypeName.c_str() );

        int view = static_cast<int>( m_PaintingView );
        if ( ImGui::Combo( "Painting shows", &view,
                           "Channel 0\0"
                           "Channel 1\0"
                           "Channel 2\0"
                           "Channel 3\0"
                           "The add/remove mask\0" ) )
        {
            m_PaintingView = static_cast<PaintingView>( view );
            m_PreviewDirty = true;
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Which table of the FILE the left pane draws. All four channels are carried "
                               "whatever the layer uses them for, so a channel this scene has no species "
                               "for can still be inspected here." );

        if ( ImGui::SliderInt( "Sky span", &m_SpanRegions, 1, 4, "%d regions" ) )
            m_PreviewDirty = true;
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "How much world the right-hand map covers, in region periods. At 1 you see "
                               "one period of the painting; above 1 you see it TILE, which is what the sky "
                               "does past the region and what the whole-number repeat count protects." );

        ImGui::SliderInt( "Pane size", &m_PreviewSide, 128, 512, "%d px" );

        const float pane = static_cast<float>( m_PreviewSide );

        if ( m_PaintingImage )
        {
            ImGui::BeginGroup();
            ImGui::TextDisabled( "What you drew - %u texels", m_Layout.Resolution );
            LayoutPreviewHelper()->Image( m_PaintingImage, ImVec2( pane, pane ) );
            ImGui::EndGroup();
        }

        if ( m_SkyImage && m_HasPreview )
        {
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextDisabled( "The sky from above, north up - %u cells, %.2f km each", m_Preview.Side,
                                 m_Preview.SamplePitchKm );
            LayoutPreviewHelper()->Image( m_SkyImage, ImVec2( pane, pane ) );
            ImGui::EndGroup();
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Looking straight DOWN on the layer, north up. White is cloud, blue is "
                                   "clear. One sample per PLACEMENT CELL where the span allows it - that is "
                                   "the resolution the sky has, and a stroke finer than a cell cannot "
                                   "survive it. Over a wide span the map is coarser still, and the pitch "
                                   "printed above it is always the one drawn." );

            // THE FLIP IS STATED RATHER THAN DISCOVERED. The layout's v runs NORTH and an image's first row
            // is its TOP, so a picture placed in the world stands on its head relative to a north-up map.
            // Both panes are honest to their own reader and the difference between them is real; an artist
            // who does not know it paints a letter and finds it upside down in a rendered sky.
            ImGui::TextWrapped( "The map is your picture FLIPPED TOP TO BOTTOM: the painting's first row "
                                "lies to the SOUTH, because the layout's v axis runs north and an image's "
                                "first row is its top." );
        }
    }

    // -------------------------------------------------------------------------------------------------
    // The two things the protocol knew and the artist did not
    // -------------------------------------------------------------------------------------------------

    void CloudLayoutPanel::DrawVerdictSection()
    {
        if ( !Utils::ImGuiUtilities::SectionHeader( "What the sky says" ) )
            return;

        if ( !m_HasPreview )
        {
            ImGui::TextDisabled( "No map yet." );
            return;
        }

        ImGui::Text( "Coverage asks for %.2f and this painting delivers %.2f over the map.", m_LastLayer.Coverage,
                     m_Preview.MeanCoverage );

        // ---- 1. the pattern and the mask are not independent -------------------------------------

        const float clampedFraction = m_Preview.Cells > 0u ? static_cast<float>( m_Preview.CellsClamped ) /
                                                                  static_cast<float>( m_Preview.Cells )
                                                           : 0.0f;
        const float movedFraction   = m_Preview.Cells > 0u ? static_cast<float>( m_Preview.CellsPatternMoves ) /
                                                                static_cast<float>( m_Preview.Cells )
                                                           : 0.0f;

        if ( m_Layout.HasPattern() && m_Preview.CellsPatternMoves == 0u )
        {
            ImGui::TextColored( kWarnColour,
                                "Layout Pattern Strength does NOTHING here: both ends of it give the same "
                                "sky, cell for cell." );
            ImGui::TextWrapped( "It is not a dead knob - the mask has saturated it. %.0f%% of the cells "
                                "are already pinned at empty or full by the clamp, and a redistribution "
                                "about the painting's own mean has nowhere left to go. Turn Layout Mask "
                                "Strength down and the pattern comes back.",
                                100.0f * clampedFraction );
        }
        else if ( m_Layout.HasPattern() )
        {
            ImGui::TextColored( kGoodColour,
                                "Layout Pattern Strength moves %.0f%% of the cells across its range (%u of "
                                "%u).",
                                100.0f * movedFraction, m_Preview.CellsPatternMoves, m_Preview.Cells );
            ImGui::TextWrapped( "%.0f%% of the cells are pinned at empty or full by the clamp. The pattern "
                                "and the mask are NOT independent: the mask can drive a region past both "
                                "ends of the clamp, and everything the pattern would have said inside it "
                                "is then eaten.",
                                100.0f * clampedFraction );
        }
        else
        {
            ImGui::TextDisabled( "This layout carries no pattern, so Layout Pattern Strength has nothing "
                                 "to apply - only the mask acts." );
        }

        ImGui::Spacing();

        // ---- 2. legibility is bounded by the STROKE, and the validator checks the TEXEL ----------

        ImGui::Text(
             "One texel is %.3f km; the cell of %s is %.2f km; one period of your painting is "
             "%.1f km.",
             m_TexelKm,
             m_LastLayer.Species[std::clamp( m_PreviewSlot, 0, static_cast<int>( m_LastLayer.SpeciesCount ) - 1 )]
                  .TypeName.c_str(),
             m_Preview.CellKm, m_PeriodKm );

        if ( m_Strokes.PaintedTexels == 0u )
        {
            ImGui::TextDisabled( "Channel %d is flat - nothing was drawn on it, so there is no stroke to "
                                 "measure.",
                                 m_PreviewSlot );
        }
        else
        {
            const float thinKm   = m_Strokes.ThinnestTenthTexels * m_TexelKm;
            const float medianKm = m_Strokes.MedianTexels * m_TexelKm;

            ImGui::Text( "Your strokes: the thinnest tenth is %.2f km, the median is %.2f km.", thinKm, medianKm );

            if ( m_Strokes.FractionBelowLimit > 0.02f )
            {
                ImGui::TextColored( kWarnColour,
                                    "%.0f%% of what you drew on this channel is NARROWER THAN ONE CLOUD "
                                    "CELL.",
                                    100.0f * m_Strokes.FractionBelowLimit );
                ImGui::TextWrapped( "Those strokes will break into evenly spaced clumps rather than read "
                                    "as a shape - the sky cannot place a cloud finer than a cell. Lower "
                                    "Layout Repeats, paint thicker, or give the layer a finer Weather "
                                    "Tile Size." );
            }
            else
            {
                ImGui::TextColored( kGoodColour, "Every stroke clears the cell, so the shape will read." );
            }
        }

        // SAID EVERY TIME, not only when it bites. The engine's own validator compares one TEXEL against
        // the cell, which is the right bound for "can the painting tell two cells apart" and says nothing
        // about whether a letter still looks like a letter. That gap is the reason this section exists,
        // and an artist who never sees the warning still has to know which question was answered.
        ImGui::TextDisabled( "The engine checks one TEXEL against the cell - that a painting can tell two "
                             "cells apart. Legibility is your thinnest STROKE against the cell, which no "
                             "validator can know, so it is measured here and reported rather than "
                             "refused." );
    }

    // -------------------------------------------------------------------------------------------------
    // Bake
    // -------------------------------------------------------------------------------------------------

    void CloudLayoutPanel::DrawSaveSection()
    {
        if ( !Utils::ImGuiUtilities::SectionHeader( "Bake" ) )
            return;

        ImGui::BeginDisabled( !m_HasLayout );
        if ( ImGui::Button( "Bake to .dclayout...", ImVec2( 180.0f, 0.0f ) ) )
        {
            std::filesystem::path target =
                 Common::Utils::FileSystem::SaveFileDialog( "Cloud Layout\0*.dclayout\0" );
            if ( !target.empty() )
            {
                if ( target.extension() != Assets::kCloudLayoutExtension )
                    target.replace_extension( Assets::kCloudLayoutExtension );

                const auto written = Assets::CloudLayoutAsset::Save( target, m_Layout );
                if ( !written )
                {
                    m_Status        = "Bake failed: " + written.GetError();
                    m_StatusIsError = true;
                }
                else
                {
                    m_SourceName     = target.filename().string();
                    m_LayoutFromFile = true;
                    m_Status         = "Baked to " + target.string();
                    m_StatusIsError  = false;

                    // Registered straight away so the cloud component's slot lists it without a restart.
                    // A tool whose output only appears after the editor is reopened is a tool nobody
                    // iterates in.
                    if ( m_Assets )
                    {
                        auto painting = m_Assets->FindByPath<Assets::CloudLayoutAsset>( target );
                        if ( painting )
                            painting->Load(); // overwritten in place: re-read so the cached bytes are new
                        else
                            painting = m_Assets->CreateAsset<Assets::CloudLayoutAsset>(
                                 Assets::AssetPriority::Medium, target );

                        if ( painting )
                        {
                            if ( const auto registered =
                                      Runtime::ResourceRegistry::GetCloudLayoutService()->Register( painting );
                                 !registered )
                            {
                                m_Status =
                                     "Baked, but the painting could not be registered: " + registered.GetError();
                                m_StatusIsError = true;
                            }
                        }
                    }
                }
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled( "Paintings live in %s", Common::Constants::Path::CLOUD_LAYOUT_PATH.string().c_str() );

        ImGui::TextDisabled( "Then drag it onto a cloud layer's Cloud Layout slot in Details." );
    }
} // namespace Desert::Editor
