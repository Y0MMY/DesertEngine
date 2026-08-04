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
        // Floor division rounding toward -infinity (block-align negative coords correctly).
        int FloorDiv( int a, int b )
        {
            return a >= 0 ? a / b : -( ( -a + b - 1 ) / b );
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

    void CubeGridTool::RefineBy( int F )
    {
        std::unordered_set<uint64_t> out;
        out.reserve( m_Cells.size() * static_cast<size_t>( F * F * F ) );
        for ( uint64_t k : m_Cells )
        {
            const glm::ivec3 c = Unpack( k );
            for ( int dx = 0; dx < F; ++dx )
                for ( int dy = 0; dy < F; ++dy )
                    for ( int dz = 0; dz < F; ++dz )
                        out.insert( Pack( { c.x * F + dx, c.y * F + dy, c.z * F + dz } ) );
        }
        m_Cells = std::move( out );
        m_Anchor *= F;
        m_UMin *= F, m_VMin *= F;
        m_UMax = m_UMax * F + F - 1, m_VMax = m_VMax * F + F - 1;
        m_PlaneCell *= F;
        m_Unit /= static_cast<float>( F );
    }

    void CubeGridTool::PushPull( ::Desert::Core::Scene& scene, int dir, int K )
    {
        if ( !m_HasSel )
            return;
        auto      occ    = [&]( const glm::ivec3& c ) { return m_Cells.count( Pack( c ) ) > 0; };
        const int na     = m_PlaneNa;
        const int sign   = m_PlaneSign;
        const int ua     = ( na + 1 ) % 3;
        const int va     = ( na + 2 ) % 3;
        const int steps  = std::max( 1, Core::ModelingState::Get().BlocksPerStep );
        const int height = K * steps; // one block = K base cells tall; × Blocks Per Step

        if ( dir > 0 ) // Extrude out: fill `height` base cells from the first empty cell, per column
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
                    for ( int s = 0; s < height; ++s, c[na] += sign )
                        m_Cells.insert( Pack( c ) );
                }
            m_PlaneCell += sign * height;
        }
        else // Push in: remove `height` base cells just inside the plane, per column
        {
            for ( int u = m_UMin; u <= m_UMax; ++u )
                for ( int v = m_VMin; v <= m_VMax; ++v )
                {
                    glm::ivec3 c{ 0 };
                    c[na] = m_PlaneCell - sign;
                    c[ua] = u;
                    c[va] = v;
                    for ( int s = 0; s < height; ++s, c[na] -= sign )
                        m_Cells.erase( Pack( c ) );
                }
            m_PlaneCell -= sign * height;
        }
        RegenMesh( scene );
    }

    void CubeGridTool::Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray,
                               const glm::mat4& viewProj, const glm::vec2& viewportPos,
                               const glm::vec2& viewportSize, bool interactive )
    {
        Core::ModelingState& ms         = Core::ModelingState::Get();
        const bool           toolActive = ms.ActiveTool == Core::ModelingState::Tool::CubeGrid;

        bool        changed = false;
        const float gs      = std::max( ms.CellSize, 0.02f ); // requested Block Size (world units)

        // Base unit: the finest cell ever used. A Block Size finer than the base subdivides the base
        // losslessly (existing solids split into F³, world-identical); a coarser Block Size never remaps —
        // it just stamps K base cells at once. So drawn geometry never changes when the grid step changes.
        if ( m_Unit < 0.0f || m_Cells.empty() )
            m_Unit = gs; // nothing drawn yet -> the requested size simply becomes the base
        else
            for ( int guard = 0; gs < m_Unit * 0.999f && guard < 24; ++guard )
            {
                RefineBy( 2 ); // finer than the base -> subdivide losslessly (geometry stays put)
                changed = true;
            }
        const int K   = std::max( 1, static_cast<int>( std::lround( gs / m_Unit ) ) );
        ms.CellSize   = static_cast<float>( K ) * m_Unit; // snap the shown Block Size to a base multiple
        const float u = m_Unit;

        if ( ms.ReqClear )
        {
            ms.ReqClear = false;
            m_Cells.clear();
            m_HasSel = m_Selecting = false;
            m_PlaneNa = 1, m_PlaneSign = 1, m_PlaneCell = 0;
            RegenMesh( scene );
        }

        auto occupied = [&]( const glm::ivec3& c ) { return m_Cells.count( Pack( c ) ) > 0; };
        // World coord of a plane (na, planeCell, sign) — the surface a Push extrudes from.
        auto planeWorldOf = [&]( int na, int sign, int cell )
        { return static_cast<float>( cell + ( sign > 0 ? 0 : 1 ) ) * u; };

        // --- Targeting: nearest filled cell's face under the cursor, else the ground plane (y=0). Yields a
        //     BASE cell (tU,tV) + a work-plane (na, sign, planeCell). ---
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
                box.Min = glm::vec3( c ) * u;
                box.Max = box.Min + u;
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
                const glm::vec3 d  = p - ( glm::vec3( hitCell ) + 0.5f ) * u;
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
            else if ( std::abs( ray.Direction.y ) > 1e-5f )
            {
                const float t = -ray.Origin.y / ray.Direction.y;
                if ( t > 0.0f && t < bestT )
                {
                    const glm::vec3 p = ray.Origin + ray.Direction * t;
                    tNa               = 1;
                    tSign             = 1;
                    tPlaneCell        = 0;
                    tU                = static_cast<int>( std::floor( p.x / u ) );
                    tV                = static_cast<int>( std::floor( p.z / u ) );
                    tHas              = true;
                }
            }
        }

        ImDrawList* dl = ::ImGui::GetWindowDrawList();

        auto drawCellSolid = [&]( const glm::ivec3& c, ImU32 fill, ImU32 outline )
        {
            const glm::vec3 mn = glm::vec3( c ) * u;
            for ( int f = 0; f < 6; ++f )
            {
                if ( occupied( c + kNeighbor[f] ) )
                    continue;
                const glm::vec3 fn = kFace[f][0].N;
                const glm::vec3 fc = mn + 0.5f * u + 0.5f * u * fn;
                if ( glm::dot( fn, ray.Origin - fc ) <= 0.0f )
                    continue;
                glm::vec2 s[4];
                bool      ok = true;
                for ( int k = 0; k < 4; ++k )
                    if ( !WorldToScreen( mn + ( kFace[f][k].P + 0.5f ) * u, viewProj, viewportPos, viewportSize,
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

        // Filled rect over BASE cells [uMin..uMax+1] × [vMin..vMax+1] on plane (na, planeW).
        auto worldPt = [&]( float fu, float fv, int na, float planeW )
        {
            const int ua = ( na + 1 ) % 3;
            const int va = ( na + 2 ) % 3;
            glm::vec3 w( 0.0f );
            w[na] = planeW;
            w[ua] = fu * u;
            w[va] = fv * u;
            return w;
        };
        auto drawRect =
             [&]( int uMin, int uMax, int vMin, int vMax, int na, float planeW, ImU32 fill, ImU32 outline )
        {
            glm::vec2 s[4];
            const int uu[4] = { uMin, uMax + 1, uMax + 1, uMin };
            const int vv[4] = { vMin, vMin, vMax + 1, vMax + 1 };
            for ( int k = 0; k < 4; ++k )
                if ( !WorldToScreen(
                          worldPt( static_cast<float>( uu[k] ), static_cast<float>( vv[k] ), na, planeW ),
                          viewProj, viewportPos, viewportSize, s[k] ) )
                    return;
            dl->AddQuadFilled( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                               ImVec2( s[3].x, s[3].y ), fill );
            dl->AddQuad( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                         ImVec2( s[3].x, s[3].y ), outline, 2.0f );
        };
        auto drawLabel = [&]( const glm::vec2& sp, const char* txt )
        {
            const ImVec2 ts = ::ImGui::CalcTextSize( txt );
            dl->AddRectFilled( ImVec2( sp.x - ts.x * 0.5f - 4.0f, sp.y - ts.y * 0.5f - 2.0f ),
                               ImVec2( sp.x + ts.x * 0.5f + 4.0f, sp.y + ts.y * 0.5f + 2.0f ),
                               IM_COL32( 20, 22, 26, 220 ), 3.0f );
            dl->AddText( ImVec2( sp.x - ts.x * 0.5f, sp.y - ts.y * 0.5f ), IM_COL32( 255, 235, 200, 255 ), txt );
        };
        // Grid lines every K base cells (block boundaries) + the drawn size (world units) on the edges.
        auto drawGridAndDims = [&]( int uMin, int uMax, int vMin, int vMax, int na, float planeW, bool showDims )
        {
            const ImU32 gcol = IM_COL32( 255, 185, 90, 120 );
            const int   nu   = ( uMax + 1 - uMin ) / K;
            const int   nv   = ( vMax + 1 - vMin ) / K;
            if ( nu <= 256 && nv <= 256 )
            {
                for ( int iu = uMin; iu <= uMax + 1; iu += K )
                {
                    glm::vec2 a, b;
                    if ( WorldToScreen( worldPt( (float)iu, (float)vMin, na, planeW ), viewProj, viewportPos,
                                        viewportSize, a ) &&
                         WorldToScreen( worldPt( (float)iu, (float)( vMax + 1 ), na, planeW ), viewProj,
                                        viewportPos, viewportSize, b ) )
                        dl->AddLine( ImVec2( a.x, a.y ), ImVec2( b.x, b.y ), gcol, 1.0f );
                }
                for ( int iv = vMin; iv <= vMax + 1; iv += K )
                {
                    glm::vec2 a, b;
                    if ( WorldToScreen( worldPt( (float)uMin, (float)iv, na, planeW ), viewProj, viewportPos,
                                        viewportSize, a ) &&
                         WorldToScreen( worldPt( (float)( uMax + 1 ), (float)iv, na, planeW ), viewProj,
                                        viewportPos, viewportSize, b ) )
                        dl->AddLine( ImVec2( a.x, a.y ), ImVec2( b.x, b.y ), gcol, 1.0f );
                }
            }
            if ( !showDims )
                return;
            const float wWorld = static_cast<float>( uMax - uMin + 1 ) * u;
            const float dWorld = static_cast<float>( vMax - vMin + 1 ) * u;
            char        buf[32];
            glm::vec2   sp;
            if ( WorldToScreen( worldPt( ( uMin + uMax + 1 ) * 0.5f, (float)vMin, na, planeW ), viewProj,
                                viewportPos, viewportSize, sp ) )
            {
                std::snprintf( buf, sizeof( buf ), "%.0f", wWorld );
                drawLabel( sp, buf );
            }
            if ( WorldToScreen( worldPt( (float)uMin, ( vMin + vMax + 1 ) * 0.5f, na, planeW ), viewProj,
                                viewportPos, viewportSize, sp ) )
            {
                std::snprintf( buf, sizeof( buf ), "%.0f", dWorld );
                drawLabel( sp, buf );
            }
        };

        const bool interact = interactive && toolActive && !::ImGui::IsAnyItemActive();

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

        // --- Marquee selection (Block-aligned): LMB drag a rectangle; start requires hover, the drag is
        //     latched to the physical button and locked to the plane picked at the press. ---
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
            glm::ivec2  cur    = m_Anchor;
            if ( std::abs( ray.Direction[na] ) > 1e-6f )
            {
                const float t = ( planeW - ray.Origin[na] ) / ray.Direction[na];
                if ( t > 0.0f )
                {
                    const glm::vec3 p = ray.Origin + ray.Direction * t;
                    cur               = { static_cast<int>( std::floor( p[ua] / u ) ),
                                          static_cast<int>( std::floor( p[va] / u ) ) };
                }
            }
            // Snap the anchor..cursor span out to whole Blocks (K base cells).
            const int aUb = FloorDiv( m_Anchor.x, K ), cUb = FloorDiv( cur.x, K );
            const int aVb = FloorDiv( m_Anchor.y, K ), cVb = FloorDiv( cur.y, K );
            m_UMin = std::min( aUb, cUb ) * K;
            m_UMax = std::max( aUb, cUb ) * K + K - 1;
            m_VMin = std::min( aVb, cVb ) * K;
            m_VMax = std::max( aVb, cVb ) * K + K - 1;
            drawRect( m_UMin, m_UMax, m_VMin, m_VMax, na, planeW, IM_COL32( 250, 150, 40, 70 ),
                      IM_COL32( 255, 170, 60, 255 ) );
            drawGridAndDims( m_UMin, m_UMax, m_VMin, m_VMax, na, planeW, true );

            if ( !::ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
            {
                m_Selecting = false;
                m_HasSel    = true;
            }
        }
        else if ( m_HasSel )
        {
            const float planeW = planeWorldOf( m_PlaneNa, m_PlaneSign, m_PlaneCell );
            drawRect( m_UMin, m_UMax, m_VMin, m_VMax, m_PlaneNa, planeW, IM_COL32( 250, 150, 40, 60 ),
                      IM_COL32( 255, 170, 60, 255 ) );
            drawGridAndDims( m_UMin, m_UMax, m_VMin, m_VMax, m_PlaneNa, planeW, true );
        }
        else if ( toolActive && tHas ) // hover: the Block under the cursor + a helper grid
        {
            const int   bu     = FloorDiv( tU, K ) * K;
            const int   bv     = FloorDiv( tV, K ) * K;
            const float planeW = planeWorldOf( tNa, tSign, tPlaneCell );
            drawRect( bu, bu + K - 1, bv, bv + K - 1, tNa, planeW, IM_COL32( 70, 220, 100, 60 ),
                      IM_COL32( 90, 240, 120, 255 ) );
            drawGridAndDims( bu - 6 * K, bu + 7 * K - 1, bv - 6 * K, bv + 7 * K - 1, tNa, planeW, false );
        }

        // Re-bake if the base resolution changed (a refine this frame).
        if ( m_Unit != m_BakedUnit && !m_Cells.empty() )
            changed = true;

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

                ::ImGui::SameLine( 0.0f, 14.0f );
                const bool ground = m_PlaneNa == 1;
                if ( !ground )
                    ::ImGui::BeginDisabled();
                if ( ::ImGui::Button( ICON_MDI_ARROW_UP "##lvlup" ) )
                    m_PlaneCell += K;
                ::ImGui::SameLine();
                if ( ::ImGui::Button( ICON_MDI_ARROW_DOWN "##lvldn" ) )
                    m_PlaneCell -= K;
                if ( !ground )
                    ::ImGui::EndDisabled();

                ::ImGui::SameLine( 0.0f, 14.0f );
                if ( !m_HasSel )
                    ::ImGui::BeginDisabled();
                ::ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.45f, 0.75f, 1.0f ) );
                if ( ::ImGui::Button( ICON_MDI_ARROW_EXPAND_UP "  Push" ) )
                    PushPull( scene, +1, K );
                ::ImGui::SameLine();
                if ( ::ImGui::Button( ICON_MDI_ARROW_COLLAPSE_DOWN "  Pull" ) )
                    PushPull( scene, -1, K );
                ::ImGui::PopStyleColor();
                if ( !m_HasSel )
                    ::ImGui::EndDisabled();

                ::ImGui::SameLine( 0.0f, 14.0f );
                if ( ::ImGui::Button( ICON_MDI_MINUS "##grid_dn" ) )
                    ms.CellSize = std::max( ms.CellSize * 0.5f, 0.02f );
                ::ImGui::SameLine();
                ::ImGui::Text( "Grid %.2f", static_cast<float>( K ) * u );
                ::ImGui::SameLine();
                if ( ::ImGui::Button( ICON_MDI_PLUS "##grid_up" ) )
                    ms.CellSize = std::min( ms.CellSize * 2.0f, 100000.0f );

                ::ImGui::SameLine( 0.0f, 16.0f );
                ::ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.55f, 0.30f, 1.0f ) );
                ::ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.26f, 0.68f, 0.38f, 1.0f ) );
                if ( ::ImGui::Button( ICON_MDI_CHECK "  Accept" ) && !m_Cells.empty() )
                {
                    Core::SelectionManager::SetSelected( m_Entity );
                    m_Entity = Common::UUID::Null();
                    m_Cells.clear();
                    m_HasSel = m_Selecting = false;
                    m_Unit = m_BakedUnit = -1.0f;
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
        m_BakedUnit = m_Unit;
        if ( m_Cells.empty() )
        {
            Cancel( scene );
            return;
        }

        const float u        = m_Unit;
        auto        occupied = [&]( const glm::ivec3& c ) { return m_Cells.count( Pack( c ) ) > 0; };

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
                    v.Position  = ( glm::vec3( c ) + fv.P + 0.5f ) * u;
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
        m_Unit = m_BakedUnit = -1.0f;
        m_PlaneNa = 1, m_PlaneSign = 1, m_PlaneCell = 0;
    }
} // namespace Desert::Editor::Tools
