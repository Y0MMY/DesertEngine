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
        if ( ms.ReqClear )
        {
            ms.ReqClear = false;
            m_Cells.clear();
            m_Region.clear();
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

        // --- Overlay: wire box spanning inclusive cell range [cmin, cmax]. ---
        ImDrawList* dl      = ::ImGui::GetWindowDrawList();
        auto        drawBox = [&]( const glm::ivec3& cmin, const glm::ivec3& cmax, ImU32 col )
        {
            const glm::vec3 mn = glm::vec3( cmin ) * cs;
            const glm::vec3 mx = ( glm::vec3( cmax ) + 1.0f ) * cs;
            glm::vec2       s[8];
            bool            ok = true;
            for ( int i = 0; i < 8; ++i )
            {
                const glm::vec3 w( ( i & 1 ) ? mx.x : mn.x, ( i & 2 ) ? mx.y : mn.y, ( i & 4 ) ? mx.z : mn.z );
                if ( !WorldToScreen( w, viewProj, viewportPos, viewportSize, s[i] ) )
                    ok = false;
            }
            if ( !ok )
                return;
            const int edges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 },
                                       { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for ( auto& e : edges )
                dl->AddLine( ImVec2( s[e[0]].x, s[e[0]].y ), ImVec2( s[e[1]].x, s[e[1]].y ), col, 2.0f );
        };
        const bool removeMode = ::ImGui::GetIO().KeyShift;
        const bool interact   = interactive && toolActive && !::ImGui::IsAnyItemActive();
        bool       changed    = false;

        if ( toolActive && m_HasAdd )
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

        // Overlay every placed cell as a faint wire box — the blockout stays visible in the viewport even
        // before/independent of the generated mesh (and confirms exactly where cubes landed).
        if ( toolActive && m_Cells.size() < 4000 )
            for ( uint64_t k : m_Cells )
                drawBox( Unpack( k ), Unpack( k ), IM_COL32( 180, 195, 215, 70 ) );

        // In-plane axes of the target face + its plane coordinate, and the WxD brush footprint.
        const int   na        = m_AddNormal.x ? 0 : m_AddNormal.y ? 1 : 2;
        const int   ua        = ( na + 1 ) % 3;
        const int   va        = ( na + 2 ) % 3;
        const float pcrd      = static_cast<float>( m_HasAdd ? m_Add[na] : 0 ) * cs;
        const int   bw        = std::max( 1, ms.BrushW );
        const int   bd        = std::max( 1, ms.BrushD );
        auto        footprint = [&]( std::vector<glm::ivec3>& out )
        {
            out.clear();
            for ( int i = 0; i < bw; ++i )
                for ( int j = 0; j < bd; ++j )
                {
                    glm::ivec3 c = m_Add;
                    c[ua] += i;
                    c[va] += j;
                    out.push_back( c );
                }
        };
        // Flat cell face on the grid plane (a square) — reads as a GRID CELL, not a 3D cube.
        auto drawCellFace = [&]( const glm::ivec3& c, ImU32 fill, ImU32 outline )
        {
            glm::vec2 s[4];
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

        // Highlight the target as flat grid cells (green = the WxD paint footprint, red = erase).
        if ( toolActive )
        {
            if ( removeMode && m_HasRemove )
                drawCellFace( m_Remove, IM_COL32( 240, 80, 60, 70 ), IM_COL32( 240, 90, 70, 255 ) );
            else if ( m_HasAdd )
            {
                std::vector<glm::ivec3> fp;
                footprint( fp );
                for ( const auto& c : fp )
                    drawCellFace( c, IM_COL32( 70, 220, 100, 70 ), IM_COL32( 90, 240, 120, 255 ) );
            }
        }

        // --- Continuous painting: hold LMB and move to add the WxD footprint under the cursor; Shift/RMB
        //     erases. Each stroke's painted cells become the region for E/Q. ---
        if ( interact )
        {
            const bool lmb   = ::ImGui::IsMouseDown( ImGuiMouseButton_Left );
            const bool rmb   = ::ImGui::IsMouseDown( ImGuiMouseButton_Right );
            const bool erase = ( lmb && removeMode ) || rmb;

            if ( ::ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !removeMode ) // begin an ADD stroke
            {
                m_Dragging = true;
                m_Region.clear();
                m_RegionNormal = m_AddNormal;
            }
            if ( erase )
            {
                if ( m_HasRemove && m_Cells.erase( Pack( m_Remove ) ) > 0 )
                    changed = true;
            }
            else if ( m_Dragging && lmb && m_HasAdd )
            {
                std::vector<glm::ivec3> fp;
                footprint( fp );
                for ( const auto& c : fp )
                    if ( m_Cells.insert( Pack( c ) ).second )
                    {
                        m_Region.push_back( c );
                        changed = true;
                    }
            }
            if ( !lmb )
                m_Dragging = false;
        }

        // --- Keys: E/Q extrude the last region out/in; Ctrl+E/Q grow/shrink the grid step. ---
        if ( interact )
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
            else if ( ::ImGui::IsKeyPressed( ImGuiKey_E, false ) && !m_Region.empty() ) // extrude out
            {
                std::vector<glm::ivec3> next;
                for ( const auto& c : m_Region )
                {
                    const glm::ivec3 nc = c + m_RegionNormal;
                    m_Cells.insert( Pack( nc ) );
                    next.push_back( nc );
                }
                m_Region = next;
                changed  = true;
            }
            else if ( ::ImGui::IsKeyPressed( ImGuiKey_Q, false ) && !m_Region.empty() ) // extrude in (carve)
            {
                for ( const auto& c : m_Region )
                    m_Cells.erase( Pack( c ) );
                std::vector<glm::ivec3> prev;
                for ( const auto& c : m_Region )
                    prev.push_back( c - m_RegionNormal );
                m_Region = prev;
                changed  = true;
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
        m_Dragging = false;
    }
} // namespace Desert::Editor::Tools
