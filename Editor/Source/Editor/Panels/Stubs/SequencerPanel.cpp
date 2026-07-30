#include "SequencerPanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Animation/Animator.hpp>
#include <Engine/Animation/AnimationLibrary.hpp>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    SequencerPanel::SequencerPanel( std::shared_ptr<::Desert::Core::Scene> scene,
                                    Animation::AnimationLibrary* library )
         : IPanel( "Sequencer", /*showPanel=*/false ), m_Scene( std::move( scene ) ), m_Library( library )
    {
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

        auto& anim = entity.GetComponent<ECS::AnimationComponent>();
        auto* mesh = Runtime::ResourceRegistry::GetMeshService()->Get(
             entity.GetComponent<ECS::SkinnedMeshComponent>().MeshHandle );
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

        // ---- Clip picker ----
        ImGui::SetNextItemWidth( 240.0f );
        if ( ImGui::Combo( "Clip", &currentClipIdx,
                           clipNames.empty() ? nullptr : clipNames.data(),
                           static_cast<int>( clipNames.size() ) ) )
        {
            if ( currentClipIdx >= 0 && currentClipIdx < static_cast<int>( clips.size() ) )
                anim.CurrentClip = clips[currentClipIdx]->GetClip().AnimationName; // AnimationECSSystem plays it
        }

        Animation::Animator* animator = anim.Animator.get();

        // ---- Transport ----
        ImGui::SameLine( 0.0f, 20.0f );
        if ( ImGui::Button( anim.Playing ? ICON_MDI_PAUSE : ICON_MDI_PLAY ) )
            anim.Playing = !anim.Playing;
        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_STOP ) )
        {
            anim.Playing = false;
            if ( animator )
                animator->SetTime( 0.0f );
        }
        ImGui::SameLine();
        ImGui::Checkbox( "Loop", &anim.Loop );
        ImGui::SameLine( 0.0f, 16.0f );
        ImGui::SetNextItemWidth( 120.0f );
        ImGui::SliderFloat( "Speed", &anim.PlaybackSpeed, 0.0f, 3.0f, "%.2fx" );
        ImGui::SameLine( 0.0f, 16.0f );
        ImGui::SetNextItemWidth( 130.0f );
        ImGui::SliderFloat( "Zoom", &m_PxPerSec, 20.0f, 240.0f, "%.0f px/u" );

        const float duration = animator ? animator->GetDuration() : 0.0f;
        const float playTime = animator ? animator->GetCurrentTime() : 0.0f;
        ImGui::SameLine( 0.0f, 16.0f );
        ImGui::Text( "%.2f / %.2f", playTime, duration );

        // ---- Timeline (ruler + playhead + notify markers) ----
        if ( animator && duration > 0.0f )
        {
            const float  height = 70.0f;
            const ImVec2 avail  = ImGui::GetContentRegionAvail();
            const float  width  = std::max( avail.x, 60.0f );
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImDrawList*  dl     = ImGui::GetWindowDrawList();

            dl->AddRectFilled( origin, ImVec2( origin.x + width, origin.y + height ),
                               IM_COL32( 28, 28, 32, 255 ) );

            const auto timeToX = [&]( float t ) { return origin.x + ( t / duration ) * width; };

            // Whole-unit tick lines + labels.
            for ( int u = 0; u <= static_cast<int>( duration ); ++u )
            {
                const float x = timeToX( static_cast<float>( u ) );
                dl->AddLine( ImVec2( x, origin.y ), ImVec2( x, origin.y + height ),
                             IM_COL32( 255, 255, 255, 22 ) );
                char buf[16];
                std::snprintf( buf, sizeof( buf ), "%d", u );
                dl->AddText( ImVec2( x + 2.0f, origin.y + 2.0f ), IM_COL32( 190, 190, 190, 150 ), buf );
            }

            // Notify markers (from the current clip).
            if ( const Animation::AnimationClip* clip = animator->GetCurrentClip() )
            {
                for ( const auto& n : clip->Notifies )
                {
                    const float x = timeToX( n.Time );
                    dl->AddLine( ImVec2( x, origin.y ), ImVec2( x, origin.y + height ),
                                 IM_COL32( 240, 200, 90, 220 ), 1.5f );
                    const ImVec2 d0( x, origin.y + height - 12.0f );
                    dl->AddTriangleFilled( ImVec2( d0.x - 5.0f, d0.y ), ImVec2( d0.x + 5.0f, d0.y ),
                                           ImVec2( d0.x, d0.y + 10.0f ), IM_COL32( 240, 200, 90, 255 ) );
                    if ( !n.Name.empty() )
                        dl->AddText( ImVec2( x + 4.0f, origin.y + height - 26.0f ),
                                     IM_COL32( 240, 220, 150, 230 ), n.Name.c_str() );
                }
            }

            // Playhead.
            const float px = timeToX( playTime );
            dl->AddLine( ImVec2( px, origin.y ), ImVec2( px, origin.y + height ),
                         IM_COL32( 255, 90, 90, 255 ), 2.0f );

            // Scrub: click / drag the timeline seeks (and pauses so the pose holds where you left it).
            // Guard the zero-size case (a docked/zero-width timeline) — InvisibleButton asserts on it.
            if ( width > 0.0f && height > 0.0f )
            {
                ImGui::InvisibleButton( "##seqScrub", ImVec2( width, height ) );
                if ( ImGui::IsItemActive() )
                {
                    const float mx    = ImGui::GetMousePos().x - origin.x;
                    const float tSeek = std::clamp( mx / width, 0.0f, 1.0f ) * duration;
                    anim.Playing      = false;
                    animator->SetTime( tSeek );
                }
            }
        }
        else
        {
            ImGui::TextDisabled( "No clip playing — pick a clip above (or the mesh has no animations)." );
        }

        // ---- Layers preview (override / additive with a bone mask) ----
        ImGui::Separator();
        if ( ImGui::CollapsingHeader( "Layers (preview)" ) && animator )
        {
            ImGui::TextDisabled( "Overlay a clip on the base pose. 'Mask from bone' restricts it to that "
                                 "bone + its children (empty = whole body)." );
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
} // namespace Desert::Editor
