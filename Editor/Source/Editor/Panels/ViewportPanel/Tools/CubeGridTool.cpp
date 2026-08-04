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
#include <climits>
#include <cmath>

namespace Desert::Editor::Tools
{
    namespace
    {
        // Sparse voxel volume key: pack a grid cell into a 63-bit key (21 bits/axis, centred so negatives fit).
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

        // The 6 faces of a unit cube (position in [-0.5,0.5] + the engine's cube normals/tangents/UVs), each as
        // 4 CCW corners — copied from PrimitiveMeshFactory::CreateCube so winding/normals are known-good.
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
        // Outward neighbour offset per face — a face is generated only on a Solid/Empty border (Face Culling).
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

    void CubeGridTool::PushPull( ::Desert::Core::Scene& scene, int dir )
    {
        if ( !m_HasSel )
            return;
        auto      occ  = [&]( const glm::ivec3& c ) { return m_Cells.count( Pack( c ) ) > 0; };
        const int na   = m_PlaneNa;
        const int sign = m_PlaneSign;
        const int ua   = ( na + 1 ) % 3;
        const int va   = ( na + 2 ) % 3;

        if ( dir > 0 ) // Extrude out: fill the first empty cell from the plane, per column
        {
            for ( int u = m_UMin; u <= m_UMax; ++u )
                for ( int v = m_VMin; v <= m_VMax; ++v )
                {
                    glm::ivec3 c{ 0 };
                    c[na] = m_PlaneCell;
                    c[ua] = u;
                    c[va] = v;
                    while ( occ( c ) )
                        c[na] += sign;
                    m_Cells.insert( Pack( c ) );
                }
            m_PlaneCell += sign; // plane advances so the next push builds on top
        }
        else // Push in: remove the solid cell just inside the plane, per column
        {
            const int inner = m_PlaneCell - sign;
            for ( int u = m_UMin; u <= m_UMax; ++u )
                for ( int v = m_VMin; v <= m_VMax; ++v )
                {
                    glm::ivec3 c{ 0 };
                    c[na] = inner;
                    c[ua] = u;
                    c[va] = v;
                    m_Cells.erase( Pack( c ) );
                }
            m_PlaneCell -= sign;
        }
        RegenMesh( scene );
    }

