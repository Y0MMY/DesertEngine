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

    void AnimationComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
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

        auto* animator = animation.Animator.get();

        // ============================================================
        // BASIC CONTROLS
        // ============================================================

        ImGui::Columns( 2 );
        ImGui::Separator();

        Utils::ImGuiUtilities::Property( "Playing", animation.Playing );
        Utils::ImGuiUtilities::Property( "Loop", animation.Loop );
        Utils::ImGuiUtilities::Property( "Speed", animation.PlaybackSpeed, 0.1f, 3.0f, 0.01f,
                                         Utils::ImGuiUtilities::PropertyFlag::DragValue );

        ImGui::Columns( 1 );
        ImGui::Separator();

        // ============================================================
        // CLIP SELECTION
        // ============================================================

        const auto&    skeleton = animator->GetSkeleton();
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

        // ============================================================
        // TIMELINE
        // ============================================================

        if ( animator->GetDuration() > 0.0f )
        {
            float currentTime = animator->GetCurrentTime();
            float duration    = animator->GetDuration();

            ImGui::TextUnformatted( "Timeline" );

            if ( ImGui::SliderFloat( "##Timeline", &currentTime, 0.0f, duration, "%.3f" ) )
            {
                animation.Playing = false;
                animator->SetTime( currentTime );
            }
        }

        // ============================================================
        // CLIP EDITOR
        // ============================================================

        auto* clip = const_cast<Animation::AnimationClip*>( animator->GetCurrentClip() );

        if ( clip && ImGui::TreeNodeEx( "Clip Editor", ImGuiTreeNodeFlags_Framed ) )
        {
            bool changed = false;

            ImGui::Columns( 2 );

            changed |= Utils::ImGuiUtilities::Property( "Name", clip->AnimationName );
            changed |= Utils::ImGuiUtilities::Property( "Duration", clip->Duration, 0.0f, 100.0f, 0.01f,
                                                        Utils::ImGuiUtilities::PropertyFlag::DragValue );
            changed |= Utils::ImGuiUtilities::Property( "TPS", clip->TicksPerSecond, 1.0f, 200.0f, 0.1f,
                                                        Utils::ImGuiUtilities::PropertyFlag::DragValue );

            ImGui::Columns( 1 );
            ImGui::Separator();

            // ========================================================
            // TRACKS
            // ========================================================

            for ( size_t i = 0; i < clip->Tracks.size(); ++i )
            {
                auto& track = clip->Tracks[i];

                if ( ImGui::TreeNode( ( track.BoneName + "##track" ).c_str() ) )
                {
                    // ---------------- POSITION ----------------
                    if ( ImGui::TreeNode( "Position" ) )
                    {
                        for ( size_t k = 0; k < track.PositionKeys.size(); ++k )
                        {
                            auto& key = track.PositionKeys[k];
                            ImGui::PushID( (int)k );

                            changed |= ImGui::DragFloat( "Time", &key.Time, 0.01f );
                            changed |= Utils::ImGuiUtilities::Property( "Value", key.Position );

                            if ( ImGui::Button( "Delete" ) )
                            {
                                track.PositionKeys.erase( track.PositionKeys.begin() + k );
                                ImGui::PopID();
                                break;
                            }

                            ImGui::Separator();
                            ImGui::PopID();
                        }

                        if ( ImGui::Button( "Add Position Key" ) )
                        {
                            track.PositionKeys.push_back( { animator->GetCurrentTime(), glm::vec3( 0.0f ) } );

                            std::sort( track.PositionKeys.begin(), track.PositionKeys.end() );
                            changed = true;
                        }

                        ImGui::TreePop();
                    }

                    // ---------------- ROTATION ----------------
                    if ( ImGui::TreeNode( "Rotation" ) )
                    {
                        for ( size_t k = 0; k < track.RotationKeys.size(); ++k )
                        {
                            auto& key = track.RotationKeys[k];
                            ImGui::PushID( (int)k );

                            changed |= ImGui::DragFloat( "Time", &key.Time, 0.01f );

                            glm::vec3 euler = glm::degrees( glm::eulerAngles( key.Rotation ) );

                            if ( Utils::ImGuiUtilities::Property( "Rotation", euler ) )
                            {
                                key.Rotation = glm::quat( glm::radians( euler ) );
                                changed      = true;
                            }

                            if ( ImGui::Button( "Delete" ) )
                            {
                                track.RotationKeys.erase( track.RotationKeys.begin() + k );
                                ImGui::PopID();
                                break;
                            }

                            ImGui::Separator();
                            ImGui::PopID();
                        }

                        if ( ImGui::Button( "Add Rotation Key" ) )
                        {
                            track.RotationKeys.push_back(
                                 { animator->GetCurrentTime(), glm::quat( 1, 0, 0, 0 ) } );

                            std::sort( track.RotationKeys.begin(), track.RotationKeys.end() );
                            changed = true;
                        }

                        ImGui::TreePop();
                    }

                    // ---------------- SCALE ----------------
                    if ( ImGui::TreeNode( "Scale" ) )
                    {
                        for ( size_t k = 0; k < track.ScaleKeys.size(); ++k )
                        {
                            auto& key = track.ScaleKeys[k];
                            ImGui::PushID( (int)k );

                            changed |= ImGui::DragFloat( "Time", &key.Time, 0.01f );
                            changed |= Utils::ImGuiUtilities::Property( "Value", key.Scale );

                            if ( ImGui::Button( "Delete" ) )
                            {
                                track.ScaleKeys.erase( track.ScaleKeys.begin() + k );
                                ImGui::PopID();
                                break;
                            }

                            ImGui::Separator();
                            ImGui::PopID();
                        }

                        if ( ImGui::Button( "Add Scale Key" ) )
                        {
                            track.ScaleKeys.push_back( { animator->GetCurrentTime(), glm::vec3( 1.0f ) } );

                            std::sort( track.ScaleKeys.begin(), track.ScaleKeys.end() );
                            changed = true;
                        }

                        ImGui::TreePop();
                    }

                    ImGui::TreePop();
                }
            }

            // ========================================================
            //  LIVE UPDATE
            // ========================================================

            if ( changed )
            {
                animator->SetTime( animator->GetCurrentTime() );
            }

            ImGui::TreePop();
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }
} // namespace Desert::Editor
