#include "LightGizmoRenderer.hpp"
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

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

            auto& light     = entity.GetComponent<ECS::PointLightComponent>().Data;
            auto& transform = entity.GetComponent<ECS::TransformComponent>();
            auto& uuid      = entity.GetComponent<ECS::UUIDComponent>().UUID;

            const auto mvp = camera->GetProjectionMatrix() * camera->GetViewMatrix();

            glm::vec2 screenPos;
            if ( !ProjectToScreen( transform.Translation, mvp, width, height, screenPos ) )
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
                DrawLightRadiusSphere( camera, transform.Translation, light.Radius, width, height, windowPos.x,
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
                                  light.Intensity, light.Radius, transform.Translation.x, transform.Translation.y,
                                  transform.Translation.z )
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

            auto& light     = entity.GetComponent<ECS::SpotLightComponent>().Data;
            auto& transform = entity.GetComponent<ECS::TransformComponent>();

            const auto mvp = camera->GetProjectionMatrix() * camera->GetViewMatrix();

            glm::vec2 screenPos;
            if ( !ProjectToScreen( transform.Translation, mvp, width, height, screenPos ) )
                continue;

            const float absoluteX = windowPos.x + screenPos.x;
            const float absoluteY = windowPos.y + screenPos.y;

            const char* icon     = ICON_MDI_SPOTLIGHT;
            ImVec2      iconSize  = ImGui::CalcTextSize( icon );
            ImDrawList* drawList  = ImGui::GetWindowDrawList();

            drawList->AddText( ImVec2( absoluteX - iconSize.x * 0.5f, absoluteY - iconSize.y * 0.5f ),
                               ImColor( ImVec4( 1.0f, 0.9f, 0.5f, 1.0f ) ), icon );

            // Forward = entity's -Z in world space (matches the SpotLightECSSystem direction).
            const glm::mat4 world   = transform.GetTransform();
            const glm::vec3 forward = glm::normalize( -glm::vec3( world[2] ) );

            if ( light.ShowCone )
                DrawSpotCone( camera, transform.Translation, forward, light.OuterConeAngle, light.Range, width,
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
                                  transform.Translation.x, transform.Translation.y, transform.Translation.z )
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

            const auto& cam       = entity.GetComponent<ECS::CameraComponent>().Data;
            const auto& transform = entity.GetComponent<ECS::TransformComponent>();

            // Billboard icon at the camera position.
            glm::vec2 screenPos;
            if ( ProjectToScreen( transform.Translation, mvp, width, height, screenPos ) )
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
            const glm::mat4 world    = transform.GetTransform();
            const glm::vec3 pos      = transform.Translation;
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