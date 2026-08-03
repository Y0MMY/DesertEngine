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
        // Pack/unpack an integer grid cell into a 63-bit key (21 bits/axis, centred so negatives fit).
        constexpr int OFF = 1 << 20;
        uint64_t      Pack( const glm::ivec3& c )
        {
            const uint64_t x = static_cast<uint64_t>( c.x + OFF ) & 0x1FFFFF;
            const uint64_t y = static_cast<uint64_t>( c.y + OFF ) & 0x1FFFFF;
            const uint64_t z = static_cast<uint64_t>( c.z + OFF ) & 0x1FFFFF;
            return x | ( y << 21 ) | ( z << 42 );
        }
        glm::ivec3 Unpack( uint64_t k )
        {
            return { static_cast<int>( k & 0x1FFFFF ) - OFF, static_cast<int>( ( k >> 21 ) & 0x1FFFFF ) - OFF,
                     static_cast<int>( ( k >> 42 ) & 0x1FFFFF ) - OFF };
        }

        // The 6 faces of a unit cube (position in [-0.5,0.5], + the engine's cube normals/tangents/UVs), each
        // as 4 CCW corners — copied from PrimitiveMeshFactory::CreateCube so winding/normals are known-good.
        // Per-face outward neighbour offset drives internal-face culling.
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
        const glm::ivec3 kNeighbor[6] = { { 0, 0, 1 },  { 0, 0, -1 }, { 0, 1, 0 },
                                          { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 } };
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
        // Grid step + tool activation are owned by the docked ModelingPanel (Core::ModelingState). CubeGrid
        // paints/keys only when it's the selected tool; the panel's "Clear" is a one-shot request.
        Core::ModelingState& ms         = Core::ModelingState::Get();
        const float          cs         = ms.CellSize;
        const bool           toolActive = ms.ActiveTool == Core::ModelingState::Tool::CubeGrid;
        m_CellSize = cs; // keep the generated mesh's cell size in lockstep with the panel's grid step (else
                         // the overlay uses `cs` while RegenMesh bakes a mismatched-scale mesh -> "big cube")
        if ( ms.ReqClear )
        {
            ms.ReqClear = false;
            m_Cells.clear();
            m_Region.clear();
            m_LastBase.clear();
            m_LastHeight = 0;
            RegenMesh( scene );
        }

        // --- Targeting: nearest occupied cube face, else the ground plane (y=0). ---
        m_HasAdd = m_HasRemove = false;
        float      bestT       = FLT_MAX;
        glm::ivec3 hitCell{ 0 };
        bool       hit = false;
        for ( uint64_t key : m_Cells )
        {
            const glm::ivec3   c = Unpack( key );
            Common::Math::AABB box;
            box.Min = glm::vec3( c ) * cs;
            box.Max = box.Min + cs;
            float t;
            if ( ray.IntersectsAABB( box, t ) && t >= 0.0f && t < bestT )
            {
                bestT   = t;
                hitCell = c;
                hit     = true;
            }
        }
        if ( hit )
        {
            const glm::vec3 p  = ray.Origin + ray.Direction * bestT;
            const glm::vec3 d  = p - ( glm::vec3( hitCell ) + 0.5f ) * cs; // from cube centre
            const glm::vec3 ad = glm::abs( d );
            glm::ivec3      n{ 0 };
            if ( ad.x >= ad.y && ad.x >= ad.z )
                n.x = d.x > 0 ? 1 : -1;
            else if ( ad.y >= ad.z )
                n.y = d.y > 0 ? 1 : -1;
            else
                n.z = d.z > 0 ? 1 : -1;
            m_Add       = hitCell + n;
            m_Remove    = hitCell;
            m_AddNormal = n;
            m_HasAdd = m_HasRemove = true;
        }
        else if ( std::abs( ray.Direction.y ) > 1e-5f )
        {
            const float t = -ray.Origin.y / ray.Direction.y;
            if ( t > 0.0f )
            {
                const glm::vec3 p = ray.Origin + ray.Direction * t;
                m_Add             = { static_cast<int>( std::floor( p.x / cs ) ), 0,
                                      static_cast<int>( std::floor( p.z / cs ) ) };
                m_AddNormal       = { 0, 1, 0 };
                m_HasAdd          = true;
            }
        }

        ImDrawList* dl = ::ImGui::GetWindowDrawList();

        auto occupied = [&]( const glm::ivec3& c ) { return m_Cells.count( Pack( c ) ) > 0; };

        // Draw a cell as SOLID shaded faces — only outward, camera-facing, non-internal faces — so the
        // blockout reads as solid boxes in the viewport (independent of the generated mesh). `isOcc` decides
        // which neighbours hide a face (existing cells vs. the live extrude preview); checkerboard shading
        // makes individual cells legible.
        auto drawCellSolid = [&]( const glm::ivec3& c, auto&& isOcc, ImU32 fillEven, ImU32 fillOdd, ImU32 outline )
        {
            const ImU32 fill = ( ( c.x + c.y + c.z ) & 1 ) ? fillOdd : fillEven;
            for ( int f = 0; f < 6; ++f )
            {
                if ( isOcc( c + kNeighbor[f] ) )
                    continue; // internal face — hidden
                const glm::vec3 fn = glm::vec3( kNeighbor[f] );
                const glm::vec3 fc = ( glm::vec3( c ) + 0.5f ) * cs + 0.5f * cs * fn;
                if ( glm::dot( fn, ray.Origin - fc ) <= 0.0f )
                    continue; // back face (points away from the camera)
                glm::vec2 s[4];
                bool      ok = true;
                for ( int k = 0; k < 4; ++k )
                    if ( !WorldToScreen( ( glm::vec3( c ) + kFace[f][k].P + 0.5f ) * cs, viewProj, viewportPos,
                                         viewportSize, s[k] ) )
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

        // Existing cells: grey checkerboard solid boxes.
        if ( toolActive && m_Cells.size() < 6000 )
            for ( uint64_t k : m_Cells )
                drawCellSolid( Unpack( k ), occupied, IM_COL32( 150, 155, 165, 190 ),
                               IM_COL32( 120, 125, 135, 190 ), IM_COL32( 90, 95, 105, 220 ) );

        if ( toolActive && m_HasAdd && !m_Dragging )
        {
            // Visible working grid on the target plane around the cursor (UE5-style) so you see where cells go.
            const glm::ivec3 nrm        = m_AddNormal;
            const int        na         = nrm.x ? 0 : nrm.y ? 1 : 2;
            const int        ua         = ( na + 1 ) % 3;
            const int        va         = ( na + 2 ) % 3;
            const float      planeCoord = static_cast<float>( m_Add[na] ) * cs;
            const int        G          = 8;
            auto             gridPt     = [&]( int iu, int iv )
            {
                glm::vec3 w( 0.0f );
                w[na] = planeCoord;
                w[ua] = static_cast<float>( m_Add[ua] + iu ) * cs;
                w[va] = static_cast<float>( m_Add[va] + iv ) * cs;
                return w;
            };
            const ImU32 gcol = IM_COL32( 120, 145, 180, 90 );
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

        // In-plane axes for the current target face + the WxD brush footprint on it.
        const int na = m_AddNormal.x ? 0 : m_AddNormal.y ? 1 : 2;
        const int ua = ( na + 1 ) % 3;
        const int va = ( na + 2 ) % 3;
        const int bw = std::max( 1, ms.BrushW );
        const int bd = std::max( 1, ms.BrushD );

        // WxD footprint at an anchor cell across two in-plane axes.
        auto footprintAt = [&]( const glm::ivec3& anchor, int axisU, int axisV, std::vector<glm::ivec3>& out )
        {
            out.clear();
            for ( int i = 0; i < bw; ++i )
                for ( int j = 0; j < bd; ++j )
                {
                    glm::ivec3 c = anchor;
                    c[axisU] += i;
                    c[axisV] += j;
                    out.push_back( c );
                }
        };

        // Flat cell face on the current target plane (a square) — the hover footprint reads as a GRID CELL.
        auto drawCellFace = [&]( const glm::ivec3& c, ImU32 fill, ImU32 outline )
        {
            const float pcrd = static_cast<float>( m_Add[na] ) * cs;
            glm::vec2   s[4];
            for ( int k = 0; k < 4; ++k )
            {
                glm::vec3 w( 0.0f );
                w[na] = pcrd;
                w[ua] = static_cast<float>( c[ua] + ( ( k == 1 || k == 2 ) ? 1 : 0 ) ) * cs;
                w[va] = static_cast<float>( c[va] + ( ( k == 2 || k == 3 ) ? 1 : 0 ) ) * cs;
                if ( !WorldToScreen( w, viewProj, viewportPos, viewportSize, s[k] ) )
                    return;
            }
            dl->AddQuadFilled( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                               ImVec2( s[3].x, s[3].y ), fill );
            dl->AddQuad( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                         ImVec2( s[3].x, s[3].y ), outline, 1.5f );
        };

        // --- Interaction: click a cell, then drag along the face normal (up / down) to set the extrude
        //     HEIGHT — a clean column of cells that NEVER overlaps. The live preview is drawn in a distinct
        //     colour; existing cells stay grey. Shift+LMB / RMB removes the hovered cube. ---
        // Base footprint + N-tall column of cells along a normal, as a packed set.
        auto buildColumn = [&]( const glm::ivec3& anchor, const glm::ivec3& nrm, int axisU, int axisV, int nH,
                                std::unordered_set<uint64_t>& out )
        {
            out.clear();
            std::vector<glm::ivec3> fp;
            footprintAt( anchor, axisU, axisV, fp );
            for ( const auto& c : fp )
                for ( int L = 0; L < nH; ++L )
                    out.insert( Pack( c + nrm * L ) );
        };

        if ( m_Dragging )
        {
            const glm::vec3 nrmW = glm::normalize( glm::vec3( m_DragNormal ) );
            const int       dna  = m_DragNormal.x ? 0 : m_DragNormal.y ? 1 : 2;
            const int       dua  = ( dna + 1 ) % 3;
            const int       dva  = ( dna + 2 ) % 3;

            // Height is driven in SCREEN space so the preview tracks the cursor exactly (predictable, works in
            // any view): project the footprint centre + one cell along the normal, and count how many
            // cell-lengths the cursor has moved from the press point along that screen direction.
            glm::vec3 center( 0.0f );
            center[dna] = ( static_cast<float>( m_DragAnchor[dna] ) + 0.5f ) * cs;
            center[dua] = ( static_cast<float>( m_DragAnchor[dua] ) + bw * 0.5f ) * cs;
            center[dva] = ( static_cast<float>( m_DragAnchor[dva] ) + bd * 0.5f ) * cs;
            glm::vec2  s0, s1;
            const bool ok0     = WorldToScreen( center, viewProj, viewportPos, viewportSize, s0 );
            const bool ok1     = WorldToScreen( center + nrmW * cs, viewProj, viewportPos, viewportSize, s1 );
            glm::vec2  dir     = s1 - s0;
            float      perCell = glm::length( dir );
            if ( ok0 && ok1 && perCell > 6.0f )
                dir /= perCell; // one cell up == `perCell` px along `dir`
            else
            {
                dir     = glm::vec2( 0.0f, -1.0f ); // near-parallel to view: fall back to screen-up = taller
                perCell = 28.0f;
            }
            const glm::vec2 mp = glm::vec2( ::ImGui::GetMousePos().x, ::ImGui::GetMousePos().y );
            const int levels = static_cast<int>( std::round( glm::dot( mp - m_DragStartMouse, dir ) / perCell ) );
            const int N      = std::clamp( m_DragBaseH + levels, 1, 512 );
            ms.Height        = N; // live-reflect into the panel

            std::unordered_set<uint64_t> preview;
            buildColumn( m_DragAnchor, m_DragNormal, dua, dva, N, preview );

            auto isPrev = [&]( const glm::ivec3& c ) { return preview.count( Pack( c ) ) > 0; };
            for ( uint64_t k : preview )
                drawCellSolid( Unpack( k ), isPrev, IM_COL32( 90, 195, 240, 200 ), IM_COL32( 60, 165, 215, 200 ),
                               IM_COL32( 220, 245, 255, 235 ) );

            char htxt[24];
            std::snprintf( htxt, sizeof( htxt ), "H %d", N );
            dl->AddText( ImVec2( mp.x + 14.0f, mp.y - 4.0f ), IM_COL32( 230, 245, 255, 255 ), htxt );

            if ( !::ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) // release -> commit the column
            {
                for ( uint64_t k : preview )
                    m_Cells.insert( k );
                footprintAt( m_DragAnchor, dua, dva, m_LastBase ); // remember it so Height can re-extrude live
                m_LastNormal = m_DragNormal;
                m_LastHeight = N;
                m_Dragging   = false;
                changed      = true;
            }
        }
        else if ( toolActive )
        {
            // Hover preview of the footprint (green = add, red = erase).
            if ( removeMode && m_HasRemove )
                drawCellFace( m_Remove, IM_COL32( 240, 80, 60, 70 ), IM_COL32( 240, 90, 70, 255 ) );
            else if ( m_HasAdd )
            {
                std::vector<glm::ivec3> fp;
                footprintAt( m_Add, ua, va, fp );
                for ( const auto& c : fp )
                    drawCellFace( c, IM_COL32( 70, 220, 100, 70 ), IM_COL32( 90, 240, 120, 255 ) );
            }

            if ( interact )
            {
                const bool lmbClick = ::ImGui::IsMouseClicked( ImGuiMouseButton_Left );
                const bool rmbClick = ::ImGui::IsMouseClicked( ImGuiMouseButton_Right );
                if ( ( ( lmbClick && removeMode ) || rmbClick ) && m_HasRemove ) // erase hovered cube
                {
                    if ( m_Cells.erase( Pack( m_Remove ) ) > 0 )
                        changed = true;
                }
                else if ( lmbClick && !removeMode && m_HasAdd ) // begin an extrude drag (a plain click without
                {                                               // moving places a column of the current Height)
                    m_Dragging       = true;
                    m_DragAnchor     = m_Add;
                    m_DragNormal     = m_AddNormal;
                    m_DragStartMouse = glm::vec2( ::ImGui::GetMousePos().x, ::ImGui::GetMousePos().y );
                    m_DragBaseH      = std::clamp( ms.Height, 1, 512 );
                }
            }
        }

        // --- Live height edit: changing ModelingState.Height (panel field or E/Q) re-extrudes the last
        //     committed column, so you can tune the height after drawing it and before Accept. ---
        if ( !m_Dragging && !m_LastBase.empty() )
        {
            const int newH = std::clamp( ms.Height, 1, 512 );
            if ( newH != m_LastHeight )
            {
                for ( const auto& base : m_LastBase )
                    for ( int L = 0; L < m_LastHeight; ++L )
                        m_Cells.erase( Pack( base + m_LastNormal * L ) );
                for ( const auto& base : m_LastBase )
                    for ( int L = 0; L < newH; ++L )
                        m_Cells.insert( Pack( base + m_LastNormal * L ) );
                m_LastHeight = newH;
                ms.Height    = newH;
                changed      = true;
            }
        }

        // --- Keys: E / Q (or Up / Down) raise / lower the last column's Height (handled by the live-edit
        //     block above); Ctrl+E / Ctrl+Q grow / shrink the grid step. ---
        if ( interact && !m_Dragging )
        {
            const bool ctrl = ::ImGui::GetIO().KeyCtrl;
            if ( ctrl && ::ImGui::IsKeyPressed( ImGuiKey_E, false ) )
            {
                ms.CellSize = std::min( ms.CellSize * 2.0f, 100000.0f );
                changed     = true;
            }
            else if ( ctrl && ::ImGui::IsKeyPressed( ImGuiKey_Q, false ) )
            {
                ms.CellSize = std::max( ms.CellSize * 0.5f, 1.0f );
                changed     = true;
            }
            else if ( ::ImGui::IsKeyPressed( ImGuiKey_E, false ) ||
                      ::ImGui::IsKeyPressed( ImGuiKey_UpArrow, false ) )
            {
                ms.Height = std::clamp( ms.Height + 1, 1, 512 );
            }
            else if ( ::ImGui::IsKeyPressed( ImGuiKey_Q, false ) ||
                      ::ImGui::IsKeyPressed( ImGuiKey_DownArrow, false ) )
            {
                ms.Height = std::clamp( ms.Height - 1, 1, 512 );
            }
        }

        ms.Cubes = static_cast<int>( m_Cells.size() );

        // --- Viewport bottom bar: tool name + Accept / Cancel (UE5-style confirmation), shown while a
        //     blockout is in progress. ---
        if ( toolActive && ( !m_Cells.empty() || m_Entity != Common::UUID::Null() ) )
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
                if ( ::ImGui::Button( ICON_MDI_CHECK "  Accept" ) && !m_Cells.empty() )
                {
                    Core::SelectionManager::SetSelected( m_Entity );
                    m_Entity = Common::UUID::Null(); // keep the mesh; next edits start a fresh blockout
                    m_Cells.clear();
                    m_Region.clear();
                    m_LastBase.clear();
                    m_LastHeight = 0;
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
        if ( m_Cells.empty() )
        {
            Cancel( scene ); // no cubes left -> remove the entity
            return;
        }

        std::vector<Vertex> verts;
        std::vector<Index>  inds;
        glm::vec3           mn( FLT_MAX ), mx( -FLT_MAX );
        for ( uint64_t key : m_Cells )
        {
            const glm::ivec3 c = Unpack( key );
            for ( int f = 0; f < 6; ++f )
            {
                if ( m_Cells.count( Pack( c + kNeighbor[f] ) ) ) // neighbour occupied -> internal face, cull
                    continue;
                const uint32_t base = static_cast<uint32_t>( verts.size() );
                for ( int k = 0; k < 4; ++k )
                {
                    const FaceVert& fv = kFace[f][k];
                    Vertex          v;
                    v.Position  = ( glm::vec3( c ) + fv.P + 0.5f ) * m_CellSize; // cell cube = [c*cs, (c+1)*cs]
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
            if ( auto ref = scene.FindEntityByID( m_Entity ) )
                scene.DestroyEntity( ref->get() );
        m_Entity = Common::UUID::Null();
        m_Cells.clear();
        m_Region.clear();
        m_LastBase.clear();
        m_LastHeight = 0;
        m_Dragging = false;
    }
} // namespace Desert::Editor::Tools