    void CubeGridTool::Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray,
                               const glm::mat4& viewProj, const glm::vec2& viewportPos,
                               const glm::vec2& viewportSize, bool interactive )
    {
        Core::ModelingState& ms         = Core::ModelingState::Get();
        const bool           toolActive = ms.ActiveTool == Core::ModelingState::Tool::CubeGrid;
        const float          gs         = std::max( ms.CellSize, 0.02f ); // grid / block size (Resize Grid)

        if ( ms.ReqClear )
        {
            ms.ReqClear = false;
            m_Cells.clear();
            m_HasSel = m_Selecting = false;
            m_Origin               = glm::vec3( 0.0f );
            m_BakedGrid            = -1.0f;
            m_PlaneNa = 1, m_PlaneSign = 1, m_PlaneCell = 0;
            RegenMesh( scene );
        }

        auto occupied = [&]( const glm::ivec3& c ) { return m_Cells.count( Pack( c ) ) > 0; };
        // World coord of the plane (na, planeCell, sign) — the surface a Push extrudes from.
        auto planeWorldOf = [&]( int na, int sign, int cell )
        { return m_Origin[na] + static_cast<float>( cell + ( sign > 0 ? 0 : 1 ) ) * gs; };

        // --- Targeting: the surface under the cursor (a filled cell's face, else the ground). Produces a
        //     candidate work-plane + the cell (u,v) the cursor is over. ---
        bool  tHas = false;
        int   tNa = 1, tSign = 1, tPlaneCell = 0, tU = 0, tV = 0;
        float bestT = FLT_MAX;
        {
            glm::ivec3 hitCell{ 0 };
            bool       hit = false;
            for ( uint64_t key : m_Cells )
            {
                const glm::ivec3   c = Unpack( key );
                Common::Math::AABB box;
                box.Min = m_Origin + glm::vec3( c ) * gs;
                box.Max = box.Min + gs;
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
                const glm::vec3 d  = ( p - ( m_Origin + ( glm::vec3( hitCell ) + 0.5f ) * gs ) );
                const glm::vec3 ad = glm::abs( d );
                glm::ivec3      n{ 0 };
                if ( ad.x >= ad.y && ad.x >= ad.z )
                    n.x = d.x > 0 ? 1 : -1;
                else if ( ad.y >= ad.z )
                    n.y = d.y > 0 ? 1 : -1;
                else
                    n.z = d.z > 0 ? 1 : -1;
                tNa        = n.x ? 0 : n.y ? 1 : 2;
                tSign      = n[tNa];
                tPlaneCell = hitCell[tNa] + tSign;
                tU         = hitCell[( tNa + 1 ) % 3];
                tV         = hitCell[( tNa + 2 ) % 3];
                tHas       = true;
            }
            else if ( std::abs( ray.Direction.y ) > 1e-5f ) // ground plane y=0
            {
                const float t = -ray.Origin.y / ray.Direction.y;
                if ( t > 0.0f && t < bestT )
                {
                    const glm::vec3 p = ray.Origin + ray.Direction * t;
                    tNa               = 1;
                    tSign             = 1;
                    tPlaneCell        = static_cast<int>( std::lround( -m_Origin.y / gs ) );
                    tU                = static_cast<int>( std::floor( ( p.x - m_Origin.x ) / gs ) );
                    tV                = static_cast<int>( std::floor( ( p.z - m_Origin.z ) / gs ) );
                    tHas              = true;
                }
            }
        }

        ImDrawList* dl = ::ImGui::GetWindowDrawList();

        auto drawCellSolid = [&]( const glm::ivec3& c, ImU32 fill, ImU32 outline )
        {
            const glm::vec3 mn = m_Origin + glm::vec3( c ) * gs;
            for ( int f = 0; f < 6; ++f )
            {
                if ( occupied( c + kNeighbor[f] ) )
                    continue;
                const glm::vec3 fn = kFace[f][0].N;
                const glm::vec3 fc = mn + 0.5f * gs + 0.5f * gs * fn;
                if ( glm::dot( fn, ray.Origin - fc ) <= 0.0f )
                    continue;
                glm::vec2 s[4];
                bool      ok = true;
                for ( int k = 0; k < 4; ++k )
                    if ( !WorldToScreen( mn + ( kFace[f][k].P + 0.5f ) * gs, viewProj, viewportPos, viewportSize,
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

        // Filled rect [uMin..uMax+1] × [vMin..vMax+1] on plane (na, planeW).
        auto drawRect =
             [&]( int uMin, int uMax, int vMin, int vMax, int na, float planeW, ImU32 fill, ImU32 outline )
        {
            const int ua = ( na + 1 ) % 3;
            const int va = ( na + 2 ) % 3;
            glm::vec2 s[4];
            const int uu[4] = { uMin, uMax + 1, uMax + 1, uMin };
            const int vv[4] = { vMin, vMin, vMax + 1, vMax + 1 };
            for ( int k = 0; k < 4; ++k )
            {
                glm::vec3 w( 0.0f );
                w[na] = planeW;
                w[ua] = m_Origin[ua] + static_cast<float>( uu[k] ) * gs;
                w[va] = m_Origin[va] + static_cast<float>( vv[k] ) * gs;
                if ( !WorldToScreen( w, viewProj, viewportPos, viewportSize, s[k] ) )
                    return;
            }
            dl->AddQuadFilled( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                               ImVec2( s[3].x, s[3].y ), fill );
            dl->AddQuad( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                         ImVec2( s[3].x, s[3].y ), outline, 2.0f );
        };

        const bool interact = interactive && toolActive && !::ImGui::IsAnyItemActive();
        bool       changed  = false;

        // Existing cells: grey checkerboard solids.
        if ( toolActive )
            for ( uint64_t k : m_Cells )
            {
                const glm::ivec3 c = Unpack( k );
                drawCellSolid( c,
                               ( ( c.x + c.y + c.z ) & 1 ) ? IM_COL32( 120, 125, 135, 205 )
                                                           : IM_COL32( 150, 155, 165, 205 ),
                               IM_COL32( 90, 95, 105, 230 ) );
            }

        // --- Marquee selection: LMB drag a rectangle on the surface. Start requires hover; the drag is
        //     latched to the physically-held button and locked to the plane picked at the press. ---
        if ( interact && ::ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && tHas )
        {
            m_Selecting = true;
            m_HasSel    = false;
            m_PlaneNa   = tNa;
            m_PlaneSign = tSign;
            m_PlaneCell = tPlaneCell;
            m_Anchor    = { tU, tV };
        }

        if ( m_Selecting )
        {
            const int   na     = m_PlaneNa;
            const int   ua     = ( na + 1 ) % 3;
            const int   va     = ( na + 2 ) % 3;
            const float planeW = planeWorldOf( na, m_PlaneSign, m_PlaneCell );
            // Project the cursor onto the locked plane to get the current cell.
            glm::ivec2 cur = m_Anchor;
            if ( std::abs( ray.Direction[na] ) > 1e-6f )
            {
                const float t = ( planeW - ray.Origin[na] ) / ray.Direction[na];
                if ( t > 0.0f )
                {
                    const glm::vec3 p = ray.Origin + ray.Direction * t;
                    cur               = { static_cast<int>( std::floor( ( p[ua] - m_Origin[ua] ) / gs ) ),
                                          static_cast<int>( std::floor( ( p[va] - m_Origin[va] ) / gs ) ) };
                }
            }
            m_UMin = std::min( m_Anchor.x, cur.x );
            m_UMax = std::max( m_Anchor.x, cur.x );
            m_VMin = std::min( m_Anchor.y, cur.y );
            m_VMax = std::max( m_Anchor.y, cur.y );
            drawRect( m_UMin, m_UMax, m_VMin, m_VMax, na, planeW, IM_COL32( 250, 150, 40, 70 ),
                      IM_COL32( 255, 170, 60, 255 ) );

            if ( !::ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) // release -> fix the selection
            {
                m_Selecting = false;
                m_HasSel    = true;
            }
        }
        else if ( m_HasSel ) // keep the fixed selection highlighted (orange)
        {
            drawRect( m_UMin, m_UMax, m_VMin, m_VMax, m_PlaneNa,
                      planeWorldOf( m_PlaneNa, m_PlaneSign, m_PlaneCell ), IM_COL32( 250, 150, 40, 60 ),
                      IM_COL32( 255, 170, 60, 255 ) );
        }
        else if ( toolActive && tHas ) // hover: highlight the cell under the cursor
        {
            drawRect( tU, tU, tV, tV, tNa, planeWorldOf( tNa, tSign, tPlaneCell ), IM_COL32( 70, 220, 100, 60 ),
                      IM_COL32( 90, 240, 120, 255 ) );
        }

        // Resize Grid pins the min corner in place (grow from the corner, not slide away from the origin).
        if ( gs != m_BakedGrid && !m_Cells.empty() )
        {
            if ( m_BakedGrid > 0.0f )
            {
                glm::ivec3 minCell{ INT_MAX };
                for ( uint64_t k : m_Cells )
                    minCell = glm::min( minCell, Unpack( k ) );
                m_Origin += glm::vec3( minCell ) * ( m_BakedGrid - gs );
            }
            changed = true;
        }

        ms.Cubes = static_cast<int>( m_Cells.size() );

        // --- Viewport bottom bar: tool + Level shift + Push/Pull + Resize Grid + Accept/Cancel. ---
        if ( toolActive )
        {
            ::ImGui::SetNextWindowPos(
                 ImVec2( viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + viewportSize.y - 58.0f ),
                 ImGuiCond_Always, ImVec2( 0.5f, 0.0f ) );
            ::ImGui::SetNextWindowBgAlpha( 0.92f );
            ::ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 12.0f, 8.0f ) );
            ::ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 10.0f, 6.0f ) );
            ::ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 6.0f, 6.0f ) );
            if ( ::ImGui::Begin( "##cubegrid_bar", nullptr,
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoNav ) )
            {
                ::ImGui::AlignTextToFramePadding();
                ::ImGui::TextUnformatted( ICON_MDI_GRID "  CubeGrid" );

                // Move the horizontal work-plane up / down a level (only meaningful on the ground plane).
                ::ImGui::SameLine( 0.0f, 14.0f );
                const bool ground = m_PlaneNa == 1;
                if ( !ground )
                    ::ImGui::BeginDisabled();
                if ( ::ImGui::Button( ICON_MDI_ARROW_UP "##lvlup" ) )
                    m_PlaneCell += 1;
                ::ImGui::SameLine();
                if ( ::ImGui::Button( ICON_MDI_ARROW_DOWN "##lvldn" ) )
                    m_PlaneCell -= 1;
                if ( !ground )
                    ::ImGui::EndDisabled();

                // Push / Pull the selected region.
                ::ImGui::SameLine( 0.0f, 14.0f );
                if ( !m_HasSel )
                    ::ImGui::BeginDisabled();
                ::ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.45f, 0.75f, 1.0f ) );
                if ( ::ImGui::Button( ICON_MDI_ARROW_EXPAND_UP "  Push" ) )
                    PushPull( scene, +1 );
                ::ImGui::SameLine();
                if ( ::ImGui::Button( ICON_MDI_ARROW_COLLAPSE_DOWN "  Pull" ) )
                    PushPull( scene, -1 );
                ::ImGui::PopStyleColor();
                if ( !m_HasSel )
                    ::ImGui::EndDisabled();

                // Resize Grid (block size).
                ::ImGui::SameLine( 0.0f, 14.0f );
                if ( ::ImGui::Button( ICON_MDI_MINUS "##grid_dn" ) )
                    ms.CellSize = std::max( ms.CellSize * 0.5f, 0.02f );
                ::ImGui::SameLine();
                ::ImGui::Text( "Grid %.2f", gs );
                ::ImGui::SameLine();
                if ( ::ImGui::Button( ICON_MDI_PLUS "##grid_up" ) )
                    ms.CellSize = std::min( ms.CellSize * 2.0f, 100000.0f );

                // Accept / Cancel.
                ::ImGui::SameLine( 0.0f, 16.0f );
                ::ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.55f, 0.30f, 1.0f ) );
                ::ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.26f, 0.68f, 0.38f, 1.0f ) );
                if ( ::ImGui::Button( ICON_MDI_CHECK "  Accept" ) && !m_Cells.empty() )
                {
                    Core::SelectionManager::SetSelected( m_Entity );
                    m_Entity = Common::UUID::Null();
                    m_Cells.clear();
                    m_HasSel = m_Selecting = false;
                    m_Origin               = glm::vec3( 0.0f );
                    m_BakedGrid            = -1.0f;
                    m_PlaneNa = 1, m_PlaneSign = 1, m_PlaneCell = 0;
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
        Core::ModelingState& ms = Core::ModelingState::Get();
        const float          gs = std::max( ms.CellSize, 0.02f );
        m_BakedGrid             = gs;

        if ( m_Cells.empty() )
        {
            Cancel( scene );
            return;
        }

        auto occupied = [&]( const glm::ivec3& c ) { return m_Cells.count( Pack( c ) ) > 0; };

        std::vector<Vertex> verts;
        std::vector<Index>  inds;
        glm::vec3           mn( FLT_MAX ), mx( -FLT_MAX );
        for ( uint64_t key : m_Cells )
        {
            const glm::ivec3 c = Unpack( key );
            for ( int f = 0; f < 6; ++f )
            {
                if ( occupied( c + kNeighbor[f] ) ) // Face Culling: only Solid/Empty borders
                    continue;
                const uint32_t base = static_cast<uint32_t>( verts.size() );
                for ( int k = 0; k < 4; ++k )
                {
                    const FaceVert& fv = kFace[f][k];
                    Vertex          v;
                    v.Position  = m_Origin + ( glm::vec3( c ) + fv.P + 0.5f ) * gs;
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
        m_HasSel = m_Selecting = false;
        m_Origin               = glm::vec3( 0.0f );
        m_BakedGrid            = -1.0f;
        m_PlaneNa = 1, m_PlaneSign = 1, m_PlaneCell = 0;
    }
} // namespace Desert::Editor::Tools
