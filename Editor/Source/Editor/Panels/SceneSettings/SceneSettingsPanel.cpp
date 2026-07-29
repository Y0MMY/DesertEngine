#include "SceneSettingsPanel.hpp"

#include <Engine/Core/Scene.hpp>
#include <Engine/Core/SceneSettings.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/Image.hpp>

#include <ImGui/imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    SceneSettingsPanel::SceneSettingsPanel( std::shared_ptr<::Desert::Core::Scene> scene )
         : IPanel( "Scene Settings" ), m_Scene( std::move( scene ) ),
           m_UIHelper( std::make_unique<UI::UIHelper>() )
    {
        m_UIHelper->Init();
    }

    void SceneSettingsPanel::OnUIRender()
    {
        if ( !m_Scene )
        {
            ImGui::TextDisabled( "No active scene" );
            return;
        }

        Core::SceneSettings& s = m_Scene->GetSettings();

        if ( ImGui::CollapsingHeader( "Anti-Aliasing", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            const char* items[] = { "None", "FXAA", "SMAA" };
            int         current = static_cast<int>( s.AA );
            if ( ImGui::Combo( "Mode", &current, items, IM_ARRAYSIZE( items ) ) )
                s.AA = static_cast<Core::AntiAliasingMode>( current );
            ImGui::TextDisabled( "TAA/DLSS need motion vectors (deferred)." );
        }

        if ( ImGui::CollapsingHeader( "Post-Processing", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            // Live — consumed by the tonemap pass.
            ImGui::Checkbox( "Auto Exposure (eye adaptation)", &s.AutoExposure );
            ImGui::BeginDisabled( !s.AutoExposure );
            ImGui::SliderFloat( "Exposure Key", &s.AutoExposureKey, 0.01f, 1.0f );
            ImGui::SliderFloat( "Adapt Speed", &s.AutoExposureSpeed, 0.1f, 10.0f );
            ImGui::SliderFloat( "Min Luma", &s.AutoExposureMin, 0.001f, 1.0f, "%.3f" );
            ImGui::SliderFloat( "Max Luma", &s.AutoExposureMax, 1.0f, 32.0f );
            ImGui::EndDisabled();

            ImGui::BeginDisabled( s.AutoExposure );
            ImGui::SliderFloat( "Exposure (manual)", &s.Exposure, 0.0f, 5.0f );
            ImGui::EndDisabled();
            ImGui::SliderFloat( "Gamma", &s.Gamma, 1.0f, 3.0f );

            ImGui::Spacing();
            ImGui::Checkbox( "Bloom", &s.EnableBloom );
            ImGui::SliderFloat( "Bloom Threshold", &s.BloomThreshold, 0.0f, 5.0f );
            ImGui::SliderFloat( "Bloom Intensity", &s.BloomIntensity, 0.0f, 3.0f );
            ImGui::SliderFloat( "Lens Dispersion", &s.LensDispersion, 0.0f, 3.0f );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Chromatic rainbow fringe around bright sources (glare). Needs Bloom on." );
        }

        if ( ImGui::CollapsingHeader( "Textures", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            // Global sampler filter — applies live (samplers are recreated when this changes).
            const char* items[]  = { "Nearest", "Bilinear", "Trilinear", "Anisotropic" };
            int         current  = static_cast<int>( s.TextureFilterMode );
            if ( ImGui::Combo( "Filter", &current, items, IM_ARRAYSIZE( items ) ) )
                s.TextureFilterMode = static_cast<Core::TextureFilter>( current );

            // Anisotropy level — only meaningful in Anisotropic mode.
            if ( s.TextureFilterMode == Core::TextureFilter::Anisotropic )
            {
                const char* levels[]  = { "1x", "2x", "4x", "8x", "16x" };
                const int   values[]  = { 1, 2, 4, 8, 16 };
                int         levelIdx  = 3; // default 8x
                for ( int i = 0; i < IM_ARRAYSIZE( values ); ++i )
                    if ( values[i] == s.Anisotropy )
                        levelIdx = i;
                if ( ImGui::Combo( "Anisotropy", &levelIdx, levels, IM_ARRAYSIZE( levels ) ) )
                    s.Anisotropy = values[levelIdx];
            }
        }

        if ( ImGui::CollapsingHeader( "Environment" ) )
        {
            // Live — cascaded directional shadow maps.
            ImGui::Checkbox( "Shadows", &s.EnableShadows );
            ImGui::SliderFloat( "Shadow Bias", &s.ShadowBias, 0.0f, 0.02f, "%.4f" );
            ImGui::SliderFloat( "Cascade Split Lambda", &s.CascadeSplitLambda, 0.0f, 1.0f );

            ImGui::Spacing();
            ImGui::TextDisabled( "Procedural Sky + skybox intensity moved to the Skybox component (Details)." );
        }

        if ( ImGui::CollapsingHeader( "Debug" ) )
        {
            // Live — infinite editor ground grid.
            ImGui::Checkbox( "Show Grid", &s.ShowGrid );

            // Live — selects the line-polygon mesh pipeline.
            ImGui::Checkbox( "Wireframe", &s.WireframeMode );

            // Live — shadow debug: Off / raw shadow factor (grayscale) / cascade tint. Lets the CSM
            // shadow maps be verified independently of scene lighting (IBL isn't shadowed).
            {
                const char* modes[] = { "Off", "Shadow Factor", "Cascades" };
                int         cur     = static_cast<int>( s.ShadowDebug );
                if ( ImGui::Combo( "Shadow Debug", &cur, modes, IM_ARRAYSIZE( modes ) ) )
                    s.ShadowDebug = static_cast<Core::ShadowDebugMode>( cur );
            }

            // CSM cascade depth maps (R32F light-space depth, near→far cascades).
            if ( auto* sr = m_Scene->GetSceneRenderer() )
            {
                const uint32_t count = sr->GetShadowCascadeCount();
                ImGui::TextDisabled( "CSM cascade depth maps (near -> far):" );
                constexpr float kThumb = 96.0f;
                for ( uint32_t c = 0; c < count; ++c )
                {
                    if ( auto img = sr->GetShadowCascadeImage( c ) )
                        m_UIHelper->Image( img, ImVec2( kThumb, kThumb ) );
                    if ( ( c % 3 ) != 2 && c + 1 < count )
                        ImGui::SameLine();
                }
            }

            ImGui::Spacing();
            ImGui::Checkbox( "Show Bounding Boxes", &s.ShowBoundingBoxes );
            ImGui::BeginDisabled( !s.ShowBoundingBoxes );
            ImGui::ColorEdit3( "BB Color", glm::value_ptr( s.BoundingBoxColor ) );
            ImGui::SliderFloat( "BB Line Width", &s.BoundingBoxLineWidth, 1.0f, 10.0f, "%.1f" );
            ImGui::EndDisabled();
            ImGui::Checkbox( "Show Colliders", &s.ShowColliders );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Draw green wireframes for physics colliders (Box/Sphere/Capsule)." );
            ImGui::Checkbox( "Show Normals", &s.ShowNormals );
            ImGui::Checkbox( "Light Debug", &s.LightingDebug );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Tint surfaces by which light reaches them (a distinct color per source;\n"
                                   "brightness = light strength). Unlit areas are black." );
        }

        if ( ImGui::CollapsingHeader( "Rendering", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            const char* paths[] = { "Forward", "Deferred" };
            int         cur     = static_cast<int>( s.RenderingPath );
            if ( ImGui::Combo( "Render Path", &cur, paths, IM_ARRAYSIZE( paths ) ) )
                s.RenderingPath = static_cast<Core::RenderPath>( cur );

            // Deferred G-buffer debug view (UE-style buffer visualization) — only meaningful in Deferred.
            ImGui::BeginDisabled( s.RenderingPath != Core::RenderPath::Deferred );
            const char* dbg[]  = { "Lit", "Albedo", "Normal", "Metallic", "Roughness", "AO" };
            int         dbgCur = static_cast<int>( s.DeferredDebug );
            if ( ImGui::Combo( "Deferred Debug", &dbgCur, dbg, IM_ARRAYSIZE( dbg ) ) )
                s.DeferredDebug = static_cast<Core::DeferredDebugMode>( dbgCur );
            ImGui::Checkbox( "Enable SSAO", &s.EnableSSAO );
            ImGui::Checkbox( "Enable SSGI", &s.EnableSSGI );
            ImGui::EndDisabled();
            ImGui::TextDisabled( "Deferred: static meshes only, directional light (WIP)." );
        }

        // Shared wind — a scene-global environment force (not owned by the Skybox). Drives grass + cloud
        // drift now; hair/cloth later. Consumed via SceneRenderer::GetWind().
        if ( ImGui::CollapsingHeader( "Wind", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::SliderFloat( "Direction (deg)", &s.WindDirection, 0.0f, 360.0f );
            ImGui::SliderFloat( "Strength", &s.WindStrength, 0.0f, 1.0f );
            ImGui::SliderFloat( "Turbulence", &s.WindTurbulence, 0.0f, 3.0f );
            ImGui::TextDisabled( "Shared: one direction moves grass AND clouds." );
        }

        // Opt-in day/night cycle: drives the sun (directional light) from the hour; the sky follows its
        // elevation. Default off leaves any hand-placed sun untouched.
        if ( ImGui::CollapsingHeader( "Time of Day", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Checkbox( "Enable Day/Night", &s.EnableDayNight );
            ImGui::BeginDisabled( !s.EnableDayNight );
            ImGui::SliderFloat( "Time of Day (h)", &s.TimeOfDay, 0.0f, 24.0f );
            ImGui::SliderFloat( "Day Length (s)", &s.DayLengthSeconds, 0.0f, 600.0f );
            ImGui::SliderFloat( "Sun Peak Intensity", &s.SunPeakIntensity, 0.0f, 10.0f );
            ImGui::EndDisabled();
            ImGui::TextDisabled( "Day Length 0 = frozen (scrub the hour by hand)." );
        }
    }
} // namespace Desert::Editor
