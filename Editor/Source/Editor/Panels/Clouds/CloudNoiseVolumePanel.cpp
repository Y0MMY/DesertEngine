#include "CloudNoiseVolumePanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/CloudNoiseVolumeAsset.hpp>
#include <Engine/Assets/CloudNoiseVolumeGenerator.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <ImGui/imgui.h>

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace Desert::Editor
{
    // The editor's ImGui lives in the global namespace; unqualified `ImGui::` inside `Desert::` would
    // resolve to `Desert::ImGui`, which is the engine's own runtime UI. Every panel in this folder opens
    // with the same alias for the same reason.
    namespace ImGui = ::ImGui;

    namespace
    {
        // The panel's own UIHelper, created on first use exactly as NodeGraphPanel's is: the helper caches
        // ImGui texture ids by image view, and a panel that never opens should not build one.
        std::unique_ptr<UI::UIHelper>& SlicePreviewHelper()
        {
            static std::unique_ptr<UI::UIHelper> helper;
            if ( !helper )
            {
                helper = std::make_unique<UI::UIHelper>();
                helper->Init();
            }
            return helper;
        }

        const char* AxisName( int axis )
        {
            return axis == 0 ? "X" : axis == 1 ? "Y" : "Z";
        }
    } // namespace

    CloudNoiseVolumePanel::CloudNoiseVolumePanel( Assets::AssetManager* assets )
         : IPanel( "Cloud Noise Volume", /*showPanel=*/false ), m_Assets( assets )
    {
    }

    CloudNoiseVolumePanel::~CloudNoiseVolumePanel()
    {
        // A bake writes into a std::future this object owns, so the panel must outlive it. Waiting here
        // rather than detaching is the difference between a slow editor shutdown and a use-after-free.
        if ( m_Baking.valid() )
            m_Baking.wait();
    }

    void CloudNoiseVolumePanel::OnUIRender()
    {
        // NO ImGui::Begin HERE, and it is not an omission. EditorLayer's panel loop already wraps
        // OnUIRender in Begin/End for this panel's name, and it owns the p_open bool the title-bar X
        // writes to. A Begin for the SAME name nested inside that one is not appending -- ImGui only
        // supports appending between Begin/End PAIRS -- and the window comes out with its title bar
        // and nothing else. Every panel in this folder had it and drew nothing; no panel outside it
        // does. See CALIBRATION.md §PTP.
        ImGui::TextWrapped(
             "The 3D noise the cloud shape is eroded with. Four channels, following Nubis Cubed "
             "(SIGGRAPH 2023, slide 96): R and G are low- and high-frequency Curly-Alligator (wispy), "
             "B and A are low- and high-frequency Alligator (billowy)." );
        ImGui::Separator();

        DrawGenerateSection();
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

    void CloudNoiseVolumePanel::DrawGenerateSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "Generate" );

        // Collect a finished bake before drawing anything that depends on m_Volume, so the frame the bake
        // ends is already the frame that shows it.
        if ( m_BakeRunning && m_Baking.valid() &&
             m_Baking.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
        {
            auto result   = m_Baking.get();
            m_BakeRunning = false;
            if ( result )
            {
                m_Volume        = result.ExtractValue();
                m_HasVolume     = true;
                m_SourceName    = "(baked, unsaved)";
                m_SliceIndex    = static_cast<int>( m_Volume.Params.Resolution ) / 2;
                m_SliceDirty    = true;
                m_Status        = "Baked. Save it to make it an asset the cloud component can point at.";
                m_StatusIsError = false;
            }
            else
            {
                m_Status        = "Bake failed: " + result.GetError();
                m_StatusIsError = true;
            }
        }

        const bool busy = m_BakeRunning;
        ImGui::BeginDisabled( busy );

        int resolutionIndex = m_Params.Resolution == 64u ? 0 : 1;
        if ( ImGui::Combo( "Resolution", &resolutionIndex,
                           "64 (preview)\0"
                           "128 (ship)\0" ) )
            m_Params.Resolution = resolutionIndex == 0 ? 64u : 128u;
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "128 is the shipping size and the deck's (8 MiB in RGBA8). 64 bakes eight "
                               "times faster and is for finding a look, not for shipping." );

        int seed = static_cast<int>( m_Params.Seed );
        if ( ImGui::DragInt( "Seed", &seed, 1.0f, 0, 1000000 ) )
            m_Params.Seed = static_cast<uint32_t>( seed < 0 ? 0 : seed );
        ImGui::SameLine();
        if ( ImGui::Button( "Re-roll" ) )
            m_Params.Seed = m_Params.Seed * 1664525u + 1013904223u;

        ImGui::DragFloat( "Curl Strength", &m_Params.CurlStrength, 0.005f, 0.0f, 0.5f, "%.3f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "How far the divergence-free flow shears the wispy channels, in lattice "
                               "cells. 0 leaves plain inverted Alligator - a web with no hooks in it." );

        ImGui::DragFloat( "Wispy Period LF", &m_Params.WispyPeriodLowFrequency, 1.0f, 1.0f, 32.0f, "%.0f" );
        ImGui::DragFloat( "Wispy Period HF", &m_Params.WispyPeriodHighFrequency, 1.0f, 1.0f, 32.0f, "%.0f" );
        ImGui::DragFloat( "Billow Period LF", &m_Params.BillowPeriodLowFrequency, 1.0f, 1.0f, 32.0f, "%.0f" );
        ImGui::DragFloat( "Billow Period HF", &m_Params.BillowPeriodHighFrequency, 1.0f, 1.0f, 32.0f, "%.0f" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Lattice cells across the volume. Whole numbers only - a fractional period "
                               "does not tile, and the seam runs the length of the sky." );

        ImGui::EndDisabled();

        // The SAME validation the loader runs, so the button and the file agree about what is legal.
        const auto valid = Assets::ValidateCloudNoiseVolumeParams( m_Params );
        if ( !valid )
            ImGui::TextColored( ImVec4( 0.95f, 0.45f, 0.40f, 1.0f ), "%s", valid.GetError().c_str() );

        ImGui::BeginDisabled( busy || !valid );
        if ( ImGui::Button( "Bake", ImVec2( 120.0f, 0.0f ) ) )
        {
            m_BakeProgress.store( 0.0f );
            m_BakeRunning   = true;
            m_Status        = "Baking...";
            m_StatusIsError = false;

            const Assets::CloudNoiseVolumeParams params = m_Params;
            m_Baking                                    = std::async( std::launch::async, [this, params]
                                                                      { return Assets::GenerateCloudNoiseVolume( params, &m_BakeProgress ); } );
        }
        ImGui::EndDisabled();

        if ( busy )
        {
            ImGui::SameLine();
            ImGui::ProgressBar( m_BakeProgress.load(), ImVec2( -1.0f, 0.0f ) );
        }

        // Loading an existing volume back in is half the tool: it is how an artist opens the default,
        // sees what it is made of, and re-rolls a variant of it rather than starting from nothing.
        if ( m_Assets )
        {
            const auto volumes = m_Assets->FindAllByType<Assets::CloudNoiseVolumeAsset>();
            ImGui::BeginDisabled( busy || volumes.empty() );
            if ( ImGui::BeginCombo( "Open", volumes.empty() ? "(no volumes on disk)" : "Pick a volume" ) )
            {
                for ( const auto& [handle, asset] : volumes )
                {
                    const std::string name = asset->GetMetadata().Filepath.filename().string();
                    if ( ImGui::Selectable( name.c_str() ) && asset->IsReadyForUse() )
                    {
                        m_Volume        = asset->GetVolume();
                        m_Params        = m_Volume.Params;
                        m_HasVolume     = true;
                        m_SourceName    = name;
                        m_SliceIndex    = static_cast<int>( m_Volume.Params.Resolution ) / 2;
                        m_SliceDirty    = true;
                        m_Status        = "Opened '" + name + "' - its parameters are above, ready to re-roll.";
                        m_StatusIsError = false;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        }
    }

    std::vector<unsigned char> CloudNoiseVolumePanel::BuildSlicePixels() const
    {
        const uint32_t             n = m_Volume.Params.Resolution;
        std::vector<unsigned char> pixels( static_cast<size_t>( n ) * n * 4u, 0u );

        const int slice = m_SliceIndex < 0 ? 0
                                           : ( m_SliceIndex >= static_cast<int>( n ) ? static_cast<int>( n ) - 1
                                                                                     : m_SliceIndex );

        for ( uint32_t v = 0; v < n; ++v )
        {
            for ( uint32_t u = 0; u < n; ++u )
            {
                uint32_t x = 0;
                uint32_t y = 0;
                uint32_t z = 0;
                switch ( m_Axis )
                {
                    case SliceAxis::X:
                        x = static_cast<uint32_t>( slice );
                        y = u;
                        z = v;
                        break;
                    case SliceAxis::Y:
                        x = u;
                        y = static_cast<uint32_t>( slice );
                        z = v;
                        break;
                    case SliceAxis::Z:
                        x = u;
                        y = v;
                        z = static_cast<uint32_t>( slice );
                        break;
                }

                const size_t source = ( ( static_cast<size_t>( z ) * n + y ) * n + x ) * 4u;
                const size_t target = ( static_cast<size_t>( v ) * n + u ) * 4u;

                if ( m_ChannelView == ChannelView::AllFour )
                {
                    // All four at once, and the alpha channel is folded into the picture rather than left
                    // to the compositor: an image drawn with a real alpha would show the ImGui window
                    // behind it, which reads as the volume being empty exactly where it is densest.
                    pixels[target + 0] = m_Volume.Voxels[source + 0];
                    pixels[target + 1] = m_Volume.Voxels[source + 1];
                    pixels[target + 2] =
                         static_cast<unsigned char>( ( static_cast<int>( m_Volume.Voxels[source + 2] ) +
                                                       static_cast<int>( m_Volume.Voxels[source + 3] ) ) /
                                                     2 );
                    pixels[target + 3] = 255u;
                }
                else
                {
                    const int           channel = static_cast<int>( m_ChannelView ) - 1;
                    const unsigned char value   = m_Volume.Voxels[source + static_cast<size_t>( channel )];
                    pixels[target + 0]          = value;
                    pixels[target + 1]          = value;
                    pixels[target + 2]          = value;
                    pixels[target + 3]          = 255u;
                }
            }
        }

        return pixels;
    }

    void CloudNoiseVolumePanel::RefreshSlice()
    {
        m_SliceDirty = false;
        m_SliceImage.reset();

        if ( !m_HasVolume || m_Volume.Voxels.empty() )
            return;

        const uint32_t n = m_Volume.Params.Resolution;

        const Core::Formats::Image2DSpecification spec{
             .Tag        = "CloudNoiseSlice",
             .Width      = n,
             .Height     = n,
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Mips       = 1,
             .Data       = BuildSlicePixels(),
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Sample,
        };

        m_SliceImage = Graphic::Image2D::Create( spec, nullptr );
        if ( !m_SliceImage )
        {
            m_Status        = "The slice preview image could not be created on the device.";
            m_StatusIsError = true;
        }
    }

    void CloudNoiseVolumePanel::DrawPreviewSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "Preview" );

        if ( !m_HasVolume )
        {
            ImGui::TextDisabled( "Bake a volume, or open one, to see its slices." );
            return;
        }

        const uint32_t n = m_Volume.Params.Resolution;

        ImGui::Text( "%s - %u^3 RGBA8, %.2f MiB, seed %u", m_SourceName.c_str(), n,
                     static_cast<double>( m_Volume.Voxels.size() ) / ( 1024.0 * 1024.0 ), m_Volume.Params.Seed );

        int axis = static_cast<int>( m_Axis );
        if ( ImGui::Combo( "Axis", &axis, "X\0Y\0Z\0" ) )
        {
            m_Axis       = static_cast<SliceAxis>( axis );
            m_SliceDirty = true;
        }

        if ( ImGui::SliderInt( "Slice", &m_SliceIndex, 0, static_cast<int>( n ) - 1, "%d" ) )
            m_SliceDirty = true;
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "A 3D field has no picture, only slices. Scrub this to see how the structure "
                               "continues through the volume - a field that changes abruptly between "
                               "neighbouring slices will read as noise rather than as cloud." );

        int view = static_cast<int>( m_ChannelView );
        if ( ImGui::Combo( "Channels", &view,
                           "All four\0"
                           "R  Curly-Alligator LF (wispy, coarse)\0"
                           "G  Curly-Alligator HF (wispy, fine)\0"
                           "B  Alligator LF (billowy, coarse)\0"
                           "A  Alligator HF (billowy, fine)\0" ) )
        {
            m_ChannelView = static_cast<ChannelView>( view );
            m_SliceDirty  = true;
        }

        ImGui::SliderInt( "Zoom", &m_PreviewZoom, 1, 6, "%dx" );

        if ( m_SliceDirty )
            RefreshSlice();

        if ( m_SliceImage )
        {
            const float side = static_cast<float>( n * static_cast<uint32_t>( m_PreviewZoom ) );
            SlicePreviewHelper()->Image( m_SliceImage, ImVec2( side, side ) );
        }

        ImGui::TextDisabled( "%s slice %d of %u", AxisName( axis ), m_SliceIndex, n );
    }

    void CloudNoiseVolumePanel::DrawSaveSection()
    {
        Utils::ImGuiUtilities::SectionHeader( "Save" );

        ImGui::BeginDisabled( !m_HasVolume || m_BakeRunning );
        if ( ImGui::Button( "Save As...", ImVec2( 120.0f, 0.0f ) ) )
        {
            std::filesystem::path target =
                 Common::Utils::FileSystem::SaveFileDialog( "Cloud Noise Volume\0*.dcnv\0" );
            if ( !target.empty() )
            {
                if ( target.extension() != Assets::kCloudNoiseVolumeExtension )
                    target.replace_extension( Assets::kCloudNoiseVolumeExtension );

                const auto written = Assets::CloudNoiseVolumeAsset::Save( target, m_Volume );
                if ( !written )
                {
                    m_Status        = "Save failed: " + written.GetError();
                    m_StatusIsError = true;
                }
                else
                {
                    m_SourceName    = target.filename().string();
                    m_Status        = "Saved to " + target.string();
                    m_StatusIsError = false;

                    // Registered straight away so the component's slot lists it without a restart. A tool
                    // whose output only appears after the editor is reopened is a tool nobody iterates in.
                    if ( m_Assets )
                    {
                        auto asset = m_Assets->FindByPath<Assets::CloudNoiseVolumeAsset>( target );
                        if ( asset )
                            asset->Load(); // overwritten in place: re-read so the cached bytes are the new ones
                        else
                            asset = m_Assets->CreateAsset<Assets::CloudNoiseVolumeAsset>(
                                 Assets::AssetPriority::Medium, target );

                        if ( asset )
                        {
                            if ( const auto uploaded =
                                      Runtime::ResourceRegistry::GetCloudNoiseService()->Register( asset );
                                 !uploaded )
                            {
                                m_Status = "Saved, but the volume could not be uploaded: " + uploaded.GetError();
                                m_StatusIsError = true;
                            }
                        }
                    }
                }
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled( "Volumes live in %s", Common::Constants::Path::CLOUD_NOISE_PATH.string().c_str() );
    }
} // namespace Desert::Editor
