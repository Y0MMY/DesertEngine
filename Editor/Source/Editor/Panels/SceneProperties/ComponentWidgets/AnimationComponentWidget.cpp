#include "AnimationComponentWidget.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>

#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>

#include <Engine/Animation/Graph/AnimGraph.hpp>

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
            // Match by bone NAME (tolerant): a Mixamo "without skin" animation has fewer bones than the skinned
            // character (no leaf/end bones), so its signature differs — but it still drives this skeleton.
            std::unordered_set<std::string> boneNames;
            for ( const auto& bone : skeleton.GetBones() )
                boneNames.insert( bone.Name );
            cached    = m_AnimationLibrary->GetForSkeletonBones( boneNames );
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

        RenderAnimGraph( animation, cached );

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }

    void
    AnimationComponentWidget::RenderAnimGraph( ECS::AnimationComponent&                                  animation,
                                               const std::vector<Assets::Asset<Assets::AnimationAsset>>& clips )
    {
        namespace G = Animation::Graph;

        ImGui::Separator();
        if ( !ImGui::CollapsingHeader( "AnimGraph (State Machine)" ) )
            return;

        if ( !animation.Graph )
        {
            ImGui::TextWrapped( "A state machine that picks the clip to play from live parameters "
                                "(e.g. Speed, IsJumping). Drive the parameters from scripts or the controls "
                                "below." );
            if ( ImGui::Button( "Create AnimGraph" ) )
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
            }
            return;
        }

        auto& graph      = *animation.Graph;
        auto* eval       = animation.GraphEvaluator.get();
        bool  structural = false; // set on any change that requires rebuilding the runtime evaluator

        if ( eval && eval->CurrentState() )
            ImGui::Text( "Active State: %s", eval->CurrentState()->Name.c_str() );
        else
            ImGui::TextDisabled( "Active State: (evaluated in Play/Preview)" );

        // ---------------------------------------------------------------- Parameters ----------
        const char* kTypeNames[] = { "Bool", "Int", "Float" };
        if ( ImGui::TreeNodeEx( "Parameters", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            for ( int i = 0; i < static_cast<int>( graph.Parameters.size() ); ++i )
            {
                auto& p = graph.Parameters[i];
                ImGui::PushID( i );

                ImGui::SetNextItemWidth( 110 );
                structural |= Utils::ImGuiUtilities::Property( "##pname", p.Name );
                ImGui::SameLine();
                ImGui::SetNextItemWidth( 65 );
                structural |= ImGui::Combo( "##ptype", &p.Type, kTypeNames, 3 );
                ImGui::SameLine();

                // Live value control (drives the running evaluator; does NOT rebuild it).
                ImGui::SetNextItemWidth( 90 );
                float live = eval ? eval->GetFloat( p.Name ) : p.Default;
                if ( static_cast<G::ParamType>( p.Type ) == G::ParamType::Bool )
                {
                    bool b = live != 0.0f;
                    if ( ImGui::Checkbox( "##pval", &b ) && eval )
                        eval->SetBool( p.Name, b );
                }
                else if ( ImGui::DragFloat( "##pval", &live, 0.05f ) && eval )
                {
                    eval->SetFloat( p.Name, live );
                }
                ImGui::SameLine();
                if ( ImGui::SmallButton( "X" ) )
                {
                    graph.Parameters.erase( graph.Parameters.begin() + i );
                    structural = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if ( ImGui::SmallButton( "+ Parameter" ) )
            {
                graph.Parameters.push_back( { "Param", static_cast<int>( G::ParamType::Float ), 0.0f } );
                structural = true;
            }
            ImGui::TreePop();
        }

        // ---------------------------------------------------------------- States ---------------
        const char* kOpNames[] = { ">", "<", ">=", "<=", "==", "!=", "is true", "is false" };
        if ( ImGui::TreeNodeEx( "States", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            for ( int i = 0; i < static_cast<int>( graph.States.size() ); ++i )
            {
                auto&             s       = graph.States[i];
                const bool        isEntry = ( graph.Entry == s.Name );
                const std::string title =
                     s.Name + ( isEntry ? "  [entry]" : "" ) + "###state" + std::to_string( i );
                ImGui::PushID( 1000 + i );
                if ( ImGui::TreeNodeEx( title.c_str() ) )
                {
                    const std::string oldName = s.Name;
                    if ( Utils::ImGuiUtilities::Property( "Name", s.Name ) )
                    {
                        // Keep references intact when a state is renamed.
                        if ( graph.Entry == oldName )
                            graph.Entry = s.Name;
                        for ( auto& st : graph.States )
                            for ( auto& t : st.Transitions )
                                if ( t.To == oldName )
                                    t.To = s.Name;
                        structural = true;
                    }

                    // Clip picker.
                    const char* clipPreview = s.Clip.empty() ? "Select Clip" : s.Clip.c_str();
                    if ( ImGui::BeginCombo( "Clip", clipPreview ) )
                    {
                        for ( const auto& animAsset : clips )
                        {
                            const auto& name = animAsset->GetClip().AnimationName;
                            if ( ImGui::Selectable( name.c_str(), s.Clip == name ) )
                            {
                                s.Clip     = name;
                                structural = true;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    structural |= ImGui::Checkbox( "Loop", &s.Loop );
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth( 90 );
                    structural |= ImGui::DragFloat( "Speed", &s.Speed, 0.01f, 0.0f, 5.0f );

                    if ( !isEntry && ImGui::SmallButton( "Set as Entry" ) )
                    {
                        graph.Entry = s.Name;
                        structural  = true;
                    }
                    ImGui::SameLine();
                    if ( ImGui::SmallButton( "Delete State" ) )
                    {
                        graph.States.erase( graph.States.begin() + i );
                        structural = true;
                        ImGui::TreePop();
                        ImGui::PopID();
                        break;
                    }

                    // ------------- Transitions of this state -------------
                    ImGui::Separator();
                    ImGui::TextDisabled( "Transitions" );
                    for ( int ti = 0; ti < static_cast<int>( s.Transitions.size() ); ++ti )
                    {
                        auto& t = s.Transitions[ti];
                        ImGui::PushID( 5000 + ti );

                        const std::string toLabel = t.To.empty() ? std::string( "-> Target" ) : "-> " + t.To;
                        ImGui::SetNextItemWidth( 150 );
                        if ( ImGui::BeginCombo( "##to", toLabel.c_str() ) )
                        {
                            for ( const auto& other : graph.States )
                                if ( other.Name != s.Name &&
                                     ImGui::Selectable( other.Name.c_str(), t.To == other.Name ) )
                                {
                                    t.To       = other.Name;
                                    structural = true;
                                }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth( 80 );
                        structural |= ImGui::DragFloat( "Blend", &t.Blend, 0.01f, 0.0f, 2.0f );
                        ImGui::SameLine();
                        if ( ImGui::SmallButton( "Del" ) )
                        {
                            s.Transitions.erase( s.Transitions.begin() + ti );
                            structural = true;
                            ImGui::PopID();
                            break;
                        }

                        structural |= ImGui::Checkbox( "Exit time", &t.HasExitTime );
                        if ( t.HasExitTime )
                        {
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth( 90 );
                            structural |= ImGui::DragFloat( "##exit", &t.ExitTime, 0.01f, 0.0f, 1.0f );
                        }

                        // Conditions (AND).
                        for ( int ci = 0; ci < static_cast<int>( t.Conditions.size() ); ++ci )
                        {
                            auto& c = t.Conditions[ci];
                            ImGui::PushID( 9000 + ci );
                            ImGui::SetNextItemWidth( 110 );
                            const char* cparam = c.Parameter.empty() ? "param" : c.Parameter.c_str();
                            if ( ImGui::BeginCombo( "##cparam", cparam ) )
                            {
                                for ( const auto& p : graph.Parameters )
                                    if ( ImGui::Selectable( p.Name.c_str(), c.Parameter == p.Name ) )
                                    {
                                        c.Parameter = p.Name;
                                        structural  = true;
                                    }
                                ImGui::EndCombo();
                            }
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth( 75 );
                            structural |= ImGui::Combo( "##cop", &c.Op, kOpNames, IM_ARRAYSIZE( kOpNames ) );
                            const auto op = static_cast<G::CompareOp>( c.Op );
                            if ( op != G::CompareOp::IsTrue && op != G::CompareOp::IsFalse )
                            {
                                ImGui::SameLine();
                                ImGui::SetNextItemWidth( 70 );
                                structural |= ImGui::DragFloat( "##cval", &c.Value, 0.05f );
                            }
                            ImGui::SameLine();
                            if ( ImGui::SmallButton( "x" ) )
                            {
                                t.Conditions.erase( t.Conditions.begin() + ci );
                                structural = true;
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }
                        if ( ImGui::SmallButton( "+ Condition" ) )
                        {
                            t.Conditions.push_back( {} );
                            structural = true;
                        }
                        ImGui::Separator();
                        ImGui::PopID();
                    }
                    if ( ImGui::SmallButton( "+ Transition" ) )
                    {
                        s.Transitions.push_back( {} );
                        structural = true;
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if ( ImGui::SmallButton( "+ State" ) )
            {
                G::State ns;
                ns.Name = "State_" + std::to_string( graph.States.size() );
                graph.States.push_back( ns );
                structural = true;
            }
            ImGui::TreePop();
        }

        if ( ImGui::Button( "Remove AnimGraph" ) )
        {
            animation.Graph.reset();
            animation.GraphEvaluator.reset();
            return;
        }

        // Any structural edit bumps the revision so AnimationECSSystem rebuilds the runtime evaluator.
        if ( structural )
            animation.GraphRevision++;
    }

    DESERT_REGISTER_CUSTOM_COMPONENT(
         ECS::AnimationComponent, "Animation", false,
         ( []( ECS::Entity& e, ::Desert::Core::Scene* s, const ComponentEditContext& ctx )
           { AnimationComponentWidget( ctx.AssetMgr(), ctx.AnimationLibrary ).Render( e, s ); } ) )
} // namespace Desert::Editor
