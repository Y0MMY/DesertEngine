#include "LightGizmoRenderer.hpp"
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/SkeletonEditMode.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Animation/Animator.hpp>

#include <functional>

#include "../../Core/EditorResources.hpp"

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        // Projects a world point to viewport-local screen coords, returning false when the point is
        // behind the camera (clip w <= 0). WorldToScreenSpace divides by w unconditionally, so behind
        // points flip to mirrored on-screen positions — that produced both the ghost bulb icon when
        // turning 180 degrees and the radius circle lines streaking across the whole screen.
        bool ProjectToScreen( const glm::vec3& world, const glm::mat4& mvp, float width, float height,
                              glm::vec2& outScreen )
        {
            const glm::vec4 clip = mvp * glm::vec4( world, 1.0f );
            if ( clip.w <= 1e-4f )
                return false;

            const glm::vec3 ndc = glm::vec3( clip ) / clip.w;
            outScreen.x = ( ndc.x * 0.5f + 0.5f ) * width;
            outScreen.y = ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * height;
            return true;
        }
    } // namespace

    LightGizmoRenderer::LightGizmoRenderer( const std::shared_ptr<Desert::Core::Scene>& scene ) : m_Scene( scene )
    {
    }

    void LightGizmoRenderer::Render( float width, float height, float xpos, float ypos )
    {
        const auto camera = m_Scene->GetMainCamera().lock();
        if ( !camera )
            return;

        RenderPointLights( camera, width, height, xpos, ypos );
        RenderSpotLights( camera, width, height, xpos, ypos );
        RenderCameras( camera, width, height, xpos, ypos );

        // Collider wireframes moved to EditorColliderPass (true 3D, depth-tested) via the Editor Pass API.

        if ( Core::SkeletonEditMode::IsActive() )
            RenderSkeleton( camera, width, height, xpos, ypos );
    }

    void LightGizmoRenderer::RenderPointLights( const std::shared_ptr<Desert::Core::Camera>& camera, float width,
                                                float height, float xpos, float ypos )
    {
        auto entities = m_Scene->GetAllEntities();

        ImVec2 windowPos = ImGui::GetWindowPos();

        for ( auto entity : entities )
        {
            if ( !entity.HasComponent<ECS::PointLightComponent>() )
            {
                continue;
            }

            auto&           light    = entity.GetComponent<ECS::PointLightComponent>().Data;
            const glm::vec3 worldPos = glm::vec3( entity.GetWorldTransform()[3] ); // parent-composed position

            const auto mvp = camera->GetProjectionMatrix() * camera->GetViewMatrix();

            glm::vec2 screenPos;
            if ( !ProjectToScreen( worldPos, mvp, width, height, screenPos ) )
                continue; // light is behind the camera — skip its icon, radius and tooltip entirely

            float absoluteX = windowPos.x + screenPos.x;
            float absoluteY = windowPos.y + screenPos.y;

            const char* icon     = ICON_MDI_LIGHTBULB;
            ImVec2      iconSize = ImGui::CalcTextSize( icon );

            ImDrawList* drawList   = ImGui::GetWindowDrawList();
            ImVec4      lightColor = ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );

            drawList->AddText( ImVec2( absoluteX - iconSize.x * 0.5f, absoluteY - iconSize.y * 0.5f ),
                               ImColor( lightColor ), icon );

            if ( light.ShowRadius )
            {
                DrawLightRadiusSphere( camera, worldPos, light.Radius, width, height, windowPos.x,
                                       windowPos.y, absoluteX, absoluteY );
            }
            ImVec2 mousePos = ImGui::GetMousePos();
            if ( mousePos.x >= absoluteX - iconSize.x * 0.5f && mousePos.x <= absoluteX + iconSize.x * 0.5f &&
                 mousePos.y >= absoluteY - iconSize.y * 0.5f && mousePos.y <= absoluteY + iconSize.y * 0.5f )
            {
                ImGui::PushStyleColor( ImGuiCol_PopupBg, IM_COL32( 0, 0, 0, 0 ) );
                ImGui::PushStyleColor( ImGuiCol_Border, IM_COL32( 0, 0, 0, 0 ) );
                Utils::ImGuiUtilities::Tooltip(
                     std::format( "Point Light\nIntensity: {}\nRadius: {}\nPosition: ({}, {}, {})",
                                  light.Intensity, light.Radius, worldPos.x, worldPos.y, worldPos.z )
                          .c_str() );
                ImGui::PopStyleColor( 2 );
            }
        }
    }

    void LightGizmoRenderer::RenderSpotLights( const std::shared_ptr<Desert::Core::Camera>& camera, float width,
                                               float height, float xpos, float ypos )
    {
        auto entities = m_Scene->GetAllEntities();

        ImVec2 windowPos = ImGui::GetWindowPos();

        for ( auto entity : entities )
        {
            if ( !entity.HasComponent<ECS::SpotLightComponent>() )
                continue;

            auto&           light    = entity.GetComponent<ECS::SpotLightComponent>().Data;
            const glm::mat4 worldXf  = entity.GetWorldTransform(); // parent-composed
            const glm::vec3 worldPos = glm::vec3( worldXf[3] );

            const auto mvp = camera->GetProjectionMatrix() * camera->GetViewMatrix();

            glm::vec2 screenPos;
            if ( !ProjectToScreen( worldPos, mvp, width, height, screenPos ) )
                continue;

            const float absoluteX = windowPos.x + screenPos.x;
            const float absoluteY = windowPos.y + screenPos.y;

            const char* icon     = ICON_MDI_SPOTLIGHT;
            ImVec2      iconSize  = ImGui::CalcTextSize( icon );
            ImDrawList* drawList  = ImGui::GetWindowDrawList();

            drawList->AddText( ImVec2( absoluteX - iconSize.x * 0.5f, absoluteY - iconSize.y * 0.5f ),
                               ImColor( ImVec4( 1.0f, 0.9f, 0.5f, 1.0f ) ), icon );

            // Forward = entity's -Z in world space (matches the SpotLightECSSystem direction).
            const glm::vec3 forward = glm::normalize( -glm::vec3( worldXf[2] ) );

            if ( light.ShowCone )
                DrawSpotCone( camera, worldPos, forward, light.OuterConeAngle, light.Range, width,
                              height, windowPos.x, windowPos.y );

            ImVec2 mousePos = ImGui::GetMousePos();
            if ( mousePos.x >= absoluteX - iconSize.x * 0.5f && mousePos.x <= absoluteX + iconSize.x * 0.5f &&
                 mousePos.y >= absoluteY - iconSize.y * 0.5f && mousePos.y <= absoluteY + iconSize.y * 0.5f )
            {
                ImGui::PushStyleColor( ImGuiCol_PopupBg, IM_COL32( 0, 0, 0, 0 ) );
                ImGui::PushStyleColor( ImGuiCol_Border, IM_COL32( 0, 0, 0, 0 ) );
                Utils::ImGuiUtilities::Tooltip(
                     std::format( "Spot Light\nIntensity: {}\nRange: {}\nCone: {} / {}\nPosition: ({}, {}, {})",
                                  light.Intensity, light.Range, light.InnerConeAngle, light.OuterConeAngle,
                                  worldPos.x, worldPos.y, worldPos.z )
                          .c_str() );
                ImGui::PopStyleColor( 2 );
            }
        }
    }

    void LightGizmoRenderer::RenderCameras( const std::shared_ptr<Desert::Core::Camera>& camera, float width,
                                            float height, float xpos, float ypos )
    {
        auto         entities  = m_Scene->GetAllEntities();
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const auto   mvp       = camera->GetProjectionMatrix() * camera->GetViewMatrix();
        ImDrawList*  drawList  = ImGui::GetWindowDrawList();

        for ( auto entity : entities )
        {
            if ( !entity.HasComponent<ECS::CameraComponent>() )
                continue;

            const auto& cam = entity.GetComponent<ECS::CameraComponent>().Data;
            // WORLD transform (walks parents) — a camera parented to e.g. a character must draw its icon and
            // frustum at the parent-composed position, not its local offset.
            const glm::mat4 worldXf  = entity.GetWorldTransform();
            const glm::vec3 worldPos = glm::vec3( worldXf[3] );

            // Billboard icon at the camera position.
            glm::vec2 screenPos;
            if ( ProjectToScreen( worldPos, mvp, width, height, screenPos ) )
            {
                const char*  icon     = ICON_MDI_VIDEO; // video/movie camera (not a photo camera)
                const ImVec2 iconSize = ImGui::CalcTextSize( icon );
                const float  ax       = windowPos.x + screenPos.x;
                const float  ay       = windowPos.y + screenPos.y;
                drawList->AddText( ImVec2( ax - iconSize.x * 0.5f, ay - iconSize.y * 0.5f ),
                                   ImColor( ImVec4( 0.6f, 0.85f, 1.0f, 1.0f ) ), icon );

                const ImVec2 m = ImGui::GetMousePos();
                if ( m.x >= ax - iconSize.x * 0.5f && m.x <= ax + iconSize.x * 0.5f &&
                     m.y >= ay - iconSize.y * 0.5f && m.y <= ay + iconSize.y * 0.5f )
                {
                    ImGui::PushStyleColor( ImGuiCol_PopupBg, IM_COL32( 0, 0, 0, 0 ) );
                    ImGui::PushStyleColor( ImGuiCol_Border, IM_COL32( 0, 0, 0, 0 ) );
                    Utils::ImGuiUtilities::Tooltip(
                         std::format( "Camera{}\nFOV: {}\nNear: {}  Far: {}", cam.IsMainCamera ? " (Main)" : "",
                                      cam.FOV, cam.Near, cam.Far )
                              .c_str() );
                    ImGui::PopStyleColor( 2 );
                }
            }

            // View frustum wireframe. The real Far can be huge (1000) -> draw only a SHORT, compact frustum
            // (a couple units deep) so the FOV/aspect shape reads clearly near the icon instead of a few
            // diverging lines streaking off-screen.
            const glm::mat4 world    = worldXf;
            const glm::vec3 pos      = worldPos;
            const glm::vec3 forward  = glm::normalize( -glm::vec3( world[2] ) );
            const glm::vec3 up       = glm::normalize( glm::vec3( world[1] ) );
            const float     aspect   = height > 0.0f ? width / height : 1.7778f;
            const float     gizmoFar = glm::min( cam.Far, cam.Near + 2.5f );
            const glm::mat4 camView  = glm::lookAt( pos, pos + forward, up );
            const glm::mat4 camProj  = glm::perspective( glm::radians( cam.FOV ), aspect, cam.Near, gizmoFar );
            const glm::mat4 invVP    = glm::inverse( camProj * camView );

            // GL-convention NDC cube corners (z in [-1,1]): 0-3 near, 4-7 far. Keep them in WORLD space so we
            // can clip each edge to the editor camera's near plane before projecting (a wide FOV puts corners
            // near/behind the editor camera; an unclipped perspective divide then streaks lines off-screen).
            static const glm::vec3 ndc[8] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
                                              { -1, -1, 1 },  { 1, -1, 1 },  { 1, 1, 1 },  { -1, 1, 1 } };
            glm::vec3              corners[8];
            for ( int i = 0; i < 8; ++i )
            {
                glm::vec4 w = invVP * glm::vec4( ndc[i], 1.0f );
                corners[i]  = glm::vec3( w ) / w.w;
            }

            static const int edges[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
                                              { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            const ImU32      col = IM_COL32( 150, 220, 255, 200 );

            const auto toScreen = [&]( const glm::vec4& clip ) -> ImVec2
            {
                const glm::vec3 n = glm::vec3( clip ) / clip.w;
                return ImVec2( windowPos.x + ( n.x * 0.5f + 0.5f ) * width,
                               windowPos.y + ( 1.0f - ( n.y * 0.5f + 0.5f ) ) * height );
            };

            for ( const auto& e : edges )
            {
                glm::vec4   ca   = mvp * glm::vec4( corners[e[0]], 1.0f );
                glm::vec4   cb   = mvp * glm::vec4( corners[e[1]], 1.0f );
                const float kEps = 1e-3f;
                if ( ca.w <= kEps && cb.w <= kEps )
                    continue; // both behind the editor camera
                // Clip the endpoint that crosses the near plane (w = kEps) so the line never wraps.
                if ( ca.w <= kEps )
                    ca = ca + ( ( kEps - ca.w ) / ( cb.w - ca.w ) ) * ( cb - ca );
                else if ( cb.w <= kEps )
                    cb = cb + ( ( kEps - cb.w ) / ( ca.w - cb.w ) ) * ( ca - cb );
                drawList->AddLine( toScreen( ca ), toScreen( cb ), col, 1.5f );
            }
        }
    }

    void LightGizmoRenderer::DrawWorldLine( ImDrawList* drawList, const glm::vec3& a, const glm::vec3& b,
                                            const glm::mat4& mvp, float width, float height, float windowX,
                                            float windowY, ImU32 color, float thickness )
    {
        glm::vec4         ca   = mvp * glm::vec4( a, 1.0f );
        glm::vec4         cb   = mvp * glm::vec4( b, 1.0f );
        constexpr float   kEps = 1e-3f;
        if ( ca.w <= kEps && cb.w <= kEps )
            return; // both behind the editor camera

        if ( ca.w <= kEps )
            ca = ca + ( ( kEps - ca.w ) / ( cb.w - ca.w ) ) * ( cb - ca );
        else if ( cb.w <= kEps )
            cb = cb + ( ( kEps - cb.w ) / ( ca.w - cb.w ) ) * ( ca - cb );

        const auto toScreen = [&]( const glm::vec4& clip ) -> ImVec2
        {
            const glm::vec3 n = glm::vec3( clip ) / clip.w;
            return ImVec2( windowX + ( n.x * 0.5f + 0.5f ) * width,
                           windowY + ( 1.0f - ( n.y * 0.5f + 0.5f ) ) * height );
        };

        drawList->AddLine( toScreen( ca ), toScreen( cb ), color, thickness );
    }

    void LightGizmoRenderer::RenderSkeleton( const std::shared_ptr<Desert::Core::Camera>& camera, float width,
                                             float height, float xpos, float ypos )
    {
        const auto& selected = Core::SelectionManager::GetSelected();
        if ( !selected )
            return;
        const auto& entOpt = m_Scene->FindEntityByID( *selected );
        if ( !entOpt )
            return;
        auto& entity = entOpt->get();
        if ( !entity.HasComponent<ECS::SkinnedMeshComponent>() )
            return;

        const auto& smc  = entity.GetComponent<ECS::SkinnedMeshComponent>();
        auto*       mesh = Runtime::ResourceRegistry::GetMeshService()->Get( smc.MeshHandle );
        if ( !mesh || !mesh->IsSkinned() )
            return;
        const auto& bones = static_cast<SkinnedMesh*>( mesh )->GetSkeleton().GetBones();
        if ( bones.empty() )
            return;

        const glm::mat4 entityWorld = entity.GetComponent<ECS::TransformComponent>().GetTransform();
        const glm::mat4 mvp         = camera->GetProjectionMatrix() * camera->GetViewMatrix();
        const ImVec2    windowPos   = ImGui::GetWindowPos();
        ImDrawList*     drawList    = ImGui::GetWindowDrawList();

        const ImU32 boneCol      = IM_COL32( 235, 200, 90, 230 ); // bind-pose bone
        const ImU32 selCol       = IM_COL32( 255, 130, 40, 255 ); // tree-selected bone
        const int   selectedBone = Core::SkeletonEditMode::GetSelectedBone();

        // Bone head (world) = entityWorld * chainGlobal[3], where chainGlobal = the parent chain of
        // LocalBindTransform (= the Animator's bind global). This MATCHES the rendered mesh, which is skinned
        // with bind bone matrices = chainGlobal * OffsetMatrix (NOT identity). Using inverse(OffsetMatrix)
        // instead put the bones at the raw-vertex scale (thousands of units) while the mesh renders at the
        // chain scale — hence "bones much bigger than the mesh". Memoized so bone array order doesn't matter.
        std::vector<glm::mat4>             chainGlobal( bones.size(), glm::mat4( 1.0f ) );
        std::vector<bool>                  done( bones.size(), false );
        std::function<glm::mat4( size_t )> resolve = [&]( size_t i ) -> glm::mat4
        {
            if ( done[i] )
                return chainGlobal[i];
            glm::mat4 g = bones[i].LocalBindTransform;
            if ( bones[i].ParentBoneID.has_value() && bones[i].ParentBoneID.value() < bones.size() )
                g = resolve( bones[i].ParentBoneID.value() ) * bones[i].LocalBindTransform;
            chainGlobal[i] = g;
            done[i]        = true;
            return g;
        };
        // Use the SAME pose the mesh is RENDERED with, so bones overlay the actual (possibly animated) mesh
        // — not the static bind pose. The render skins with BoneMatrices[i] = globalPosed_i * OffsetMatrix_i,
        // so the bone's posed global = BoneMatrices[i] * inverse(OffsetMatrix_i) and its head = that [3]. When
        // there is no Animator, fall back to the bind chain (== what MeshECSSystem feeds as the bind pose).
        const std::vector<glm::mat4>* poseMatrices = nullptr;
        if ( entity.HasComponent<ECS::AnimationComponent>() )
        {
            const auto& anim = entity.GetComponent<ECS::AnimationComponent>();
            if ( anim.Animator )
                poseMatrices = &anim.Animator->GetPose().BoneMatrices;
        }

        std::vector<glm::vec3> heads( bones.size() );
        for ( size_t i = 0; i < bones.size(); ++i )
        {
            glm::mat4 global;
            if ( poseMatrices && i < poseMatrices->size() )
                global = ( *poseMatrices )[i] * glm::inverse( bones[i].OffsetMatrix );
            else
                global = resolve( i ); // bind pose (no animator)
            heads[i] = glm::vec3( entityWorld * glm::vec4( glm::vec3( global[3] ), 1.0f ) );
        }

        // Parent -> child bone links.
        for ( size_t i = 0; i < bones.size(); ++i )
        {
            if ( !bones[i].ParentBoneID.has_value() )
                continue;
            const uint32_t p = bones[i].ParentBoneID.value();
            if ( p >= heads.size() )
                continue;
            const bool sel = ( static_cast<int>( i ) == selectedBone || static_cast<int>( p ) == selectedBone );
            DrawWorldLine( drawList, heads[p], heads[i], mvp, width, height, windowPos.x, windowPos.y,
                           sel ? selCol : boneCol, sel ? 3.0f : 2.0f );
        }

        // Bone head markers (small screen-space squares; the selected bone is larger + accent-coloured) +
        // the bone NAME as a label next to each head (the selected bone's label is accent-coloured).
        const ImU32 labelCol    = IM_COL32( 220, 220, 230, 210 );
        const ImU32 labelSelCol = IM_COL32( 255, 170, 90, 255 );
        for ( size_t i = 0; i < bones.size(); ++i )
        {
            glm::vec2 s;
            if ( !ProjectToScreen( heads[i], mvp, width, height, s ) )
                continue;
            const ImVec2 c( windowPos.x + s.x, windowPos.y + s.y );
            const bool   sel = ( static_cast<int>( i ) == selectedBone );
            const float  r   = sel ? 5.0f : 3.0f;
            drawList->AddRectFilled( ImVec2( c.x - r, c.y - r ), ImVec2( c.x + r, c.y + r ),
                                     sel ? selCol : boneCol );
            // Only label the selected bone by default — dense rigs overlap all names into a blob otherwise
            // (toggle "Names" in the viewport overlay to show them all).
            if ( !bones[i].Name.empty() && ( sel || Core::SkeletonEditMode::ShowAllNames() ) )
                drawList->AddText( ImVec2( c.x + r + 3.0f, c.y - 7.0f ), sel ? labelSelCol : labelCol,
                                   bones[i].Name.c_str() );
        }
    }

    void LightGizmoRenderer::DrawSpotCone( const std::shared_ptr<Desert::Core::Camera>& camera,
                                           const glm::vec3& apex, const glm::vec3& dir, float outerAngleDeg,
                                           float range, float width, float height, float windowX, float windowY )
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const auto  mvp      = camera->GetProjectionMatrix() * camera->GetViewMatrix();
        const ImU32 color    = ImColor( 1.0f, 0.9f, 0.5f, 0.85f );

        // Build an orthonormal basis around the cone axis.
        glm::vec3 up = ( glm::abs( dir.y ) > 0.99f ) ? glm::vec3( 1.0f, 0.0f, 0.0f ) : glm::vec3( 0.0f, 1.0f, 0.0f );
        glm::vec3 right = glm::normalize( glm::cross( dir, up ) );
        up             = glm::normalize( glm::cross( right, dir ) );

        const float  outer       = glm::radians( outerAngleDeg );
        const float  capRadius   = range * glm::tan( outer );
        const glm::vec3 capCenter = apex + dir * range;

        const int    segments = 48;
        glm::vec2    prev;
        bool         prevValid = false;
        glm::vec2    apexS;
        const bool   apexValid = ProjectToScreen( apex, mvp, width, height, apexS );

        for ( int i = 0; i <= segments; ++i )
        {
            const float a   = 2.0f * glm::pi<float>() * i / segments;
            glm::vec3   p   = capCenter + capRadius * ( glm::cos( a ) * right + glm::sin( a ) * up );
            glm::vec2   s;
            const bool  ok  = ProjectToScreen( p, mvp, width, height, s );
            const ImVec2 sp = ImVec2( windowX + s.x, windowY + s.y );

            if ( ok && prevValid )
                drawList->AddLine( ImVec2( windowX + prev.x, windowY + prev.y ), sp, color, 1.5f );
            // A few rib lines from the apex to the cap edge.
            if ( ok && apexValid && ( i % 12 == 0 ) )
                drawList->AddLine( ImVec2( windowX + apexS.x, windowY + apexS.y ), sp, color, 1.5f );

            prev      = s;
            prevValid = ok;
        }
    }

    void LightGizmoRenderer::DrawLightRadiusSphere( const std::shared_ptr<Desert::Core::Camera>& camera,
                                                    const glm::vec3& worldPos, float radius, float width,
                                                    float height, float windowX, float windowY, float iconCenterX,
                                                    float iconCenterY )
    {
        ImDrawList* drawList         = ImGui::GetWindowDrawList();
        const auto  viewMatrix       = camera->GetViewMatrix();
        const auto  projectionMatrix = camera->GetProjectionMatrix();
        const auto  mvp              = projectionMatrix * viewMatrix;

        const int segments = 64;

        ImU32 colorWhite = ImColor( 1.0f, 1.0f, 1.0f, 1.0f );

        DrawAxisAlignedCircle( drawList, worldPos, radius, segments, glm::vec3( 1.0f, 0.0f, 0.0f ),
                               glm::vec3( 0.0f, 0.0f, 1.0f ), mvp, width, height, windowX, windowY, colorWhite );

        DrawAxisAlignedCircle( drawList, worldPos, radius, segments, glm::vec3( 1.0f, 0.0f, 0.0f ),
                               glm::vec3( 0.0f, 1.0f, 0.0f ), mvp, width, height, windowX, windowY, colorWhite );

        DrawAxisAlignedCircle( drawList, worldPos, radius, segments, glm::vec3( 0.0f, 1.0f, 0.0f ),
                               glm::vec3( 0.0f, 0.0f, 1.0f ), mvp, width, height, windowX, windowY, colorWhite );
    }

    void LightGizmoRenderer::DrawAxisAlignedCircle( ImDrawList* drawList, const glm::vec3& center, float radius,
                                                    int segments, const glm::vec3& axis1, const glm::vec3& axis2,
                                                    const glm::mat4& mvp, float width, float height, float windowX,
                                                    float windowY, ImU32 color )
    {
        std::vector<ImVec2> screenPoints( segments + 1 );
        std::vector<bool>   valid( segments + 1, false );

        for ( int i = 0; i <= segments; ++i )
        {
            float     angle = 2.0f * glm::pi<float>() * i / segments;
            glm::vec3 point = center + radius * ( cos( angle ) * axis1 + sin( angle ) * axis2 );

            glm::vec2 screenPoint;
            valid[i] = ProjectToScreen( point, mvp, width, height, screenPoint );
            if ( valid[i] )
                screenPoints[i] = ImVec2( windowX + screenPoint.x, windowY + screenPoint.y );
        }

        // Only connect neighbouring points that are BOTH in front of the camera — otherwise a segment
        // crossing behind the near plane would draw a line streaking across the whole viewport.
        for ( int i = 1; i <= segments; ++i )
        {
            if ( valid[i - 1] && valid[i] )
                drawList->AddLine( screenPoints[i - 1], screenPoints[i], color, 1.5f );
        }
    }

} // namespace Desert::Editor