#include "CloudLayoutPanel.hpp"

#include <Editor/Core/DragPayloads.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/CloudLayoutAsset.hpp>
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

            return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                   extension == ".tga" || extension == ".bmp";
        }

        const ImVec4 kErrorColour( 0.95f, 0.45f, 0.40f, 1.0f );
        const ImVec4 kWarnColour( 0.95f, 0.78f, 0.35f, 1.0f );
        const ImVec4 kGoodColour( 0.55f, 0.85f, 0.55f, 1.0f );
    } // namespace

    CloudLayoutPanel::CloudLayoutPanel( std::shared_ptr<::Desert::Core::Scene> scene,
                                        Assets::AssetManager*                    assets )
         : IPanel( "Cloud Layout", /*showPanel=*/false ), m_Scene( std::move( scene ) ), m_Assets( assets )
    {
    }

    // -------------------------------------------------------------------------------------------------
    // The layer
    // -------------------------------------------------------------------------------------------------

    CloudLayoutPanel::LayerContext CloudLayoutPanel::ReadLayer() const
    {
        LayerContext layer;

        if ( !m_Scene )
            return layer;

        auto view = m_Scene->GetRegistry().view<ECS::VolumetricCloudComponent>();
        if ( view.begin() == view.end() )
            return layer;

        const ECS::VolumetricCloudData& data = view.get<ECS::VolumetricCloudComponent>( *view.begin() ).Data;

        layer.FromScene    = true;
        layer.RegionSizeKm = std::max( data.RegionSize, 1.0f ) / Graphic::kCloudWorldUnitsPerKm;
        layer.Coverage     = std::clamp( data.Coverage, 0.0f, 1.0f );
        layer.PatchStrength = std::clamp( data.PatchStrength, 0.0f, 1.0f );
        layer.Seed          = static_cast<uint32_t>( std::max( data.Seed, 0 ) );
        layer.ResolvableChordKm =
             Graphic::CloudFinestResolvableChordKm( static_cast<float>( std::clamp( data.MaxSteps, 8, 512 ) ) );

        // THE CELL IS THE LAYER'S LATTICE TIMES THE FINEST TYPE'S Placement Scale, and the finest is the
        // right one because it is the species a thin stroke loses first — the same species
        // ValidateCloudProceduralLayout measures its texel against.
        float finestScale = 0.0f;
        if ( auto* types = Runtime::ResourceRegistry::GetCloudTypeService() )
        {
            const Assets::AssetHandle bound[] = { data.CloudType1, data.CloudType2, data.CloudType3,
                                                  data.CloudType4 };
            for ( const Assets::AssetHandle& handle : bound )
            {
                if ( handle == Assets::AssetHandle::Null() )
                    continue;
                const float scale = types->GetShape( handle ).PlacementScale;
                if ( scale > 1e-3f && ( finestScale <= 0.0f || scale < finestScale ) )
                    finestScale = scale;
            }
        }

        layer.CellKm = ECS::CloudLayerLatticeKm( data ) * ( finestScale > 0.0f ? finestScale : 1.0f );

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
        if ( !m_SowPanel )
            return;

        const LayerContext layer = ReadLayer();

        // THE LAYER IS COMPARED FIELD BY FIELD rather than by a dirty flag somebody has to set: the
        // numbers live on a component the Details panel edits, and there is no notification. Comparing is
        // eleven floats a frame and it is what makes moving Layout Repeats in Details redraw this map.
        const bool layerMoved = layer.FromScene != m_LastLayer.FromScene ||
                                layer.RegionSizeKm != m_LastLayer.RegionSizeKm ||
                                layer.CellKm != m_LastLayer.CellKm || layer.Coverage != m_LastLayer.Coverage ||
                                layer.PatchStrength != m_LastLayer.PatchStrength ||
                                layer.ResolvableChordKm != m_LastLayer.ResolvableChordKm ||
                                layer.Seed != m_LastLayer.Seed ||
                                !Assets::CloudLayoutPlacementEqual( layer.Placement, m_LastLayer.Placement );

        if ( layerMoved )
            m_PreviewDirty = true;
        m_LastLayer = layer;

        // THE PANEL OPENS ON THE SKY YOU ARE LOOKING AT. A layer with a painting bound hands it to the
        // panel once, when it changes — once and not every frame, so that an artist who then opens a
        // different picture is not overwritten by the scene on the next frame.
        if ( layer.BoundLayout != m_AdoptedLayout )
        {
            m_AdoptedLayout = layer.BoundLayout;
            if ( layer.BoundLayout != 0u && m_Assets )
            {
                if ( auto painting = m_Assets->FindByHandle<Assets::CloudLayoutAsset>(
                          Common::UUID( layer.BoundLayout ) );
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
                    m_StatusIsError = false;
                }
            }
        }

        if ( ImGui::Begin( m_PanelName.c_str(), &m_SowPanel ) )
        {
            ImGui::TextWrapped(
                 "A PAINTED SKY. Point at a picture, say which of its channels feeds which of the layer's "
                 "four cloud type slots, and bake it to a .dclayout the cloud component's Cloud Layout "
                 "slot accepts. The painting is read when the clouds are PLACED, not while they are "
                 "drawn, so it costs the frame nothing." );
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
        ImGui::End();
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
            m_Status = "'" + path.filename().string() +
                       "' could not be read as an image: " +
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

        m_Status = "Read '" + m_SourceName + "': " + std::to_string( width ) + "x" +
                   std::to_string( height ) + ", " + std::to_string( sourceChannels ) +
                   " channels in the file.";
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
        Utils::ImGuiUtilities::SectionHeader( "Source" );

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
                               "refused by name rather than resampled — resampling would be an opinion "
                               "about your painting, and one taken silently is the worst kind." );

        ImGui::SameLine();
        ImGui::TextDisabled( "or drag a .png here from the Content Browser" );

        // Opening a finished painting is the other half of the tool: it is how an artist sees what a
        // shipped layout does to their sky before binding it, and how they check one somebody else baked.
        if ( m_Assets )
        {
            const auto paintings = m_Assets->FindAllByType<Assets::CloudLayoutAsset>();
            ImGui::BeginDisabled( paintings.empty() );
            if ( ImGui::BeginCombo( "Open",
                                    paintings.empty() ? "(no paintings on disk)" : "Pick a .dclayout" ) )
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
                                   "what it was baked with and cannot be changed — point at the picture "
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
            ImGui::Text( "%s — %ux%u, pattern %s, mask %s, content %08x", m_SourceName.c_str(),
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
        Utils::ImGuiUtilities::SectionHeader( "Channels" );

        ImGui::TextWrapped( "Which channel of the picture feeds which of the layer's four cloud type "
                            "slots. A slot's channel is its own field of 'is there cloud of this kind "
                            "here' — the four overlap freely and the sky takes whichever wins." );

        ImGui::BeginDisabled( m_LayoutFromFile || m_SourcePixels.empty() );

        bool remap = false;
        for ( uint32_t slot = 0; slot < Assets::kCloudLayoutChannels; ++slot )
        {
            const std::string label   = "Slot " + std::to_string( slot ) + " takes";
            int               channel = static_cast<int>( m_ChannelForSlot[slot] );
            if ( ImGui::Combo( label.c_str(), &channel, "Red\0Green\0Blue\0Alpha\0" ) )
            {
                m_ChannelForSlot[slot] = static_cast<uint32_t>( channel );
                remap                  = true;
            }
        }

        if ( ImGui::Button( "One drawing on every slot" ) )
        {
            for ( uint32_t slot = 0; slot < Assets::kCloudLayoutChannels; ++slot )
                m_ChannelForSlot[slot] = 0u;
            remap = true;
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Red into all four slots — what a greyscale painting usually wants, and "
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
            ImGui::TextDisabled( "Opened from a .dclayout — the mapping is already baked into its pixels." );

        if ( remap )
            RebuildLayout();
    }

    // -------------------------------------------------------------------------------------------------
    // The layer, READ and not edited
    // -------------------------------------------------------------------------------------------------

    void CloudLayoutPanel::DrawLayerSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "The layer this hangs in" );

        const LayerContext& layer = m_LastLayer;

        if ( layer.FromScene )
            ImGui::TextWrapped( "Read from this scene's cloud layer. Change any of it in Details and the "
                                "map below follows on the same frame — there is deliberately no copy of "
                                "these numbers here." );
        else
            ImGui::TextColored( kWarnColour, "This scene has NO cloud layer, so the map below is drawn "
                                             "against the shipped defaults printed here." );

        ImGui::Text( "Region Size %.1f km      placement cell %.2f km", layer.RegionSizeKm, layer.CellKm );
        ImGui::Text( "Coverage %.2f      Weather Patch Strength %.2f      Seed %u", layer.Coverage,
                     layer.PatchStrength, layer.Seed );
        ImGui::Text( "Layout Repeats %u      Rotation %u quarter turns      Offset (%.2f, %.2f) km",
                     layer.Placement.RepeatsPerRegion, layer.Placement.QuarterTurns,
                     layer.Placement.OffsetKm.x, layer.Placement.OffsetKm.y );
        ImGui::Text( "Layout Pattern Strength %.2f      Layout Mask Strength %.2f",
                     layer.Placement.PatternStrength, layer.Placement.MaskStrength );

        // The SAME validator the bake runs, so the panel refuses for the same reason the bake refuses
        // rather than the two disagreeing about what is legal.
        if ( const auto valid = Assets::ValidateCloudLayoutPlacement( layer.Placement ); !valid )
            ImGui::TextColored( kErrorColour, "%s", valid.GetError().c_str() );
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

        // THE PATCH TILE IS FLOORED AGAINST THE CELL exactly as the renderer floors it, because a
        // modulation finer than three cells decides cells one at a time and reads as a checkerboard. A
        // preview drawn with an unfloored tile would show a sky the layer cannot produce.
        params.PatchTileKm = std::max( 21.0f, 3.0f * layer.CellKm );

        if ( m_HasLayout )
            params.Layout = std::make_shared<const Assets::CloudLayoutData>( m_Layout );

        // Four slots of one size, because the map answers "what does this picture do to a sky whose cells
        // are this big". The layer's own per-type scales are already folded into LayerContext::CellKm.
        params.Species.resize( Assets::kCloudLayoutChannels );
        for ( auto& species : params.Species )
        {
            species.CellKm     = std::max( layer.CellKm, 1e-3f );
            species.Anisotropy = 1.0f;
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
        const uint32_t slot = static_cast<uint32_t>( std::clamp( m_PreviewSlot, 0, 3 ) );

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
        Utils::ImGuiUtilities::SectionHeader( "Preview" );

        if ( !m_HasLayout )
        {
            ImGui::TextDisabled( "Load a picture, or open a .dclayout, to see what it does to a sky." );
            return;
        }

        if ( ImGui::SliderInt( "Slot to map", &m_PreviewSlot, 0, 3, "slot %d" ) )
        {
            m_PaintingView = static_cast<PaintingView>( m_PreviewSlot );
            m_PreviewDirty = true;
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Which of the layer's four cloud type slots to map. Each slot is its own "
                               "channel of the painting and its own field of clouds." );

        int view = static_cast<int>( m_PaintingView );
        if ( ImGui::Combo( "Painting shows", &view,
                           "Slot 0's channel\0"
                           "Slot 1's channel\0"
                           "Slot 2's channel\0"
                           "Slot 3's channel\0"
                           "The add/remove mask\0" ) )
        {
            m_PaintingView = static_cast<PaintingView>( view );
            m_PreviewDirty = true;
        }

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
            ImGui::TextDisabled( "What you drew — %u texels", m_Layout.Resolution );
            LayoutPreviewHelper()->Image( m_PaintingImage, ImVec2( pane, pane ) );
            ImGui::EndGroup();
        }

        if ( m_SkyImage && m_HasPreview )
        {
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextDisabled( "The sky from above — %u cells, %.2f km each", m_Preview.Side,
                                 m_Preview.SamplePitchKm );
            LayoutPreviewHelper()->Image( m_SkyImage, ImVec2( pane, pane ) );
            ImGui::EndGroup();
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Looking straight DOWN on the layer, north up. White is cloud, blue is "
                                   "clear. It is drawn one PLACEMENT CELL per pixel because that is the "
                                   "resolution the sky has — a stroke finer than a cell cannot survive it, "
                                   "and this is where you see that happen." );
        }
    }

    // -------------------------------------------------------------------------------------------------
    // The two things the protocol knew and the artist did not
    // -------------------------------------------------------------------------------------------------

    void CloudLayoutPanel::DrawVerdictSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "What the sky says" );

        if ( !m_HasPreview )
        {
            ImGui::TextDisabled( "No map yet." );
            return;
        }

        ImGui::Text( "Coverage asks for %.2f and this painting delivers %.2f over the map.",
                     m_LastLayer.Coverage, m_Preview.MeanCoverage );

        // ---- 1. the pattern and the mask are not independent -------------------------------------

        const float clampedFraction =
             m_Preview.Cells > 0u
                  ? static_cast<float>( m_Preview.CellsClamped ) / static_cast<float>( m_Preview.Cells )
                  : 0.0f;
        const float movedFraction =
             m_Preview.Cells > 0u
                  ? static_cast<float>( m_Preview.CellsPatternMoves ) / static_cast<float>( m_Preview.Cells )
                  : 0.0f;

        if ( m_Layout.HasPattern() && m_Preview.CellsPatternMoves == 0u )
        {
            ImGui::TextColored( kWarnColour,
                                "Layout Pattern Strength does NOTHING here: both ends of it give the same "
                                "sky, cell for cell." );
            ImGui::TextWrapped( "It is not a dead knob — the mask has saturated it. %.0f%% of the cells "
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
                                 "to apply — only the mask acts." );
        }

        ImGui::Spacing();

        // ---- 2. legibility is bounded by the STROKE, and the validator checks the TEXEL ----------

        ImGui::Text( "One texel is %.3f km; the placement cell is %.2f km; one period of your painting is "
                     "%.1f km.",
                     m_TexelKm, m_Preview.CellKm, m_PeriodKm );

        if ( m_Strokes.PaintedTexels == 0u )
        {
            ImGui::TextDisabled( "Slot %d is flat — nothing was drawn on it, so there is no stroke to "
                                 "measure.",
                                 m_PreviewSlot );
        }
        else
        {
            const float thinKm   = m_Strokes.ThinnestTenthTexels * m_TexelKm;
            const float medianKm = m_Strokes.MedianTexels * m_TexelKm;

            ImGui::Text( "Your strokes: the thinnest tenth is %.2f km, the median is %.2f km.", thinKm,
                         medianKm );

            if ( m_Strokes.FractionBelowLimit > 0.02f )
            {
                ImGui::TextColored( kWarnColour,
                                    "%.0f%% of what you drew on this slot is NARROWER THAN ONE CLOUD "
                                    "CELL.",
                                    100.0f * m_Strokes.FractionBelowLimit );
                ImGui::TextWrapped( "Those strokes will break into evenly spaced clumps rather than read "
                                    "as a shape — the sky cannot place a cloud finer than a cell. Lower "
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
        ImGui::TextDisabled( "The engine checks one TEXEL against the cell — that a painting can tell two "
                             "cells apart. Legibility is your thinnest STROKE against the cell, which no "
                             "validator can know, so it is measured here and reported rather than "
                             "refused." );
    }

    // -------------------------------------------------------------------------------------------------
    // Bake
    // -------------------------------------------------------------------------------------------------

    void CloudLayoutPanel::DrawSaveSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "Bake" );

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
                                      Runtime::ResourceRegistry::GetCloudLayoutService()->Register(
                                           painting );
                                 !registered )
                            {
                                m_Status = "Baked, but the painting could not be registered: " +
                                           registered.GetError();
                                m_StatusIsError = true;
                            }
                        }
                    }
                }
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled( "Paintings live in %s",
                             Common::Constants::Path::CLOUD_LAYOUT_PATH.string().c_str() );

        ImGui::TextDisabled( "Then drag it onto a cloud layer's Cloud Layout slot in Details." );
    }
} // namespace Desert::Editor
