#include "AnimationComponentWidget.hpp"

#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    AnimationComponentWidget::AnimationComponentWidget( const Assets::AssetManager*        assetManager,
                                                        const Animation::AnimationLibrary* animationLibrary )
         : ComponentWidget( "Animation" ), m_AnimationLibrary( animationLibrary )
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

        // --- Clip Selection ---
        if ( animation.Animator )
        {
            const auto&    skeleton = animation.Animator->GetSkeleton();
            const uint64_t sig      = skeleton.GetSignature();

            static std::vector<Assets::Asset<Assets::AnimationAsset>> cached;
            static uint64_t                                           cachedSig = 0;

            if ( cachedSig != sig )
            {
                cached    = m_AnimationLibrary->GetBySkeleton( sig );
                cachedSig = sig;
            }

            const char* preview = animation.CurrentClip.empty() ? "Select Clip" : animation.CurrentClip.c_str();

            if ( ImGui::BeginCombo( "##ClipSelect", preview ) )
            {
                for ( const auto& animAsset : cached )
                {
                    const auto&        clip = animAsset->GetClip();
                    const std::string& name = clip.AnimationName;

                    bool selected = ( animation.CurrentClip == name );

                    if ( ImGui::Selectable( name.c_str(), selected ) )
                    {
                        animation.CurrentClip = name;
                        animation.Animator->Play( clip );
                    }

                    if ( selected )
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }
        }

        // --- Timeline ---
        const auto& animator = animation.Animator;

        if ( animator && animator->GetDuration() > 0.0f )
        {
            float currentTime = animator->GetCurrentTime();
            float duration    = animator->GetDuration();

            ImGui::TextUnformatted( "Timeline" );
            ImGui::SliderFloat( "##Timeline", &currentTime, 0.0f, duration, "%.3f" );

            if ( ImGui::IsItemActive() )
            {
                animation.Playing = false;
                animator->SetTime( currentTime );
            }
        }

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
            ImGui::Text( "%.3f", animator->GetCurrentTime() );
            ImGui::NextColumn();

            ImGui::TextUnformatted( "Duration" );
            ImGui::NextColumn();
            ImGui::Text( "%.3f", animator->GetDuration() );
            ImGui::NextColumn();

            ImGui::Columns( 1 );
            ImGui::TreePop();
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }
} // namespace Desert::Editor
