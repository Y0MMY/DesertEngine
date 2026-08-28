#include "CloudLayoutPanel.hpp"

#include "CloudDocumentOpen.hpp"

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

        // The document's VISIBLE title: the subject's file name. Computed before the base class is
        // constructed — IAssetEditorPanel bakes the title in its own constructor and holds it for the
        // window's life — so it is a free function rather than a member.
        std::string SubjectTitle( const Assets::AssetHandle& subject, Assets::AssetManager* assets )
        {
            if ( assets )
            {
                if ( const auto asset = assets->FindByHandle<Assets::CloudLayoutAsset>( subject ) )
                    return asset->GetMetadata().Filepath.filename().string();
            }
            return "Cloud Layout";
        }
    } // namespace

    CloudLayoutPanel::CloudLayoutPanel( const Assets::AssetHandle&             subject,
                                        std::shared_ptr<::Desert::Core::Scene> scene,
                                        Assets::AssetManager*                  assets )
         : IAssetEditorPanel( SubjectTitle( subject, assets ), subject, Assets::AssetTypeID::CloudLayout ),
           m_Scene( std::move( scene ) ), m_Assets( assets )
    {
        LoadSubject();
    }

    void CloudLayoutPanel::LoadSubject()
    {
        if ( !m_Assets )
            return;

        const auto painting = m_Assets->FindByHandle<Assets::CloudLayoutAsset>( Subject() );
        if ( !painting || !painting->IsReadyForUse() )
        {
            m_Status        = "This painting is not loaded - the log says why.";
            m_StatusIsError = true;
            return;
        }

        m_SubjectPath = painting->GetMetadata().Filepath;

        m_Layout     = painting->GetLayout();
        m_HasLayout  = true;
        m_SourceName = m_SubjectPath.filename().string();

        // FROM A FILE, so the channel-mapping controls are disabled and say why: the mapping an image was
        // baked WITH is already in these pixels and there is no picture here to re-map. "Edit this painting"
        // is what puts it back on the canvas.
        m_LayoutFromFile = true;
        m_PreviewDirty   = true;
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

        // THE "ADOPT THE LAYER'S BOUND PAINTING" BLOCK THAT WAS HERE IS GONE, and its absence is required
        // rather than tidy. It existed because the singleton had no subject: with nothing to show, showing
        // the sky you were looking at was the best guess available. A DOCUMENT has a subject, and the guess
        // becomes a defect — a window opened on painting A whose layer happens to wear painting B would
        // have replaced A's pixels with B's on its very first frame, then baked B over A's file. The layer
        // is still READ, for the preview's numbers; it no longer decides what is being edited.

        ImGui::TextWrapped( "A PAINTED SKY. Draw the layout with the brush, or point at a picture and say "
                            "which of its channels feeds which of this layer's cloud species, then bake it "
                            "to a .dclayout the cloud component's Cloud Layout slot accepts. The painting "
                            "is read when the clouds are PLACED, so it costs the frame nothing." );
        ImGui::Separator();

        DrawSourceSection();
        ImGui::Separator();
        DrawChannelSection();
        ImGui::Separator();
        DrawLayerSection();
        ImGui::Separator();

        if ( m_PreviewDirty )
            RefreshPreview( layer );

        // AFTER THE REFRESH, and that placement is the whole reason the section sits here rather than
        // directly under Source. The canvas draws the placement CELL, and the cell it draws has to be the
        // one the verdict below quotes — so it is read out of the map that was just built rather than
        // worked out a second time from the layer.
        DrawBrushSection();
        ImGui::Separator();

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

    void CloudLayoutPanel::LoadSourceImage( const std::filesystem::path& path )
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

        // A NEW SURFACE MEANS A NEW DEVICE IMAGE. Left alone, the canvas pane would go on showing the
        // previous picture's channel until something else happened to move, which reads as an import that
        // did nothing.
        m_CanvasImageDirty = true;

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

        // THE BLANK CANVAS COMES FIRST because it is the answer to the request this panel was extended
        // for: the designer CREATES the texture rather than brings one. Importing is the other button,
        // unchanged and beside it, and the two produce the identical kind of surface — so an artist can
        // start from a picture and paint on it without either path knowing about the other.
        if ( ImGui::Button( "New canvas", ImVec2( 120.0f, 0.0f ) ) )
            StartCanvas( static_cast<uint32_t>( m_NewCanvasSide ) );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "A blank square to draw on. Nothing painted anywhere, and the alpha plane "
                               "at the mask's NEUTRAL 128 - so ticking the mask box on an untouched canvas "
                               "gives a mask that does nothing rather than one that fills the sky." );

        ImGui::SameLine();
        ImGui::SetNextItemWidth( 140.0f );
        int sideChoice = m_NewCanvasSide == 128 ? 0 : m_NewCanvasSide == 256 ? 1 : m_NewCanvasSide == 512 ? 2 : 3;
        if ( ImGui::Combo( "##canvasside", &sideChoice,
                           "128\0"
                           "256\0"
                           "512\0"
                           "1024\0" ) )
        {
            const int sides[] = { 128, 256, 512, 1024 };
            m_NewCanvasSide   = sides[std::clamp( sideChoice, 0, 3 )];
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Texels a side. At the shipped 48 km region and one repeat, 512 puts a "
                               "texel at 94 m and 128 puts it at 375 m - and a texel coarser than a "
                               "placement cell cannot tell two clouds apart at all." );

        ImGui::SameLine();
        if ( ImGui::Button( "Image...", ImVec2( 120.0f, 0.0f ) ) )
        {
            const std::filesystem::path picked =
                 Common::Utils::FileSystem::OpenFileDialog( "Image\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0" );
            if ( !picked.empty() )
                LoadSourceImage( picked );
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
                    LoadSourceImage( dropped );
            }
            ImGui::EndDragDropTarget();
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "A SQUARE picture, 4 to 1024 a side. Non-square and oversized sources are "
                               "refused by name rather than resampled - resampling would be an opinion "
                               "about your painting, and one taken silently is the worst kind." );

        ImGui::SameLine();
        ImGui::TextDisabled( "or drag a .png here from the Content Browser" );

        // THE "OPEN A .dclayout" COMBO THAT WAS HERE IS GONE, and its absence is the feature. It was how
        // this window came to edit a different painting than the one it was opened on — which, now that the
        // title, the ImGui id and open-or-focus are all keyed on the subject handle, would leave a window
        // called one thing baking over another. A different painting is a different window, opened by
        // double-clicking it in the asset browser.

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

            // A FINISHED PAINTING CAN BE PICKED BACK UP, which is what makes this a tool rather than a
            // one-way bake. It reconstructs the RGBA source the file could have been painted on, and
            // REFUSES by name when it could not have been — a layout whose mask was drawn independently of
            // its fourth pattern channel needs five planes and a canvas has four.
            if ( m_LayoutFromFile )
            {
                if ( ImGui::Button( "Edit this painting", ImVec2( 180.0f, 0.0f ) ) )
                {
                    auto canvas = Assets::MakeCloudLayoutCanvasFromLayout( m_Layout );
                    if ( !canvas )
                    {
                        m_Status        = canvas.GetError();
                        m_StatusIsError = true;
                    }
                    else
                    {
                        const Assets::CloudLayoutCanvas& opened = canvas.GetValue();

                        m_SourcePixels = opened.Pixels;
                        m_SourceWidth  = opened.Side;
                        m_SourceHeight = opened.Side;
                        m_TakeMask     = opened.TakeMask;

                        // STRAIGHT RGBA AND NOTHING ELSE. The canvas was rebuilt FROM the pattern planes,
                        // so channel k already holds slot k; any other mapping would rearrange the
                        // painting on the way back in and the artist would watch their species swap.
                        for ( uint32_t slot = 0; slot < Assets::kCloudLayoutChannels; ++slot )
                            m_ChannelForSlot[slot] = slot;

                        m_LayoutFromFile   = false;
                        m_CanvasImageDirty = true;
                        m_Status           = "'" + m_SourceName +
                                   "' is on the canvas. Baking will write a new file "
                                   "unless you pick the same name.";
                        m_StatusIsError = false;

                        RebuildLayout();
                    }
                }
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Puts this .dclayout back on the canvas so the brush can change it. "
                                       "The channel mapping is reset to straight RGBA, because the canvas "
                                       "is rebuilt from the pattern planes themselves." );
            }
        }
        else
        {
            ImGui::TextDisabled( "Nothing loaded yet - start a canvas, or point at a picture." );
        }
    }

    // -------------------------------------------------------------------------------------------------
    // The brush
    // -------------------------------------------------------------------------------------------------

    bool CloudLayoutPanel::CanPaint() const
    {
        return !m_SourcePixels.empty() && m_SourceWidth == m_SourceHeight &&
               m_SourceWidth >= Assets::kCloudLayoutMinResolution &&
               m_SourceWidth <= Assets::kCloudLayoutMaxResolution &&
               m_SourcePixels.size() == static_cast<size_t>( m_SourceWidth ) * m_SourceHeight * 4u;
    }

    void CloudLayoutPanel::StartCanvas( uint32_t side )
    {
        auto made = Assets::MakeCloudLayoutCanvas( side );
        if ( !made )
        {
            m_Status        = made.GetError();
            m_StatusIsError = true;
            LOG_ERROR( "[CloudLayout] {}", m_Status );
            return;
        }

        const Assets::CloudLayoutCanvas& canvas = made.GetValue();

        m_SourcePixels     = canvas.Pixels;
        m_SourceWidth      = canvas.Side;
        m_SourceHeight     = canvas.Side;
        m_SourceName       = "a canvas " + std::to_string( canvas.Side ) + " a side";
        m_TakeMask         = canvas.TakeMask;
        m_LayoutFromFile   = false;
        m_Painting         = false;
        m_Stroke           = Assets::CloudLayoutStroke{};
        m_CanvasImageDirty = true;

        m_Status        = "A blank canvas. Draw on it below, then bake.";
        m_StatusIsError = false;

        RebuildLayout();
    }

    float CloudLayoutPanel::CellTexels() const
    {
        // ONE DERIVATION, TAKEN OUT OF THE MAP. The verdict section reports a percentage against exactly
        // this number; a grid computed a second way from the layer would drift from it the first time
        // BuildCloudLayoutPreview clipped a span, and the artist would be shown a cell the sky does not
        // have.
        if ( !m_HasPreview || m_TexelKm <= 0.0f )
            return 0.0f;

        return m_Preview.CellKm / m_TexelKm;
    }

    void CloudLayoutPanel::RefreshCanvasImage()
    {
        if ( !CanPaint() )
            return;

        const uint32_t side   = m_SourceWidth;
        const size_t   texels = static_cast<size_t>( side ) * side;

        std::vector<unsigned char> grey( texels * 4u, 255u );
        for ( size_t t = 0; t < texels; ++t )
        {
            const unsigned char value = m_SourcePixels[t * 4u + static_cast<size_t>( m_PaintChannel )];
            grey[t * 4u + 0]          = value;
            grey[t * 4u + 1]          = value;
            grey[t * 4u + 2]          = value;
            grey[t * 4u + 3]          = 255u;
        }

        // STREAMED WHEN IT CAN BE. A drag reaches here every frame it moves, and rebuilding the VkImage
        // each time churns a descriptor set for a picture that is one buffer copy. The image is recreated
        // only when its SHAPE changed — a new canvas side — because SetData refuses a size that disagrees.
        //
        // AND IT IS GUARDED BY A DIRTY FLAG BECAUSE THE UPLOAD IS EXPENSIVE, which was MEASURED rather
        // than assumed. Forced to stream every frame, the panel's own profiler scope read 36.5 ms at a 512
        // canvas and 23.8 ms at 128 — sixteen times less data for a third less time, so the cost is the
        // SYNCHRONOUS FLUSH inside SetData (staging allocation, submit, wait, destroy) and not the bytes.
        // Shrinking the canvas would therefore buy almost nothing. Left alone, the same scope reads
        // 0.15-0.22 ms, so the flag is worth a factor of two hundred, and a drag pays the flush only on
        // the frames it actually changed a texel. Debug, validation layers on, machine shared with another
        // agent's renders — the absolute figures are not a budget, the RATIO between them is the finding.
        if ( m_CanvasImage && m_CanvasImageSide == side )
        {
            if ( const auto streamed = m_CanvasImage->SetData( Core::Formats::ImagePixelData( grey ) ); !streamed )
            {
                m_Status        = "The canvas could not be updated on the device: " + streamed.GetError();
                m_StatusIsError = true;
                LOG_ERROR( "[CloudLayout] {}", m_Status );
                return;
            }

            m_CanvasImageChannel = m_PaintChannel;
            m_CanvasImageDirty   = false;
            return;
        }

        const Core::Formats::Image2DSpecification spec{
             .Tag        = "CloudLayoutCanvas",
             .Width      = side,
             .Height     = side,
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Mips       = 1,
             .Data       = std::move( grey ),
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Sample,
        };

        m_CanvasImage = Graphic::Image2D::Create( spec, nullptr );
        if ( !m_CanvasImage )
        {
            m_Status        = "The canvas image could not be created on the device.";
            m_StatusIsError = true;
            LOG_ERROR( "[CloudLayout] {}", m_Status );
            return;
        }

        m_CanvasImageSide    = side;
        m_CanvasImageChannel = m_PaintChannel;
        m_CanvasImageDirty   = false;
    }

    void CloudLayoutPanel::DrawBrushSection()
    {
        if ( !Utils::ImGuiUtilities::SectionHeader( "Brush" ) )
            return;

        if ( !CanPaint() )
        {
            // The device image is dropped rather than kept warm: a canvas nobody can draw on is a megabyte
            // of GPU memory showing a picture that is not the one on screen.
            m_CanvasImage.reset();
            m_CanvasImageSide    = 0u;
            m_CanvasImageChannel = -1;
            m_Painting           = false;

            if ( m_LayoutFromFile )
                ImGui::TextDisabled( "This is a finished .dclayout. Press 'Edit this painting' above to put "
                                     "it back on the canvas." );
            else
                ImGui::TextDisabled( "Start a canvas, or open a SQUARE picture, and the brush appears here." );
            return;
        }

        const float side = static_cast<float>( m_SourceWidth );

        // ---- what the stroke lands in ------------------------------------------------------------

        ImGui::SetNextItemWidth( 220.0f );
        if ( ImGui::Combo( "Painting on", &m_PaintChannel,
                           "Channel 0 (red)\0"
                           "Channel 1 (green)\0"
                           "Channel 2 (blue)\0"
                           "Channel 3 / the mask (alpha)\0" ) )
            m_CanvasImageDirty = true;
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Which plane of the canvas the brush writes. Channels 0..2 are always "
                               "pattern - a species' own field of 'is there cloud of this kind here'. "
                               "Alpha is the ADD/REMOVE MASK when the box in Channels is ticked and species "
                               "slot 3's pattern when it is not: one image has four planes and a layout has "
                               "five tables, so those two share." );

        const bool paintingAlpha = m_PaintChannel == 3;
        const bool paintingMask  = paintingAlpha && m_TakeMask;

        if ( paintingAlpha )
        {
            if ( paintingMask )
                ImGui::TextColored( kGoodColour, "Alpha is the add/remove mask: 128 is neutral, brighter "
                                                 "ADDS cloud, darker REMOVES it." );
            else
            {
                ImGui::TextColored( kWarnColour, "Alpha is species slot 3's PATTERN here - this canvas "
                                                 "carries no mask at all." );
                if ( ImGui::Button( "Make alpha the add/remove mask" ) )
                {
                    m_TakeMask = true;
                    RebuildLayout();
                }
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Ticks the same box the Channels section has. Painting a mask that "
                                       "the bake is not told to take would be a drawing that reaches "
                                       "nothing." );
            }
        }

        // ---- the three numbers -------------------------------------------------------------------
        //
        // NOTHING IS REBUILT WHEN THEY MOVE, and that is not an omission: they describe the NEXT stroke.
        // The canvas already holds what earlier strokes left, so a brush slider has nothing to recompute —
        // which is the same property that lets a stroke's width be promised at all.

        ImGui::SetNextItemWidth( 240.0f );
        ImGui::SliderFloat( "Radius", &m_Brush.RadiusTexels, 1.0f, std::max( 4.0f, side * 0.25f ), "%.1f texels" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Half the stroke's width at full hardness. This is the number the placement "
                               "cell has to be held against." );

        ImGui::SetNextItemWidth( 240.0f );
        ImGui::SliderFloat( "Hardness", &m_Brush.Hardness, 0.0f, 1.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "1 is a crisp disc, 0 ramps from the centre to nothing at the rim. A soft "
                               "brush lays a NARROWER stroke than a hard one of the same radius, and the "
                               "width printed below already accounts for it." );

        ImGui::SetNextItemWidth( 240.0f );
        ImGui::SliderFloat( "Ink", &m_Brush.Ink, 0.0f, 1.0f, "%.2f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( paintingMask ? "On the mask: 1 adds cloud, 0 removes it, 0.5 is the neutral "
                                              "an eraser returns to. Right-drag erases."
                                            : "How much of this species the stroke leaves behind. "
                                              "Right-drag erases back to nothing." );

        // ---- the width rule, in the units the artist has to act in -------------------------------

        const float widthTexels = Assets::CloudLayoutBrushWidthTexels( m_Brush );
        const float cellTexels  = CellTexels();

        if ( m_TexelKm > 0.0f )
            ImGui::Text( "Stroke %.1f texels = %.2f km wide.", widthTexels, widthTexels * m_TexelKm );
        else
            ImGui::Text( "Stroke %.1f texels wide.", widthTexels );

        ImGui::SameLine();
        if ( cellTexels <= 0.0f )
            ImGui::TextDisabled( "(no map yet, so no cell to hold it against)" );
        else if ( widthTexels >= cellTexels )
            ImGui::TextColored( kGoodColour, "Clears the %.2f km placement cell.", m_Preview.CellKm );
        else
            ImGui::TextColored( kWarnColour,
                                "NARROWER than the %.2f km placement cell - it will break into clumps.",
                                m_Preview.CellKm );

        // ---- the canvas --------------------------------------------------------------------------

        if ( m_CanvasImageDirty || m_CanvasImageChannel != m_PaintChannel )
            RefreshCanvasImage();

        if ( !m_CanvasImage )
            return;

        ImGui::SetNextItemWidth( 240.0f );
        ImGui::SliderInt( "Canvas size", &m_CanvasPane, 192, 640, "%d px" );

        ImGui::SameLine();
        ImGui::Checkbox( "Show the placement cell", &m_ShowCellGrid );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "The sky cannot place a cloud finer than one cell, so a stroke narrower than "
                               "a square of this grid does not survive to the sky - it comes out as evenly "
                               "spaced clumps. The engine's own validator checks one TEXEL against the "
                               "cell, which is a different and weaker question." );

        const float  pane   = static_cast<float>( m_CanvasPane );
        const float  scale  = pane / side;
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        // THE INVISIBLE BUTTON IS SUBMITTED FIRST AND THE PICTURE IS DRAWN INTO THE DRAW LIST, rather than
        // an ImGui::Image with a button laid over it. Two overlapping items would leave which one owns the
        // hover to submission order; one item that happens to have a picture under it cannot.
        ImGui::InvisibleButton( "##cloudlayoutcanvas", ImVec2( pane, pane ),
                                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight );

        const bool active  = ImGui::IsItemActive();
        const bool started = ImGui::IsItemActivated();
        const bool hovered = ImGui::IsItemHovered();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddImage( reinterpret_cast<ImTextureID>(
                             const_cast<void*>( LayoutPreviewHelper()->GetTextureID( m_CanvasImage ) ) ),
                        origin, ImVec2( origin.x + pane, origin.y + pane ) );

        bool cellTooFine = false;
        if ( m_ShowCellGrid && cellTexels > 0.0f )
        {
            const float step = cellTexels * scale;
            if ( step >= 4.0f )
            {
                const ImU32 line  = IM_COL32( 255, 210, 90, 90 );
                const int   count = static_cast<int>( pane / step );
                for ( int i = 0; i <= count; ++i )
                {
                    const float at = static_cast<float>( i ) * step;
                    draw->AddLine( ImVec2( origin.x + at, origin.y ), ImVec2( origin.x + at, origin.y + pane ),
                                   line );
                    draw->AddLine( ImVec2( origin.x, origin.y + at ), ImVec2( origin.x + pane, origin.y + at ),
                                   line );
                }
            }
            else
            {
                cellTooFine = true;
            }
        }

        draw->AddRect( origin, ImVec2( origin.x + pane, origin.y + pane ), IM_COL32( 130, 130, 130, 255 ) );

        // ---- the drag ----------------------------------------------------------------------------
        //
        // THE ADAPTER IS THIS AND NOTHING MORE: a screen point becomes a texel, and the segment between
        // the last one and this one is handed to the engine's stroke. Every decision about what a stroke
        // IS - its profile, its width, its wrap, that it is laid once at its strongest - lives in
        // Engine/Assets/CloudLayout.cpp, where a test can reach it and a command can reproduce it.
        const ImVec2    mouse = ImGui::GetIO().MousePos;
        const glm::vec2 texel( ( mouse.x - origin.x ) / scale, ( mouse.y - origin.y ) / scale );

        if ( started )
        {
            Assets::CloudLayoutStroke opened;
            if ( const auto began = Assets::BeginCloudLayoutStroke( opened, m_SourcePixels, m_SourceWidth,
                                                                    static_cast<uint32_t>( m_PaintChannel ) );
                 !began )
            {
                m_Status        = began.GetError();
                m_StatusIsError = true;
                LOG_ERROR( "[CloudLayout] {}", m_Status );
            }
            else
            {
                m_Stroke         = std::move( opened );
                m_Painting       = true;
                m_LastPaintTexel = texel;
            }
        }

        if ( m_Painting && active )
        {
            Assets::CloudLayoutBrush stroke = m_Brush;

            // RIGHT-DRAG IS THE SAME BRUSH WITH THE REST VALUE AS ITS INK. An eraser that is anything else
            // is a second code path doing one thing — and the rest value differs per plane, because the
            // mask is signed about neutral and a pattern channel is not.
            if ( ImGui::IsMouseDown( ImGuiMouseButton_Right ) && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
                stroke.Ink = paintingMask ? static_cast<float>( Assets::kCloudLayoutMaskNeutral ) / 255.0f : 0.0f;

            if ( Assets::ExtendCloudLayoutStroke( m_Stroke, m_SourcePixels, m_LastPaintTexel, texel, stroke ) >
                 0u )
                m_CanvasImageDirty = true;

            m_LastPaintTexel = texel;
        }
        else if ( m_Painting )
        {
            // THE LAYOUT IS REBUILT WHEN THE DRAG ENDS, not on every frame of it. A rebuild encodes a
            // megabyte, hashes it and decodes it again; per frame that turns a brush into a slideshow and
            // tells the artist nothing they could not wait one stroke for.
            m_Painting = false;
            m_Stroke   = Assets::CloudLayoutStroke{};
            RebuildLayout();
        }

        // ---- the cursor, against the grid --------------------------------------------------------

        if ( hovered || m_Painting )
        {
            const ImVec2 at( origin.x + texel.x * scale, origin.y + texel.y * scale );

            // TWO CIRCLES AND THEY MEAN DIFFERENT THINGS. The bright one is the width the stroke MEASURES
            // at, which is the one to hold against a square of the grid; the faint one is how far any ink
            // at all reaches. At full hardness they are the same circle, which is itself the point.
            draw->AddCircle( at, 0.5f * widthTexels * scale, IM_COL32( 255, 255, 255, 220 ), 0, 2.0f );
            if ( m_Brush.Hardness < 0.999f )
                draw->AddCircle( at, m_Brush.RadiusTexels * scale, IM_COL32( 255, 255, 255, 80 ) );
        }

        if ( cellTooFine )
            ImGui::TextDisabled( "One placement cell is %.1f screen pixels here - too fine to draw. Make the "
                                 "canvas bigger, or lower Layout Repeats.",
                                 cellTexels * scale );

        ImGui::TextDisabled( "Left-drag paints, right-drag erases. The canvas is drawn as AUTHORED - the "
                             "map below is it flipped top to bottom." );

        // WHY A BLANK CANVAS MAKES THE SKY FLAT, said where the surprise happens. A cell's coverage has
        // exactly ONE modulator: the painting when one is bound and turned up, the procedural weather
        // patch otherwise. So pressing New Canvas does not leave the sky alone and then add to it — it
        // takes the weather away and hands the sky to a drawing with nothing on it yet. The symptom
        // without this line is an artist reporting that the panel "flattened my clouds".
        if ( m_LastLayer.FromScene && m_LastLayer.PatchStrength > 0.0f &&
             m_LastLayer.Placement.PatternStrength > 0.0f )
            ImGui::TextDisabled( "While a painting is bound with Layout Pattern Strength above zero, it "
                                 "REPLACES this layer's Weather Patch Strength of %.2f - one modulator per "
                                 "cell, never two. An empty canvas is therefore a flat sky at the Coverage "
                                 "slider, not the sky you had.",
                                 m_LastLayer.PatchStrength );
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

        // BAKE WRITES THE SUBJECT. BAKE AS CREATES A SECOND ASSET AND OPENS ITS OWN WINDOW — it does NOT
        // repoint this one. The subject is this window's identity: its title, its ImGui id and the key
        // open-or-focus matches on are all built from it, so a document that followed a Bake As would be a
        // window named after a file it no longer edits. Authoring a new painting is still exactly what it
        // was — draw or import, Bake As — and now the new one arrives as its own document.
        ImGui::BeginDisabled( !m_HasLayout || m_SubjectPath.empty() );
        const bool bake = ImGui::Button( "Bake", ImVec2( 180.0f, 0.0f ) );
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled( !m_HasLayout );
        const bool bakeAs = ImGui::Button( "Bake As...", ImVec2( 180.0f, 0.0f ) );
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled( "Paintings live in %s", Common::Constants::Path::CLOUD_LAYOUT_PATH.string().c_str() );

        ImGui::TextDisabled( "Then drag it onto a cloud layer's Cloud Layout slot in Details." );

        std::filesystem::path target;
        if ( bake )
        {
            target = m_SubjectPath;
        }
        else if ( bakeAs )
        {
            target = Common::Utils::FileSystem::SaveFileDialog( "Cloud Layout\0*.dclayout\0" );
            if ( !target.empty() && target.extension() != Assets::kCloudLayoutExtension )
                target.replace_extension( Assets::kCloudLayoutExtension );
        }

        if ( target.empty() )
            return;

        // Compared before the write, because after it the file exists and the two paths would be
        // indistinguishable by anything on disk.
        const bool isCopy = target != m_SubjectPath;

        const auto written = Assets::CloudLayoutAsset::Save( target, m_Layout );
        if ( !written )
        {
            m_Status        = "Bake failed: " + written.GetError();
            m_StatusIsError = true;
            return;
        }

        if ( !isCopy )
        {
            m_SourceName     = target.filename().string();
            m_LayoutFromFile = true;
        }
        m_Status        = "Baked to " + target.string();
        m_StatusIsError = false;

        // Re-registered straight away so the cloud component's slot shows the new pixels without a restart.
        // A tool whose output only appears after the editor is reopened is a tool nobody iterates in.
        if ( !m_Assets )
            return;

        auto painting = m_Assets->FindByPath<Assets::CloudLayoutAsset>( target );
        if ( painting )
            painting->Load(); // overwritten in place: re-read so the cached bytes are new
        else
            painting = m_Assets->CreateAsset<Assets::CloudLayoutAsset>( Assets::AssetPriority::Medium, target );

        if ( !painting )
            return;

        if ( const auto registered = Runtime::ResourceRegistry::GetCloudLayoutService()->Register( painting );
             !registered )
        {
            m_Status        = "Baked, but the painting could not be registered: " + registered.GetError();
            m_StatusIsError = true;
            return;
        }

        if ( isCopy )
        {
            // The copy is a new asset, so it gets its own document. Queued rather than constructed here:
            // EditorLayer is the only place that may create a panel, and this is the same wire the asset
            // browser's double-click uses.
            RequestCloudDocument( m_Assets, target.string() );
            m_Status        = "Baked a copy to " + target.string() + " - it has opened in its own window.";
            m_StatusIsError = false;
        }
    }
} // namespace Desert::Editor
