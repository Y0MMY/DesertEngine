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

    namespace
    {
        // Marks a group whose backing render feature isn't implemented yet, so the disabled controls
        // below read as "intentionally inert" rather than "broken".
        void NotImplementedNote( const char* feature )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.85f, 0.65f, 0.25f, 1.0f ) );
            ImGui::Text( "%s not implemented yet", feature );
            ImGui::PopStyleColor();
        }
    } // namespace

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

        if ( ImGui::CollapsingHeader( "Outline", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Checkbox( "Enable Outline", &s.EnableOutline );
            ImGui::ColorEdit3( "Color", glm::value_ptr( s.OutlineColor ) );
            ImGui::SliderFloat( "Width (px)", &s.OutlineWidth, 0.0f, 20.0f );
            ImGui::SliderFloat( "Smoothness", &s.OutlineSmoothness, 0.0f, 10.0f );
        }

        if ( ImGui::CollapsingHeader( "Anti-Aliasing", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            const char* items[] = { "None", "FXAA", "SMAA" };
            int         current = static_cast<int>( s.AA );
            if ( ImGui::Combo( "Mode", &current, items, IM_ARRAYSIZE( items ) ) )
                s.AA = static_cast<Core::AntiAliasingMode>( current );
            ImGui::TextDisabled( "SMAA wiring is WIP; TAA/DLSS need motion vectors (deferred)." );
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

        // The settings below have no renderer backing yet (no shadow / IBL-intensity / debug-viz systems).
        // Shown disabled so they read honestly rather than appearing to do nothing.
        if ( ImGui::CollapsingHeader( "Environment" ) )
        {
            // Live — cascaded directional shadow maps.
            ImGui::Checkbox( "Shadows", &s.EnableShadows );
            ImGui::SliderFloat( "Shadow Bias", &s.ShadowBias, 0.0f, 0.02f, "%.4f" );
            ImGui::SliderFloat( "Cascade Split Lambda", &s.CascadeSplitLambda, 0.0f, 1.0f );

            ImGui::Spacing();
            ImGui::TextDisabled( "Procedural Sky moved to the Skybox component (Details panel)." );

            ImGui::Spacing();
            NotImplementedNote( "environment-map intensity / skybox LOD" );
            ImGui::BeginDisabled();
            ImGui::SliderFloat( "Env Map Intensity", &s.EnvironmentMapIntensity, 0.0f, 5.0f );
            ImGui::SliderFloat( "Skybox LOD", &s.SkyboxLOD, 0.0f, 10.0f );
            ImGui::EndDisabled();
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
            ImGui::Checkbox( "Show Normals", &s.ShowNormals );
            ImGui::Checkbox( "Light Debug", &s.LightingDebug );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Tint surfaces by which light reaches them (a distinct color per source;\n"
                                   "brightness = light strength). Unlit areas are black." );
        }
    }
} // namespace Desert::Editor
