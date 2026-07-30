#include "SequencerPanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/SkeletonEditMode.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Animation/Animator.hpp>
#include <Engine/Animation/AnimationLibrary.hpp>
#include <Engine/Animation/Skeleton.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/AnimationAsset.hpp>

#include <Engine/Animation/AnimationClip.hpp>
#include <Engine/Assets/Serialization/Animation.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <rflcpp/rfl/json.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    SequencerPanel::SequencerPanel( std::shared_ptr<::Desert::Core::Scene> scene,
                                    Animation::AnimationLibrary* library, Assets::AssetManager* assetManager )
         : IPanel( "Sequencer", /*showPanel=*/false ), m_Scene( std::move( scene ) ), m_Library( library ),
           m_AssetManager( assetManager )
    {
    }

    namespace
    {
        bool s_OpenRequested = false;

        // A hoverable "(?)" that shows a wrapped explanation — used to demystify advanced controls.
        void HelpMarker( const char* text )
        {
            ImGui::TextDisabled( ICON_MDI_HELP_CIRCLE_OUTLINE );
            if ( ImGui::IsItemHovered() )
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos( 340.0f );
                ImGui::TextUnformatted( text );
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    } // namespace

    void SequencerPanel::KeyBonePose( Animation::AnimationClip* clip, const Animation::Skeleton& skeleton,
                                      int boneIndex, float time )
    {
        if ( !clip || boneIndex < 0 || boneIndex >= static_cast<int>( skeleton.GetBones().size() ) )
            return;
        const auto& bone = skeleton.GetBones()[boneIndex];

        glm::vec3 t, s, skew;
        glm::vec4 persp;
        glm::quat r;
        glm::decompose( bone.LocalBindTransform, s, r, t, skew, persp );

        // Find (or create) the track for this bone.
        Animation::BoneTrack* track = nullptr;
        for ( auto& tr : clip->Tracks )
            if ( tr.BoneName == bone.Name )
            {
                track = &tr;
                break;
            }
        if ( !track )
        {
            Animation::BoneTrack nt;
            nt.BoneName = bone.Name;
            clip->Tracks.push_back( std::move( nt ) );
            track = &clip->Tracks.back();
        }

        // Upsert one key per channel at `time` (replace a key already within epsilon, else insert + sort).
        constexpr float eps       = 1e-3f;
        const auto      upsertPos = [&]( auto& keys, auto value, auto make )
        {
            for ( auto& k : keys )
                if ( std::abs( k.Time - time ) < eps )
                {
                    make( k, value );
                    return;
                }
            keys.push_back( {} );
            keys.back().Time = time;
            make( keys.back(), value );
            std::sort( keys.begin(), keys.end() );
        };
        upsertPos( track->PositionKeys, t, []( auto& k, const glm::vec3& v ) { k.Position = v; } );
        upsertPos( track->RotationKeys, r, []( auto& k, const glm::quat& v ) { k.Rotation = v; } );
        upsertPos( track->ScaleKeys, s, []( auto& k, const glm::vec3& v ) { k.Scale = v; } );
    }

    void SequencerPanel::RequestOpen()
    {
        s_OpenRequested = true;
    }

    void SequencerPanel::OnPreUpdate()
    {
        if ( s_OpenRequested )
        {
            GetVisibility() = true;
            s_OpenRequested = false;
        }
    }

    std::string SequencerPanel::CreateEmptyClip( const Animation::Skeleton& skeleton )
    {
        if ( !m_AssetManager || !m_Library )
            return {};

        // Unique name so repeated "New Clip" presses don't collide (scan this skeleton's registered clips).
        const auto  existing = m_Library->GetBySkeleton( skeleton.GetSignature() );
        std::string name     = "NewClip";
        for ( int n = 1;; ++n )
        {
            bool taken = false;
            for ( const auto& a : existing )
                if ( a && a->GetClip().AnimationName == name )
                {
                    taken = true;
                    break;
                }
            if ( !taken )
                break;
            name = "NewClip_" + std::to_string( n );
        }

        Animation::AnimationClip clip;
        clip.AnimationName     = name;
        clip.Duration          = 1.0f;
        clip.TicksPerSecond    = 25.0f;
        clip.SkeletonSignature = skeleton.GetSignature();
        clip.Tracks.reserve( skeleton.GetBones().size() );
        for ( const auto& bone : skeleton.GetBones() )
        {
            Animation::BoneTrack track;
            track.BoneName = bone.Name; // empty channels; the user adds keys in the track editor
            clip.Tracks.push_back( std::move( track ) );
        }

        auto asset = m_AssetManager->CreateAsset<Assets::AnimationAsset>(
             Assets::AssetPriority::Medium, Common::Filepath( "memory://clip/" + name ), false );
        if ( !asset )
            return {};
        asset->SetInMemoryClip( clip );
        m_Library->Register( asset );
        return name;
    }

    std::string SequencerPanel::SaveClipToDisk( const Animation::AnimationClip& clip )
    {
        namespace Ser = Assets::Serialization;

        Ser::AnimationAssetData data;
        data.Name              = clip.AnimationName;
        data.Duration          = clip.Duration;
        data.TicksPerSecond    = clip.TicksPerSecond;
        data.SkeletonSignature = clip.SkeletonSignature;
        for ( const auto& tr : clip.Tracks )
        {
            Ser::ChannelData ch;
            ch.BoneName  = tr.BoneName;
            ch.BoneIndex = tr.BoneIndex;
            for ( const auto& k : tr.PositionKeys )
                ch.Positions.push_back( { k.Time, k.Position } );
            for ( const auto& k : tr.RotationKeys )
                ch.Rotations.push_back( { k.Time, k.Rotation } );
            for ( const auto& k : tr.ScaleKeys )
                ch.Scales.push_back( { k.Time, k.Scale } );
            data.Channels.push_back( std::move( ch ) );
        }
        for ( const auto& n : clip.Notifies )
            data.Notifies.push_back( { n.Name, n.Time } );

        std::error_code ec;
        std::filesystem::create_directories( Common::Constants::Path::MESH_PATH_COOKED, ec );
        const std::filesystem::path path =
             Common::Constants::Path::MESH_PATH_COOKED / ( "_" + clip.AnimationName + ".anim" );

        std::ofstream out( path, std::ios::binary );
        if ( !out )
        {
            LOG_WARN( "[Sequencer] Could not write clip to {}", path.string() );
            return {};
        }
        out << rfl::json::write( data );
        LOG_INFO( "[Sequencer] Saved clip '{}' -> {}", clip.AnimationName, path.string() );
        return path.string();
    }

    void SequencerPanel::OnUIRender()
    {
        if ( !m_Scene )
        {
            ImGui::TextDisabled( "No active scene." );
            return;
        }

        const auto& sel = Core::SelectionManager::GetSelected();
        if ( !sel )
        {
            ImGui::TextDisabled( "Select a skinned-mesh entity to sequence its animation." );
            return;
        }
        const auto entOpt = m_Scene->FindEntityByID( *sel );
        if ( !entOpt )
        {
            ImGui::TextDisabled( "Selection not found in the scene." );
            return;
        }
        auto& entity = entOpt->get();
        if ( !entity.HasComponent<ECS::AnimationComponent>() ||
             !entity.HasComponent<ECS::SkinnedMeshComponent>() )
        {
            ImGui::TextDisabled( "Selected entity has no Animation + Skinned Mesh component." );
            return;
        }

        auto&       anim = entity.GetComponent<ECS::AnimationComponent>();
        const auto& smc  = entity.GetComponent<ECS::SkinnedMeshComponent>();
        // Editor-built runtime rig (Convert to Skinned) has no MeshHandle — prefer it (mirrors the render /
        // pick / animation paths) so a just-converted character animates in the Sequencer too.
        Desert::Mesh* mesh = smc.RuntimeMesh ? static_cast<Desert::Mesh*>( smc.RuntimeMesh.get() )
                                             : Runtime::ResourceRegistry::GetMeshService()->Get( smc.MeshHandle );
        if ( !mesh || !mesh->IsSkinned() )
        {
            ImGui::TextDisabled( "Skinned mesh not resolved yet." );
            return;
        }
        const uint64_t sig   = static_cast<SkinnedMesh*>( mesh )->GetSkeleton().GetSignature();
        const auto     clips = m_Library ? m_Library->GetBySkeleton( sig )
                                         : std::vector<Assets::Asset<Assets::AnimationAsset>>{};

        // Names for the clip combos + the index of the currently-selected clip.
        std::vector<const char*> clipNames;
        clipNames.reserve( clips.size() );
        int currentClipIdx = -1;
        for ( size_t i = 0; i < clips.size(); ++i )
        {
            clipNames.push_back( clips[i]->GetClip().AnimationName.c_str() );
            if ( clips[i]->GetClip().AnimationName == anim.CurrentClip )
                currentClipIdx = static_cast<int>( i );
        }

        Animation::Animator* animator = anim.Animator.get();

        // ---- Clip picker ----
        ImGui::SetNextItemWidth( 240.0f );
        if ( ImGui::Combo( "Clip", &currentClipIdx, clipNames.empty() ? nullptr : clipNames.data(),
                           static_cast<int>( clipNames.size() ) ) )
        {
            if ( currentClipIdx >= 0 && currentClipIdx < static_cast<int>( clips.size() ) )
            {
                anim.CurrentClip = clips[currentClipIdx]->GetClip().AnimationName;
                // PAUSE + Play the picked clip so it previews immediately and STAYS: an attached AnimGraph
                // drives the clip only while Playing, so pausing stops it from overriding the manual pick the
                // very next frame (the old bug where the viewport didn't change). Press Play to resume the graph.
                anim.Playing = false;
                if ( animator )
                {
                    animator->Play( clips[currentClipIdx]->GetClip(), anim.Loop );
                    animator->SetTime( 0.0f ); // Play() only sets the clip; SetTime recomputes the pose NOW
                                               // (paused => Update never runs, so the viewport would keep the
                                               // old clip's pose without this)
                }
            }
        }

        // ---- New empty clip (authored from scratch on this skeleton) ----
        ImGui::SameLine();
        if ( animator && m_AssetManager )
        {
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.40f, 0.28f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.26f, 0.50f, 0.36f, 1.0f ) );
            if ( ImGui::Button( ICON_MDI_PLUS " New Clip" ) )
            {
                const std::string created = CreateEmptyClip( animator->GetSkeleton() );
                if ( !created.empty() )
                {
                    anim.CurrentClip = created;
                    anim.Playing     = false;
                    // Rebind the picker's clip list next frame; play the new (empty) clip so its lanes show.
                    for ( const auto& a : m_Library->GetBySkeleton( animator->GetSkeleton().GetSignature() ) )
                        if ( a && a->GetClip().AnimationName == created )
                        {
                            animator->Play( a->GetClip(), false );
                            animator->SetTime( 0.0f ); // recompute the pose now (see the clip picker note)
                            break;
                        }
                }
            }
            ImGui::PopStyleColor( 2 );
        }

        // ---- Save the authored clip to disk so it persists across sessions ----
        if ( animator && animator->GetCurrentClip() )
        {
            ImGui::SameLine();
            if ( ImGui::Button( ICON_MDI_CONTENT_SAVE " Save" ) )
                SaveClipToDisk( *animator->GetCurrentClip() );
            Utils::ImGuiUtilities::Tooltip(
                 "Write this clip to Cooked/Meshes/_<name>.anim so it survives a restart\n"
                 "(rediscovered by the asset preloader next session)." );
        }

        const float duration = animator ? animator->GetDuration() : 0.0f;
        const float playTime = animator ? animator->GetCurrentTime() : 0.0f;

        // ---- Transport toolbar (icon buttons with tooltips) ----
        ImGui::SameLine( 0.0f, 20.0f );
        if ( ImGui::Button( ICON_MDI_SKIP_PREVIOUS ) && animator )
        {
            anim.Playing = false;
            animator->SetTime( 0.0f );
        }
        Utils::ImGuiUtilities::Tooltip( "Go to start" );
        ImGui::SameLine();
        if ( ImGui::Button( anim.Playing ? ICON_MDI_PAUSE : ICON_MDI_PLAY ) )
            anim.Playing = !anim.Playing;
        Utils::ImGuiUtilities::Tooltip( anim.Playing ? "Pause" : "Play (resumes the AnimGraph, if any)" );
        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_STOP ) )
        {
            anim.Playing = false;
            if ( animator )
                animator->SetTime( 0.0f );
        }
        Utils::ImGuiUtilities::Tooltip( "Stop + rewind" );
        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_SKIP_NEXT ) && animator )
        {
            anim.Playing = false;
            animator->SetTime( duration );
        }
        Utils::ImGuiUtilities::Tooltip( "Go to end" );

        ImGui::SameLine( 0.0f, 16.0f );
        ImGui::Checkbox( "Loop", &anim.Loop );
        ImGui::SameLine( 0.0f, 16.0f );
        ImGui::SetNextItemWidth( 110.0f );
        ImGui::SliderFloat( "Speed", &anim.PlaybackSpeed, 0.0f, 3.0f, "%.2fx" );
        ImGui::SameLine( 0.0f, 16.0f );
        ImGui::SetNextItemWidth( 120.0f );
        ImGui::SliderFloat( "Zoom", &m_PxPerSec, 20.0f, 240.0f, "%.0f px/u" );
        ImGui::SameLine( 0.0f, 16.0f );
        ImGui::TextColored( ImVec4( 0.80f, 0.86f, 0.98f, 1.0f ), "%.2f / %.2f s", playTime, duration );

        // ---- Keyframe toolbar: author BY MANIPULATION (pose a bone in Skeleton Edit, then record it) ----
        if ( animator )
        {
            auto*      editClip = const_cast<Animation::AnimationClip*>( animator->GetCurrentClip() );
            const int  selBone  = Core::SkeletonEditMode::GetSelectedBone();
            const bool canKey   = editClip && Core::SkeletonEditMode::IsActive() && selBone >= 0;

            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.44f, 0.31f, 0.10f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.58f, 0.41f, 0.14f, 1.0f ) );
            ImGui::BeginDisabled( !canKey );
            if ( ImGui::Button( ICON_MDI_KEY_PLUS " Key Bone @ Playhead" ) && canKey )
            {
                anim.Playing = false;
                KeyBonePose( editClip, animator->GetSkeleton(), selBone, playTime );
                animator->SetTime( playTime ); // re-evaluate so the new key shows immediately
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor( 2 );
            ImGui::SameLine();
            HelpMarker( "Keyframe by MANIPULATION:\n"
                        "1) In the viewport toolbar, enable Skeleton Edit.\n"
                        "2) Pick a bone and move/rotate it with the gizmo.\n"
                        "3) Set the playhead time, then click this to record the pose as a key.\n"
                        "Repeat at different times to build the motion (or drag the diamond keys below to "
                        "retime)." );
        }

        // ---- Timeline (gutter-aligned ruler; the track lanes below share the same time mapping) ----
        if ( animator && duration > 0.0f )
        {
            const float  gutter = 150.0f; // label column, shared by ruler + track lanes
            const float  rulerH = 30.0f;
            const ImVec2 avail  = ImGui::GetContentRegionAvail();
            const float  totalW = std::max( avail.x, gutter + 80.0f );
            const float  laneW  = totalW - gutter;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float  laneX0 = origin.x + gutter;
            ImDrawList*  dl     = ImGui::GetWindowDrawList();

            const auto timeToX = [&]( float t ) { return laneX0 + ( t / duration ) * laneW; };

            dl->AddRectFilled( ImVec2( laneX0, origin.y ), ImVec2( laneX0 + laneW, origin.y + rulerH ),
                               IM_COL32( 24, 24, 28, 255 ) );
            dl->AddText( ImVec2( origin.x + 6.0f, origin.y + 8.0f ), IM_COL32( 170, 170, 180, 255 ), "TIMELINE" );

            // Whole-unit tick lines + labels.
            for ( int u = 0; u <= static_cast<int>( duration ); ++u )
            {
                const float x = timeToX( static_cast<float>( u ) );
                dl->AddLine( ImVec2( x, origin.y ), ImVec2( x, origin.y + rulerH ),
                             IM_COL32( 255, 255, 255, 25 ) );
                char buf[16];
                std::snprintf( buf, sizeof( buf ), "%d", u );
                dl->AddText( ImVec2( x + 3.0f, origin.y + 3.0f ), IM_COL32( 190, 190, 190, 160 ), buf );
            }

            // Notify markers (from the current clip).
            if ( const Animation::AnimationClip* clip = animator->GetCurrentClip() )
            {
                for ( const auto& n : clip->Notifies )
                {
                    const float x = timeToX( n.Time );
                    const ImVec2 d0( x, origin.y + rulerH - 11.0f );
                    dl->AddTriangleFilled( ImVec2( d0.x - 5.0f, d0.y ), ImVec2( d0.x + 5.0f, d0.y ),
                                           ImVec2( d0.x, d0.y + 9.0f ), IM_COL32( 240, 200, 90, 255 ) );
                }
            }

            // Playhead over the ruler.
            const float px = timeToX( playTime );
            dl->AddLine( ImVec2( px, origin.y ), ImVec2( px, origin.y + rulerH ), IM_COL32( 255, 90, 90, 255 ),
                         2.0f );

            // Scrub over the ruler's lane area only (so it doesn't fight the key hit-boxes below).
            ImGui::SetCursorScreenPos( ImVec2( laneX0, origin.y ) );
            if ( laneW > 0.0f )
            {
                ImGui::InvisibleButton( "##seqScrub", ImVec2( laneW, rulerH ) );
                if ( ImGui::IsItemActive() )
                {
                    const float mx = ImGui::GetMousePos().x - laneX0;
                    anim.Playing   = false;
                    animator->SetTime( std::clamp( mx / laneW, 0.0f, 1.0f ) * duration );
                }
            }
            ImGui::SetCursorScreenPos( ImVec2( origin.x, origin.y + rulerH + 3.0f ) );

            // ---- Keyframe tracks ----
            DrawClipTracks( const_cast<Animation::AnimationClip*>( animator->GetCurrentClip() ), animator,
                            origin.x, gutter, laneW, duration );
        }
        else
        {
            ImGui::TextDisabled( "No clip playing — pick a clip above (or the mesh has no animations)." );
        }

        // ---- Additive layers (advanced, collapsed by default) ----
        ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
        const bool layersOpen = ImGui::CollapsingHeader( ICON_MDI_LAYERS " Additive Layers  (advanced)" );
        ImGui::SameLine();
        HelpMarker( "Play a SECOND clip ON TOP of the current one — e.g. a wave or aim while walking.\n\n"
                    "- Weight: how strongly it blends in (0..1).\n"
                    "- Additive: adds the layer's motion as an offset (good for lean/aim); off = it overrides.\n"
                    "- Mask from bone: restrict the layer to that bone + its children (empty = whole body, so a "
                    "'wave' would mask e.g. the right shoulder).\n\n"
                    "This is a preview tool; layers are not saved with the clip." );
        if ( layersOpen && animator )
        {
            ImGui::SetNextItemWidth( 200.0f );
            ImGui::Combo( "Layer clip", &m_LayerClip, clipNames.empty() ? nullptr : clipNames.data(),
                          static_cast<int>( clipNames.size() ) );
            ImGui::SetNextItemWidth( 160.0f );
            ImGui::SliderFloat( "Weight", &m_LayerWeight, 0.0f, 1.0f );
            ImGui::SameLine();
            ImGui::Checkbox( "Additive", &m_LayerAdditive );
            ImGui::SetNextItemWidth( 200.0f );
            ImGui::InputText( "Mask from bone", m_LayerMaskBone, sizeof( m_LayerMaskBone ) );

            const bool canAdd = m_LayerClip >= 0 && m_LayerClip < static_cast<int>( clips.size() );
            ImGui::BeginDisabled( !canAdd );
            if ( ImGui::Button( "Add Layer" ) && canAdd )
            {
                const int idx = animator->AddLayer( clips[m_LayerClip]->GetClip(), m_LayerWeight,
                                                    m_LayerAdditive, /*loop=*/true );
                if ( m_LayerMaskBone[0] != '\0' )
                    animator->SetLayerMaskByNames( idx, { std::string( m_LayerMaskBone ) } );
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text( "Active: %zu", animator->GetLayerCount() );
            ImGui::SameLine();
            if ( ImGui::Button( "Clear Layers" ) )
                animator->ClearLayers();
        }
    }

    void SequencerPanel::DrawClipTracks( Animation::AnimationClip* clip, Animation::Animator* animator,
                                         float contentX0, float gutter, float laneW, float duration )
    {
        if ( !clip || clip->Tracks.empty() )
        {
            ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
            ImGui::TextDisabled( "This clip has no bone tracks." );
            return;
        }

        const float laneX0    = contentX0 + gutter;
        const float laneH     = 18.0f;
        const char* chName[3] = { "Pos", "Rot", "Scl" };
        const ImU32 chCol[3]  = { IM_COL32( 120, 205, 120, 255 ), IM_COL32( 120, 165, 240, 255 ),
                                  IM_COL32( 235, 185, 110, 255 ) };
        const auto  timeToX   = [&]( float t ) { return laneX0 + ( t / duration ) * laneW; };

        const float childH = std::min( 260.0f, 8.0f + clip->Tracks.size() * 3.0f * laneH );
        ImGui::BeginChild( "##seqTracks", ImVec2( gutter + laneW, childH ), false,
                           ImGuiWindowFlags_HorizontalScrollbar );
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const float playX = timeToX( animator->GetCurrentTime() );

        bool liveRefresh = false; // a key is being dragged this frame -> refresh the pose live
        bool needSort    = false; // a drag just ended -> re-sort that channel + reselect the moved key
        int  sortTrack   = -1;
        int  sortChannel = -1;

        for ( int ti = 0; ti < static_cast<int>( clip->Tracks.size() ); ++ti )
        {
            auto& tr = clip->Tracks[ti];
            for ( int ch = 0; ch < 3; ++ch )
            {
                const ImVec2 rp    = ImGui::GetCursorScreenPos();
                const float  laneY = rp.y;
                const ImU32  strip = ( ti % 2 ) ? IM_COL32( 40, 40, 46, 255 ) : IM_COL32( 33, 33, 39, 255 );
                dl->AddRectFilled( ImVec2( laneX0, laneY ), ImVec2( laneX0 + laneW, laneY + laneH - 2.0f ),
                                   strip );

                if ( ch == 0 )
                    dl->AddText( ImVec2( contentX0 + 6.0f, laneY + 1.0f ), IM_COL32( 205, 205, 215, 255 ),
                                 tr.BoneName.c_str() );
                dl->AddText( ImVec2( contentX0 + gutter - 58.0f, laneY + 1.0f ), chCol[ch], chName[ch] );

                // Per-lane "+" (add a key at the playhead) so empty channels are keyable from scratch.
                ImGui::SetCursorScreenPos( ImVec2( contentX0 + gutter - 22.0f, laneY - 1.0f ) );
                ImGui::PushID( ( ti * 3 + ch ) * 4096 + 3999 );
                if ( ImGui::SmallButton( "+" ) )
                {
                    const float t = animator->GetCurrentTime();
                    if ( ch == 0 )
                        tr.PositionKeys.push_back( { t, glm::vec3( 0.0f ) } );
                    else if ( ch == 1 )
                        tr.RotationKeys.push_back( { t, glm::quat( 1.0f, 0.0f, 0.0f, 0.0f ) } );
                    else
                        tr.ScaleKeys.push_back( { t, glm::vec3( 1.0f ) } );
                    if ( ch == 0 )
                        std::sort( tr.PositionKeys.begin(), tr.PositionKeys.end() );
                    else if ( ch == 1 )
                        std::sort( tr.RotationKeys.begin(), tr.RotationKeys.end() );
                    else
                        std::sort( tr.ScaleKeys.begin(), tr.ScaleKeys.end() );
                    m_SelTrack   = ti;
                    m_SelChannel = ch;
                    if ( auto bi = animator->GetSkeleton().FindBoneIndex( tr.BoneName ); bi.has_value() )
                        Core::SkeletonEditMode::SetSelectedBone( static_cast<int>( bi.value() ) );
                    animator->SetTime( animator->GetCurrentTime() );
                }
                ImGui::PopID();

                dl->AddLine( ImVec2( playX, laneY ), ImVec2( playX, laneY + laneH - 2.0f ),
                             IM_COL32( 255, 90, 90, 150 ), 1.0f );

                const size_t nKeys = ( ch == 0 )   ? tr.PositionKeys.size()
                                     : ( ch == 1 ) ? tr.RotationKeys.size()
                                                   : tr.ScaleKeys.size();
                for ( int k = 0; k < static_cast<int>( nKeys ); ++k )
                {
                    float&      kt  = ( ch == 0 )   ? tr.PositionKeys[k].Time
                                      : ( ch == 1 ) ? tr.RotationKeys[k].Time
                                                    : tr.ScaleKeys[k].Time;
                    const float kx  = timeToX( kt );
                    const bool  isS = ( m_SelTrack == ti && m_SelChannel == ch && m_SelKey == k );

                    ImGui::SetCursorScreenPos( ImVec2( kx - 6.0f, laneY ) );
                    ImGui::PushID( ( ti * 3 + ch ) * 4096 + k );
                    ImGui::InvisibleButton( "##k", ImVec2( 12.0f, laneH - 2.0f ) );
                    const bool hov = ImGui::IsItemHovered();
                    if ( ImGui::IsItemActivated() )
                    {
                        m_SelTrack   = ti;
                        m_SelChannel = ch;
                        m_SelKey     = k;
                        // Selecting a bone's track also selects that bone on the skeleton (viewport highlight
                        // + gizmo), so Sequencer <-> Skeleton Edit stay in sync.
                        if ( auto bi = animator->GetSkeleton().FindBoneIndex( tr.BoneName ); bi.has_value() )
                            Core::SkeletonEditMode::SetSelectedBone( static_cast<int>( bi.value() ) );
                    }
                    if ( ImGui::IsItemActive() && laneW > 0.0f )
                    {
                        // Retime WITHOUT sorting mid-drag: sorting would change this key's index (and thus its
                        // ImGui ID), dropping the drag. We sort once on release instead.
                        kt = std::clamp( ( ImGui::GetMousePos().x - laneX0 ) / laneW, 0.0f, 1.0f ) * duration;
                        m_DragTime  = kt;
                        liveRefresh = true;
                    }
                    if ( ImGui::IsItemDeactivated() )
                    {
                        needSort    = true;
                        sortTrack   = ti;
                        sortChannel = ch;
                    }
                    ImGui::PopID();

                    const ImVec2 c( kx, laneY + ( laneH - 2.0f ) * 0.5f );
                    const float  r    = isS ? 6.0f : ( hov ? 5.5f : 4.0f );
                    ImVec2 diamond[4] = { ImVec2( c.x, c.y - r ), ImVec2( c.x + r, c.y ), ImVec2( c.x, c.y + r ),
                                          ImVec2( c.x - r, c.y ) };
                    dl->AddConvexPolyFilled( diamond, 4, isS ? IM_COL32( 255, 170, 60, 255 ) : chCol[ch] );
                    dl->AddPolyline( diamond, 4, IM_COL32( 18, 18, 22, 220 ), ImDrawFlags_Closed, 1.0f );
                }

                ImGui::SetCursorScreenPos( ImVec2( contentX0, laneY + laneH ) );
            }
        }
        ImGui::EndChild();

        if ( liveRefresh )
            animator->SetTime( animator->GetCurrentTime() ); // show the retimed pose live while dragging

        // Drag released: keep the channel sorted (the sampler needs monotonic time) and re-select the key
        // that just moved (its index changed after the sort).
        if ( needSort && sortTrack >= 0 )
        {
            auto& tr = clip->Tracks[sortTrack];
            if ( sortChannel == 0 )
                std::sort( tr.PositionKeys.begin(), tr.PositionKeys.end() );
            else if ( sortChannel == 1 )
                std::sort( tr.RotationKeys.begin(), tr.RotationKeys.end() );
            else
                std::sort( tr.ScaleKeys.begin(), tr.ScaleKeys.end() );

            const auto nearestKey = [&]( const auto& keys )
            {
                int   best = 0;
                float bd   = 1e9f;
                for ( int i = 0; i < static_cast<int>( keys.size() ); ++i )
                    if ( const float d = std::abs( keys[i].Time - m_DragTime ); d < bd )
                    {
                        bd   = d;
                        best = i;
                    }
                return best;
            };
            m_SelTrack   = sortTrack;
            m_SelChannel = sortChannel;
            m_SelKey     = sortChannel == 0   ? nearestKey( tr.PositionKeys )
                           : sortChannel == 1 ? nearestKey( tr.RotationKeys )
                                              : nearestKey( tr.ScaleKeys );

            animator->SetTime( animator->GetCurrentTime() ); // refresh the pose to the edited key
        }

        // ---- Selected-key inspector ----
        ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
        const bool valid = m_SelTrack >= 0 && m_SelTrack < static_cast<int>( clip->Tracks.size() ) &&
                           m_SelChannel >= 0 && m_SelChannel < 3;
        if ( valid )
        {
            auto&      tr    = clip->Tracks[m_SelTrack];
            const auto count = ( m_SelChannel == 0 )   ? tr.PositionKeys.size()
                               : ( m_SelChannel == 1 ) ? tr.RotationKeys.size()
                                                       : tr.ScaleKeys.size();
            const bool keyOk = m_SelKey >= 0 && m_SelKey < static_cast<int>( count );

            ImGui::Text( "%s  /  %s", tr.BoneName.c_str(), chName[m_SelChannel] );
            bool changed = false;
            if ( keyOk )
            {
                if ( m_SelChannel == 0 )
                {
                    auto& key = tr.PositionKeys[m_SelKey];
                    changed |= ImGui::DragFloat( "Time", &key.Time, 0.01f, 0.0f, duration, "%.3f" );
                    changed |= ImGui::DragFloat3( "Position", &key.Position.x, 0.01f );
                }
                else if ( m_SelChannel == 1 )
                {
                    auto& key = tr.RotationKeys[m_SelKey];
                    changed |= ImGui::DragFloat( "Time", &key.Time, 0.01f, 0.0f, duration, "%.3f" );
                    glm::vec3 euler = glm::degrees( glm::eulerAngles( key.Rotation ) );
                    if ( ImGui::DragFloat3( "Euler", &euler.x, 0.5f ) )
                    {
                        key.Rotation = glm::quat( glm::radians( euler ) );
                        changed      = true;
                    }
                }
                else
                {
                    auto& key = tr.ScaleKeys[m_SelKey];
                    changed |= ImGui::DragFloat( "Time", &key.Time, 0.01f, 0.0f, duration, "%.3f" );
                    changed |= ImGui::DragFloat3( "Scale", &key.Scale.x, 0.01f );
                }

                if ( ImGui::Button( ICON_MDI_DELETE "  Delete Key" ) )
                {
                    if ( m_SelChannel == 0 )
                        tr.PositionKeys.erase( tr.PositionKeys.begin() + m_SelKey );
                    else if ( m_SelChannel == 1 )
                        tr.RotationKeys.erase( tr.RotationKeys.begin() + m_SelKey );
                    else
                        tr.ScaleKeys.erase( tr.ScaleKeys.begin() + m_SelKey );
                    m_SelKey = -1;
                    changed  = true;
                }
                ImGui::SameLine();
            }

            if ( ImGui::Button( ICON_MDI_PLUS "  Add Key @ Playhead" ) )
            {
                const float t = animator->GetCurrentTime();
                if ( m_SelChannel == 0 )
                {
                    tr.PositionKeys.push_back( { t, glm::vec3( 0.0f ) } );
                    std::sort( tr.PositionKeys.begin(), tr.PositionKeys.end() );
                }
                else if ( m_SelChannel == 1 )
                {
                    tr.RotationKeys.push_back( { t, glm::quat( 1.0f, 0.0f, 0.0f, 0.0f ) } );
                    std::sort( tr.RotationKeys.begin(), tr.RotationKeys.end() );
                }
                else
                {
                    tr.ScaleKeys.push_back( { t, glm::vec3( 1.0f ) } );
                    std::sort( tr.ScaleKeys.begin(), tr.ScaleKeys.end() );
                }
                changed = true;
            }

            if ( changed )
                animator->SetTime( animator->GetCurrentTime() );
        }
        else
        {
            ImGui::TextDisabled( "Click a keyframe to edit it. Drag keys to retime." );
        }
    }
} // namespace Desert::Editor
