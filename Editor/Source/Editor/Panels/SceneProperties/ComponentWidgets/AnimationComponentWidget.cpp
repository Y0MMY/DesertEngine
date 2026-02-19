#include "AnimationComponentWidget.hpp"

#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    AnimationComponentWidget::AnimationComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager )
         : ComponentWidget( "Animation" )
    {
    }

    void AnimationComponentWidget::Render( ECS::Entity& entity )
    {
        auto& animation = entity.GetComponent<ECS::AnimationComponent>();

        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        if ( !animation.Animator )
        {
            ImGui::TextDisabled( "No animator assigned" );
            ImGui::PopStyleVar();
            Utils::ImGuiUtilities::PopID();
            return;
        }

        ImGui::Columns( 2 );
        ImGui::Separator();

        // --- Playing ---
        ImGui::TextUnformatted( "Playing" );
        ImGui::NextColumn();
        ImGui::Checkbox( "##Playing", &animation.Playing );
        ImGui::NextColumn();

        // --- Loop ---
        ImGui::TextUnformatted( "Loop" );
        ImGui::NextColumn();
        ImGui::Checkbox( "##Loop", &animation.Loop );
        ImGui::NextColumn();

        // --- Playback Speed ---
        ImGui::TextUnformatted( "Speed" );
        ImGui::NextColumn();
        ImGui::SliderFloat( "##Speed", &animation.PlaybackSpeed, 0.1f, 3.0f, "%.2fx" );
        ImGui::NextColumn();

        ImGui::Columns( 1 );
        ImGui::Separator();

        // --- Debug info ---
        if ( ImGui::TreeNodeEx( "Animation Info", ImGuiTreeNodeFlags_Framed ) )
        {
            ImGui::Columns( 2 );

            ImGui::TextUnformatted( "Current Clip" );
            ImGui::NextColumn();
            ImGui::TextUnformatted( animation.CurrentClip.empty() ? "None" : animation.CurrentClip.c_str() );
            ImGui::NextColumn();

            const auto& animator = animation.Animator;

            ImGui::TextUnformatted( "Time" );
            ImGui::NextColumn();
          //  ImGui::Text( "%.3f", animator->GetCurrentTime() );
            ImGui::NextColumn();

            ImGui::TextUnformatted( "Duration" );
            ImGui::NextColumn();
         //   ImGui::Text( "%.3f", animator->GetDuration() );
            ImGui::NextColumn();

            ImGui::Columns( 1 );
            ImGui::TreePop();
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }
} // namespace Desert::Editor
