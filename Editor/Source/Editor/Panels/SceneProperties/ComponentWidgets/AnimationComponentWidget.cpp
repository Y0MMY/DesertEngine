#include "AnimationComponentWidget.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>

#include <ImGui/imgui.h>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>

#include <Engine/Animation/Graph/AnimGraph.hpp>

#include <Editor/Panels/Animation/AnimGraphPanel.hpp>
#include <Editor/Core/PanelRequests.hpp>
#include <Editor/Panels/Stubs/SequencerPanel.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    AnimationComponentWidget::AnimationComponentWidget( const Assets::AssetManager*        assetManager,
                                                        const Animation::AnimationLibrary* animationLibrary )
         : ComponentWidget( "Animation" ), m_AnimationLibrary( animationLibrary )
    {
    }

    void AnimationComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        auto& animation = entity.GetComponent<ECS::AnimationComponent>();

        Utils::ImGuiUtilities::PushID();

        if ( !animation.Animator )
        {
            ImGui::TextDisabled( "No animator assigned" );
            Utils::ImGuiUtilities::PopID();
            return;
        }

        auto* animator = animation.Animator.get();

        // ============================================================
        // BASIC CONTROLS
        // ============================================================

        // Playback, on the panel's shared rows.
        Utils::ImGuiUtilities::ResetPropertyRows();

        Utils::ImGuiUtilities::BeginPropertyRow( "Playing" );
        ImGui::Checkbox( "##playing", &animation.Playing );
        Utils::ImGuiUtilities::EndPropertyRow();

        Utils::ImGuiUtilities::BeginPropertyRow( "Loop" );
        ImGui::Checkbox( "##loop", &animation.Loop );
        Utils::ImGuiUtilities::EndPropertyRow();

        Utils::ImGuiUtilities::BeginPropertyRow( "Speed" );
        ImGui::DragFloat( "##speed", &animation.PlaybackSpeed, 0.01f, 0.0f, 3.0f, "%.2fx" );
        Utils::ImGuiUtilities::EndPropertyRow();

        // Root motion: the clip's hips displacement drives the ENTITY instead of sliding under it. The
        // component has carried this flag all along with nothing in the editor able to set it.
        Utils::ImGuiUtilities::BeginPropertyRow(
             "Root Motion", "Apply the clip's root/hips displacement to the entity's transform instead of "
                            "animating in place" );
        ImGui::Checkbox( "##rootmotion", &animation.EnableRootMotion );
        Utils::ImGuiUtilities::EndPropertyRow();

        // ============================================================
        // CLIP SELECTION
        // ============================================================

        const auto&    skeleton = animator->GetSkeleton();
        const uint64_t sig      = skeleton.GetSignature();

        static std::vector<Assets::Asset<Assets::AnimationAsset>> cached;
        static uint64_t                                           cachedSig = 0;

        if ( cachedSig != sig )
        {
            // Match by bone NAME (tolerant): a Mixamo "without skin" animation has fewer bones than the skinned
            // character (no leaf/end bones), so its signature differs — but it still drives this skeleton.
            std::unordered_set<std::string> boneNames;
            for ( const auto& bone : skeleton.GetBones() )
                boneNames.insert( bone.Name );
            cached    = m_AnimationLibrary->GetForSkeletonBones( boneNames );
            cachedSig = sig;
        }

        const char* preview = animation.CurrentClip.empty() ? "Select Clip" : animation.CurrentClip.c_str();

        Utils::ImGuiUtilities::BeginPropertyRow( "Clip" );
        if ( ImGui::BeginCombo( "##ClipSelect", preview ) )
        {
            for ( const auto& animAsset : cached )
            {
                const auto& clip     = animAsset->GetClip();
                bool        selected = ( animation.CurrentClip == clip.AnimationName );

                if ( ImGui::Selectable( clip.AnimationName.c_str(), selected ) )
                {
                    animation.CurrentClip = clip.AnimationName;
                    animator->Play( clip );
                }

                if ( selected )
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }
        Utils::ImGuiUtilities::EndPropertyRow();

        // ============================================================
        // TIMELINE
        // ============================================================

        if ( animator->GetDuration() > 0.0f )
        {
            float currentTime = animator->GetCurrentTime();
            float duration    = animator->GetDuration();

            Utils::ImGuiUtilities::BeginPropertyRow( "Time" );
            if ( ImGui::SliderFloat( "##Timeline", &currentTime, 0.0f, duration, "%.2f s" ) )
            {
                animation.Playing = false;
                animator->SetTime( currentTime );
            }
            Utils::ImGuiUtilities::EndPropertyRow();
        }

        // Keyframe/track editing lives in the Sequencer (a proper timeline); Details stays focused on playback
        // + the AnimGraph summary.
        // The authoring tools, opened EXPLICITLY. None of these panels shows up on selection any more:
        // clicking a character is not a request to author its animation.
        ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
        const float half = ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x ) * 0.5f;
        if ( ImGui::Button( ICON_MDI_CHART_TIMELINE "  Sequencer", ImVec2( half, 0.0f ) ) )
            SequencerPanel::RequestOpen();
        Utils::ImGuiUtilities::Tooltip( "Author clips on a timeline (keyframes per bone)" );
        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_LAYERS "  Anim Layers", ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
            Core::PanelRequests::Open( "Anim Layers" );
        Utils::ImGuiUtilities::Tooltip( "Additive layers on top of the base clip" );

        RenderAnimGraph( animation, cached );

        Utils::ImGuiUtilities::PopID();
    }

    // Compact AnimGraph summary in Details: create / active-badge / counts + an "Open in Anim Graph" button.
    // Full authoring (states / transitions / parameters) lives in the visual Anim Graph node panel — no triple
    // UI.
    void
    AnimationComponentWidget::RenderAnimGraph( ECS::AnimationComponent&                                  animation,
                                               const std::vector<Assets::Asset<Assets::AnimationAsset>>& clips )
    {
        namespace G = Animation::Graph;

        ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
        if ( !Utils::ImGuiUtilities::SectionHeader( ICON_MDI_STATE_MACHINE "  AnimGraph (State Machine)" ) )
            return;

        ImGui::Indent( 6.0f );
        ImGui::Dummy( ImVec2( 0.0f, 2.0f ) );

        if ( !animation.Graph )
        {
            ImGui::PushTextWrapPos( 0.0f );
            ImGui::TextDisabled( "A state machine that picks the clip from live parameters "
                                 "(e.g. Speed, IsJumping). Author it visually in the Anim Graph panel." );
            ImGui::PopTextWrapPos();
            ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
            if ( Utils::ImGuiUtilities::AccentButton( ICON_MDI_PLUS_CIRCLE "  Create AnimGraph", 28.0f ) )
            {
                auto     graph = std::make_shared<G::AnimGraph>();
                G::State idle;
                idle.Name = "Idle";
                if ( !clips.empty() )
                    idle.Clip = clips.front()->GetClip().AnimationName;
                graph->States.push_back( idle );
                graph->Entry    = "Idle";
                animation.Graph = graph;
                animation.GraphRevision++;
                AnimGraphPanel::RequestOpen(); // jump straight into the visual editor
            }
            ImGui::Unindent( 6.0f );
            return;
        }

        auto* eval = animation.GraphEvaluator.get();

        // Active-state badge.
        if ( eval && eval->CurrentState() )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.2f, 1.0f ), ICON_MDI_PLAY );
            ImGui::SameLine( 0.0f, 6.0f );
            ImGui::Text( "Active: %s", eval->CurrentState()->Name.c_str() );
        }
        else
        {
            ImGui::TextDisabled( ICON_MDI_PAUSE " Active state shows in Play/Preview" );
        }
        ImGui::TextDisabled( "%zu states  \xc2\xb7  %zu parameters", animation.Graph->States.size(),
                             animation.Graph->Parameters.size() );

        ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
        if ( Utils::ImGuiUtilities::AccentButton( ICON_MDI_STATE_MACHINE "  Open in Anim Graph", 28.0f ) )
            AnimGraphPanel::RequestOpen();

        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.46f, 0.19f, 0.19f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.62f, 0.24f, 0.24f, 1.0f ) );
        const bool remove = ImGui::Button( ICON_MDI_DELETE "  Remove AnimGraph" );
        ImGui::PopStyleColor( 2 );
        if ( remove )
        {
            animation.Graph.reset();
            animation.GraphEvaluator.reset();
        }

        ImGui::Unindent( 6.0f );
    }

    DESERT_REGISTER_CUSTOM_COMPONENT(
         ECS::AnimationComponent, "Animation", false,
         ( []( ECS::Entity& e, ::Desert::Core::Scene* s, const ComponentEditContext& ctx )
           { AnimationComponentWidget( ctx.AssetMgr(), ctx.AnimationLibrary ).Render( e, s ); } ) )
} // namespace Desert::Editor
