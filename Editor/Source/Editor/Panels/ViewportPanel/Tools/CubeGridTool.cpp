#include "CubeGridTool.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/ModelingState.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/MeshTypes.hpp>

#include <Common/Core/Math/AABB.hpp>

#include <ImGui/imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Desert::Editor::Tools
{
    namespace
    {
        // The 6 faces of a unit cube (position in [-0.5,0.5] + the engine's cube normals/tangents/UVs), each
        // as 4 CCW corners — copied from PrimitiveMeshFactory::CreateCube so winding/normals are known-good.
        // Corners map onto an arbitrary box AABB (P+0.5 -> [0,1], * boxSize + boxMin) so a box can be any size.
        struct FaceVert
        {
            glm::vec3 P, N, T, B;
            glm::vec2 UV;
        };
        const FaceVert kFace[6][4] = {
             // Front (+Z)
             { { { -0.5f, -0.5f, 0.5f }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0 } },
               { { 0.5f, -0.5f, 0.5f }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 0 } },
               { { 0.5f, 0.5f, 0.5f }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1 } },
               { { -0.5f, 0.5f, 0.5f }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 1 } } },
             // Back (-Z)
             { { { -0.5f, -0.5f, -0.5f }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 }, { 1, 0 } },
               { { -0.5f, 0.5f, -0.5f }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 }, { 1, 1 } },
               { { 0.5f, 0.5f, -0.5f }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, 1 } },
               { { 0.5f, -0.5f, -0.5f }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0 } } },
             // Top (+Y)
             { { { -0.5f, 0.5f, -0.5f }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1 } },
               { { -0.5f, 0.5f, 0.5f }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 0 } },
               { { 0.5f, 0.5f, 0.5f }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 1, 0 } },
               { { 0.5f, 0.5f, -0.5f }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 1, 1 } } },
             // Bottom (-Y)
             { { { -0.5f, -0.5f, -0.5f }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 1, 1 } },
               { { 0.5f, -0.5f, -0.5f }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1 } },
               { { 0.5f, -0.5f, 0.5f }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0 } },
               { { -0.5f, -0.5f, 0.5f }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0 } } },
             // Left (-X)
             { { { -0.5f, -0.5f, -0.5f }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 0 } },
               { { -0.5f, -0.5f, 0.5f }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0 } },
               { { -0.5f, 0.5f, 0.5f }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 1, 1 } },
               { { -0.5f, 0.5f, -0.5f }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1 } } },
             // Right (+X)
             { { { 0.5f, -0.5f, -0.5f }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0 } },
               { { 0.5f, 0.5f, -0.5f }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, { 1, 1 } },
               { { 0.5f, 0.5f, 0.5f }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, { 0, 1 } },
               { { 0.5f, -0.5f, 0.5f }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, { 0, 0 } } },
        };

        constexpr float kMinHeight = 0.02f; // smallest extrude (world units) — keeps a box non-degenerate
    } // namespace

    bool CubeGridTool::WorldToScreen( const glm::vec3& world, const glm::mat4& vp, const glm::vec2& pos,
                                      const glm::vec2& size, glm::vec2& out )
    {
        const glm::vec4 clip = vp * glm::vec4( world, 1.0f );
        if ( clip.w <= 0.0001f )
            return false; // behind the camera
        const glm::vec3 ndc = glm::vec3( clip ) / clip.w;
        out.x               = pos.x + ( ndc.x * 0.5f + 0.5f ) * size.x;
        out.y               = pos.y + ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * size.y;
        return true;
    }

    void CubeGridTool::Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray,
                               const glm::mat4& viewProj, const glm::vec2& viewportPos,
                               const glm::vec2& viewportSize, bool interactive )
    {
        Core::ModelingState& ms         = Core::ModelingState::Get();
        const float          cs         = ms.CellSize;
        const bool           toolActive = ms.ActiveTool == Core::ModelingState::Tool::CubeGrid;
        m_CellSize                      = cs;
        if ( ms.ReqClear )
        {
            ms.ReqClear = false;
            m_Boxes.clear();
            m_LastBox = -1;
            RegenMesh( scene );
        }

        // World AABB of a box: normal axis spans [base, base+sign*height]; in-plane axes span the footprint.
        auto boxMinSize = [cs]( const Box& bx, glm::vec3& mn, glm::vec3& size )
        {
            const int   ua = ( bx.na + 1 ) % 3;
            const int   va = ( bx.na + 2 ) % 3;
            const float a  = bx.base;
            const float b  = bx.base + bx.sign * bx.height;
            glm::vec3   lo( 0.0f ), hi( 0.0f );
            lo[bx.na] = std::min( a, b );
            hi[bx.na] = std::max( a, b );
            lo[ua]    = static_cast<float>( bx.cellMin[ua] ) * cs;
            hi[ua]    = static_cast<float>( bx.cellMin[ua] + bx.w ) * cs;
            lo[va]    = static_cast<float>( bx.cellMin[va] ) * cs;
            hi[va]    = static_cast<float>( bx.cellMin[va] + bx.d ) * cs;
            mn        = lo;
            size      = hi - lo;
        };

        // --- Targeting: nearest box face under the cursor, else the ground plane (y=0). ---
        m_HasTarget = m_HasErase = false;
        m_EraseBox               = -1;
        float bestT              = FLT_MAX;
        int   hitBox             = -1;
        for ( int i = 0; i < static_cast<int>( m_Boxes.size() ); ++i )
        {
            glm::vec3 mn, size;
            boxMinSize( m_Boxes[i], mn, size );
            Common::Math::AABB box;
            box.Min = mn;
            box.Max = mn + size;
            float t;
            if ( ray.IntersectsAABB( box, t ) && t >= 0.0f && t < bestT )
            {
                bestT  = t;
                hitBox = i;
            }
        }
        bool groundHit = false;
        if ( std::abs( ray.Direction.y ) > 1e-5f )
        {
            const float t = -ray.Origin.y / ray.Direction.y;
            if ( t > 0.0f && t < bestT )
            {
                bestT     = t;
                hitBox    = -1;
                groundHit = true;
            }
        }

        if ( bestT < FLT_MAX )
        {
            const glm::vec3 hitP = ray.Origin + ray.Direction * bestT;
            if ( groundHit || hitBox < 0 )
            {
                m_TargetNa   = 1;
                m_TargetSign = 1;
                m_TargetBase = 0.0f;
                m_TargetCell = { static_cast<int>( std::floor( hitP.x / cs ) ), 0,
                                 static_cast<int>( std::floor( hitP.z / cs ) ) };
                m_HasTarget  = true;
            }
            else
            {
                glm::vec3 mn, size;
                boxMinSize( m_Boxes[hitBox], mn, size );
                const glm::vec3 mx = mn + size;
                // Face = the axis+side whose plane the hit point lies on (nearest of the 6 box planes).
                float best = FLT_MAX;
                int   fa = 1, fs = 1;
                for ( int a = 0; a < 3; ++a )
                {
                    if ( std::abs( hitP[a] - mn[a] ) < best )
                    {
                        best = std::abs( hitP[a] - mn[a] );
                        fa   = a;
                        fs   = -1;
                    }
                    if ( std::abs( hitP[a] - mx[a] ) < best )
                    {
                        best = std::abs( hitP[a] - mx[a] );
                        fa   = a;
                        fs   = 1;
                    }
                }
                const int ua     = ( fa + 1 ) % 3;
                const int va     = ( fa + 2 ) % 3;
                m_TargetNa       = fa;
                m_TargetSign     = fs;
                m_TargetBase     = fs > 0 ? mx[fa] : mn[fa];
                m_TargetCell     = { 0, 0, 0 };
                m_TargetCell[ua] = static_cast<int>( std::floor( hitP[ua] / cs ) );
                m_TargetCell[va] = static_cast<int>( std::floor( hitP[va] / cs ) );
                m_HasTarget      = true;
                m_HasErase       = true;
                m_EraseBox       = hitBox;
            }
        }

        // --- Overlay draw list + helpers. ---
        ImDrawList* dl = ::ImGui::GetWindowDrawList();

        // Draw a box as SOLID shaded faces (only camera-facing faces) so the blockout reads as solid volumes
        // in the viewport, independent of the generated mesh.
        auto drawBoxSolid = [&]( const glm::vec3& mn, const glm::vec3& size, ImU32 fill, ImU32 outline )
        {
            const glm::vec3 center = mn + size * 0.5f;
            for ( int f = 0; f < 6; ++f )
            {
                const glm::vec3 fn = kFace[f][0].N;
                const glm::vec3 fc = center + 0.5f * size * fn;
                if ( glm::dot( fn, ray.Origin - fc ) <= 0.0f )
                    continue; // back face (points away from the camera)
                glm::vec2 s[4];
                bool      ok = true;
                for ( int k = 0; k < 4; ++k )
                    if ( !WorldToScreen( mn + ( kFace[f][k].P + 0.5f ) * size, viewProj, viewportPos, viewportSize,
                                         s[k] ) )
                    {
                        ok = false;
                        break;
                    }
                if ( !ok )
                    continue;
                dl->AddQuadFilled( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                                   ImVec2( s[3].x, s[3].y ), fill );
                dl->AddQuad( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                             ImVec2( s[3].x, s[3].y ), outline, 1.0f );
            }
        };

        const bool removeMode = ::ImGui::GetIO().KeyShift;
        const bool interact   = interactive && toolActive && !::ImGui::IsAnyItemActive();
        bool       changed    = false;

        // Existing boxes: grey checkerboard solids (the erase target is tinted red).
        if ( toolActive )
            for ( int i = 0; i < static_cast<int>( m_Boxes.size() ); ++i )
            {
                glm::vec3 mn, size;
                boxMinSize( m_Boxes[i], mn, size );
                const bool  erase = removeMode && m_HasErase && m_EraseBox == i;
                const ImU32 fill  = erase       ? IM_COL32( 200, 90, 80, 200 )
                                    : ( i & 1 ) ? IM_COL32( 120, 125, 135, 200 )
                                                : IM_COL32( 150, 155, 165, 200 );
                drawBoxSolid( mn, size, fill, IM_COL32( 90, 95, 105, 230 ) );
            }

        const int bw = std::max( 1, ms.BrushW );
        const int bd = std::max( 1, ms.BrushD );

        // Flat W×D footprint rectangle on the target plane (the hover cursor).
        auto drawFootprint = [&]( ImU32 fill, ImU32 outline )
        {
            const int na = m_TargetNa;
            const int ua = ( na + 1 ) % 3;
            const int va = ( na + 2 ) % 3;
            glm::vec2 s[4];
            const int du[4] = { 0, bw, bw, 0 };
            const int dv[4] = { 0, 0, bd, bd };
            for ( int k = 0; k < 4; ++k )
            {
                glm::vec3 w( 0.0f );
                w[na] = m_TargetBase;
                w[ua] = static_cast<float>( m_TargetCell[ua] + du[k] ) * cs;
                w[va] = static_cast<float>( m_TargetCell[va] + dv[k] ) * cs;
                if ( !WorldToScreen( w, viewProj, viewportPos, viewportSize, s[k] ) )
                    return;
            }
            dl->AddQuadFilled( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                               ImVec2( s[3].x, s[3].y ), fill );
            dl->AddQuad( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                         ImVec2( s[3].x, s[3].y ), outline, 1.5f );
        };

        // Working grid around the cursor on the target plane (only when hovering, not dragging).
        if ( toolActive && m_HasTarget && !m_Dragging )
        {
            const int   na     = m_TargetNa;
            const int   ua     = ( na + 1 ) % 3;
            const int   va     = ( na + 2 ) % 3;
            const int   G      = 8;
            const ImU32 gcol   = IM_COL32( 120, 145, 180, 90 );
            auto        gridPt = [&]( int iu, int iv )
            {
                glm::vec3 w( 0.0f );
                w[na] = m_TargetBase;
                w[ua] = static_cast<float>( m_TargetCell[ua] + iu ) * cs;
                w[va] = static_cast<float>( m_TargetCell[va] + iv ) * cs;
                return w;
            };
            for ( int iu = -G; iu <= G + 1; ++iu )
            {
                glm::vec2 a, b;
                if ( WorldToScreen( gridPt( iu, -G ), viewProj, viewportPos, viewportSize, a ) &&
                     WorldToScreen( gridPt( iu, G + 1 ), viewProj, viewportPos, viewportSize, b ) )
                    dl->AddLine( ImVec2( a.x, a.y ), ImVec2( b.x, b.y ), gcol, 1.0f );
            }
            for ( int iv = -G; iv <= G + 1; ++iv )
            {
                glm::vec2 a, b;
                if ( WorldToScreen( gridPt( -G, iv ), viewProj, viewportPos, viewportSize, a ) &&
                     WorldToScreen( gridPt( G + 1, iv ), viewProj, viewportPos, viewportSize, b ) )
                    dl->AddLine( ImVec2( a.x, a.y ), ImVec2( b.x, b.y ), gcol, 1.0f );
            }
        }

        // --- Interaction: click a footprint, then drag along the face normal to STRETCH the box to a free
        //     continuous height (a plain click makes a box of the current Height). Shift+LMB / RMB removes a
        //     whole box. ---
        if ( m_Dragging )
        {
            const int na = m_DragBox.na;
            const int ua = ( na + 1 ) % 3;
            const int va = ( na + 2 ) % 3;
            glm::vec3 nrmW( 0.0f );
            nrmW[na] = static_cast<float>( m_DragBox.sign );

            // Height is driven in SCREEN space so the preview tracks the cursor exactly and the value stays
            // fully continuous (no cube snapping): project the footprint centre + one cell along the normal,
            // and convert the cursor's travel from the press point into world units.
            glm::vec3 center( 0.0f );
            center[na] = m_DragBox.base;
            center[ua] = ( static_cast<float>( m_DragBox.cellMin[ua] ) + m_DragBox.w * 0.5f ) * cs;
            center[va] = ( static_cast<float>( m_DragBox.cellMin[va] ) + m_DragBox.d * 0.5f ) * cs;
            glm::vec2  s0, s1;
            const bool ok0     = WorldToScreen( center, viewProj, viewportPos, viewportSize, s0 );
            const bool ok1     = WorldToScreen( center + nrmW * cs, viewProj, viewportPos, viewportSize, s1 );
            glm::vec2  dir     = s1 - s0;
            float      perCell = glm::length( dir );
            float      worldPerPx;
            if ( ok0 && ok1 && perCell > 6.0f )
            {
                dir /= perCell;
                worldPerPx = cs / perCell;
            }
            else
            {
                dir        = glm::vec2( 0.0f, -1.0f ); // near-parallel to view: screen-up == taller
                worldPerPx = cs / 28.0f;
            }
            const glm::vec2 mp = glm::vec2( ::ImGui::GetMousePos().x, ::ImGui::GetMousePos().y );
            const float     px = glm::dot( mp - m_DragStartMouse, dir );
            m_DragBox.height   = std::clamp( m_DragBaseHeight + px * worldPerPx, kMinHeight, 1.0e6f );
            ms.Height          = m_DragBox.height; // live-reflect into the panel

            glm::vec3 mn, size;
            boxMinSize( m_DragBox, mn, size );
            drawBoxSolid( mn, size, IM_COL32( 90, 195, 240, 210 ), IM_COL32( 220, 245, 255, 235 ) );

            char htxt[32];
            std::snprintf( htxt, sizeof( htxt ), "H %.2f", m_DragBox.height );
            dl->AddText( ImVec2( mp.x + 14.0f, mp.y - 4.0f ), IM_COL32( 230, 245, 255, 255 ), htxt );

            if ( !::ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) // release -> commit the box
            {
                m_Boxes.push_back( m_DragBox );
                m_LastBox  = static_cast<int>( m_Boxes.size() ) - 1;
                m_Dragging = false;
                changed    = true;
            }
        }
        else if ( toolActive )
        {
            // Hover preview of the footprint (green = add, red = erase).
            if ( removeMode && m_HasErase )
                drawFootprint( IM_COL32( 240, 80, 60, 60 ), IM_COL32( 240, 90, 70, 255 ) );
            else if ( m_HasTarget )
                drawFootprint( IM_COL32( 70, 220, 100, 60 ), IM_COL32( 90, 240, 120, 255 ) );

            if ( interact )
            {
                const bool lmbClick = ::ImGui::IsMouseClicked( ImGuiMouseButton_Left );
                const bool rmbClick = ::ImGui::IsMouseClicked( ImGuiMouseButton_Right );
                if ( ( ( lmbClick && removeMode ) || rmbClick ) && m_HasErase ) // erase the hovered box
                {
                    m_Boxes.erase( m_Boxes.begin() + m_EraseBox );
                    m_LastBox = -1;
                    changed   = true;
                }
                else if ( lmbClick && !removeMode && m_HasTarget ) // begin an extrude drag
                {
                    m_Dragging        = true;
                    m_DragBox         = Box{};
                    m_DragBox.cellMin = m_TargetCell;
                    m_DragBox.w       = bw;
                    m_DragBox.d       = bd;
                    m_DragBox.na      = m_TargetNa;
                    m_DragBox.sign    = m_TargetSign;
                    m_DragBox.base    = m_TargetBase;
                    m_DragBox.height  = kMinHeight;
                    m_DragStartMouse  = glm::vec2( ::ImGui::GetMousePos().x, ::ImGui::GetMousePos().y );
                    m_DragBaseHeight  = std::max( ms.Height, kMinHeight );
                }
            }
        }

        // --- Live height edit: changing ModelingState.Height (panel field, E/Q or arrows) re-deforms the
        //     last committed box, so you can tune the height after drawing it and before Accept. ---
        if ( !m_Dragging && m_LastBox >= 0 && m_LastBox < static_cast<int>( m_Boxes.size() ) )
        {
            const float newH = std::max( ms.Height, kMinHeight );
            if ( std::abs( newH - m_Boxes[m_LastBox].height ) > 1e-5f )
            {
                m_Boxes[m_LastBox].height = newH;
                ms.Height                 = newH;
                changed                   = true;
            }
        }

        // --- Keys: E / Q (or Up / Down) raise / lower the last box's Height (handled by the live-edit block
        //     above); Ctrl+E / Ctrl+Q grow / shrink the grid step. ---
        if ( interact && !m_Dragging )
        {
            const bool ctrl = ::ImGui::GetIO().KeyCtrl;
            if ( ctrl && ::ImGui::IsKeyPressed( ImGuiKey_E, false ) )
                ms.CellSize = std::min( ms.CellSize * 2.0f, 100000.0f );
            else if ( ctrl && ::ImGui::IsKeyPressed( ImGuiKey_Q, false ) )
                ms.CellSize = std::max( ms.CellSize * 0.5f, 0.01f );
            else if ( ::ImGui::IsKeyPressed( ImGuiKey_E, false ) ||
                      ::ImGui::IsKeyPressed( ImGuiKey_UpArrow, false ) )
                ms.Height = std::max( kMinHeight, ms.Height + cs );
            else if ( ::ImGui::IsKeyPressed( ImGuiKey_Q, false ) ||
                      ::ImGui::IsKeyPressed( ImGuiKey_DownArrow, false ) )
                ms.Height = std::max( kMinHeight, ms.Height - cs );
        }

        ms.Cubes = static_cast<int>( m_Boxes.size() );

        // --- Viewport bottom bar: tool name + Accept / Cancel (UE5-style confirmation), shown while a
        //     blockout is in progress. ---
        if ( toolActive && ( !m_Boxes.empty() || m_Entity != Common::UUID::Null() ) )
        {
            ::ImGui::SetNextWindowPos(
                 ImVec2( viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + viewportSize.y - 58.0f ),
                 ImGuiCond_Always, ImVec2( 0.5f, 0.0f ) );
            ::ImGui::SetNextWindowBgAlpha( 0.92f );
            ::ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 12.0f, 8.0f ) );
            ::ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 12.0f, 6.0f ) );
            ::ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8.0f, 6.0f ) );
            if ( ::ImGui::Begin( "##cubegrid_accept", nullptr,
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoNav ) )
            {
                ::ImGui::AlignTextToFramePadding();
                ::ImGui::TextUnformatted( ICON_MDI_GRID "  CubeGrid" );
                ::ImGui::SameLine( 0.0f, 16.0f );
                ::ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.55f, 0.30f, 1.0f ) );
                ::ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.26f, 0.68f, 0.38f, 1.0f ) );
                if ( ::ImGui::Button( ICON_MDI_CHECK "  Accept" ) && !m_Boxes.empty() )
                {
                    Core::SelectionManager::SetSelected( m_Entity );
                    m_Entity = Common::UUID::Null(); // keep the mesh; next edits start a fresh blockout
                    m_Boxes.clear();
                    m_LastBox = -1;
                }
                ::ImGui::PopStyleColor( 2 );
                ::ImGui::SameLine();
                ::ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.55f, 0.22f, 0.22f, 1.0f ) );
                ::ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.70f, 0.28f, 0.28f, 1.0f ) );
                if ( ::ImGui::Button( ICON_MDI_CLOSE "  Cancel" ) )
                    Cancel( scene );
                ::ImGui::PopStyleColor( 2 );
            }
            ::ImGui::End();
            ::ImGui::PopStyleVar( 3 );
        }

        if ( changed )
            RegenMesh( scene );
    }

    void CubeGridTool::RegenMesh( ::Desert::Core::Scene& scene )
    {
        if ( m_Boxes.empty() )
        {
            Cancel( scene ); // no boxes left -> remove the entity
            return;
        }

        std::vector<Vertex> verts;
        std::vector<Index>  inds;
        glm::vec3           mn( FLT_MAX ), mx( -FLT_MAX );
        for ( const Box& bx : m_Boxes )
        {
            const int   ua = ( bx.na + 1 ) % 3;
            const int   va = ( bx.na + 2 ) % 3;
            const float a  = bx.base;
            const float b  = bx.base + bx.sign * bx.height;
            glm::vec3   bmin( 0.0f ), bsize( 0.0f );
            bmin[bx.na]  = std::min( a, b );
            bsize[bx.na] = std::abs( b - a );
            bmin[ua]     = static_cast<float>( bx.cellMin[ua] ) * m_CellSize;
            bsize[ua]    = static_cast<float>( bx.w ) * m_CellSize;
            bmin[va]     = static_cast<float>( bx.cellMin[va] ) * m_CellSize;
            bsize[va]    = static_cast<float>( bx.d ) * m_CellSize;

            for ( int f = 0; f < 6; ++f )
            {
                const uint32_t base = static_cast<uint32_t>( verts.size() );
                for ( int k = 0; k < 4; ++k )
                {
                    const FaceVert& fv = kFace[f][k];
                    Vertex          v;
                    v.Position  = bmin + ( fv.P + 0.5f ) * bsize;
                    v.Normal    = fv.N;
                    v.Tangent   = fv.T;
                    v.Bitangent = fv.B;
                    v.TexCoord  = fv.UV;
                    verts.push_back( v );
                    mn = glm::min( mn, v.Position );
                    mx = glm::max( mx, v.Position );
                }
                inds.push_back( { base + 0, base + 1, base + 2 } );
                inds.push_back( { base + 2, base + 3, base + 0 } );
            }
        }

        // Ensure the live blockout entity exists.
        if ( m_Entity == Common::UUID::Null() )
        {
            auto& e = scene.CreateNewEntity( "Blockout" );
            e.AddComponent<ECS::StaticMeshComponent>();
            m_Entity = e.GetComponent<ECS::UUIDComponent>().UUID;
        }
        auto ref = scene.FindEntityByID( m_Entity );
        if ( !ref )
        {
            m_Entity = Common::UUID::Null();
            return;
        }
        ECS::Entity entity = ref->get();
        auto&       smc    = entity.HasComponent<ECS::StaticMeshComponent>()
                                  ? entity.GetComponent<ECS::StaticMeshComponent>()
                                  : entity.AddComponent<ECS::StaticMeshComponent>();

        Common::Math::AABB aabb;
        aabb.Min                  = mn;
        aabb.Max                  = mx;
        std::vector<Submesh> subs = { { "Blockout", 0, static_cast<uint32_t>( verts.size() ), 0,
                                        static_cast<uint32_t>( inds.size() ) * 3, glm::mat4( 1.0f ), aabb } };
        smc.RuntimeMesh           = std::make_shared<DynamicMesh>( verts, inds, subs );
        smc.RuntimeMesh->Invalidate();
    }

    void CubeGridTool::Cancel( ::Desert::Core::Scene& scene )
    {
        if ( m_Entity != Common::UUID::Null() )
        {
            if ( auto ref = scene.FindEntityByID( m_Entity ) )
                scene.DestroyEntity( ref->get() );
        }
        m_Entity = Common::UUID::Null();
        m_Boxes.clear();
        m_LastBox  = -1;
        m_Dragging = false;
    }
} // namespace Desert::Editor::Tools
