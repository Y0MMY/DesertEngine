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
        // Pack/unpack a 2D ground cell (ix, iz) into a 42-bit key (21 bits/axis, centred so negatives fit).
        constexpr int OFF = 1 << 20;
        uint64_t      Pack( int ix, int iz )
        {
            return ( static_cast<uint64_t>( ix + OFF ) & 0x1FFFFF ) |
                   ( ( static_cast<uint64_t>( iz + OFF ) & 0x1FFFFF ) << 21 );
        }
        glm::ivec2 Unpack( uint64_t k )
        {
            return { static_cast<int>( k & 0x1FFFFF ) - OFF, static_cast<int>( ( k >> 21 ) & 0x1FFFFF ) - OFF };
        }

        // The 6 faces of a unit cube (position in [-0.5,0.5] + the engine's cube normals/tangents/UVs), each as
        // 4 CCW corners — copied from PrimitiveMeshFactory::CreateCube so winding/normals are known-good.
        // Corners map onto an arbitrary box AABB (P+0.5 -> [0,1], * boxSize + boxMin) so a cell can be any tall.
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
        // Per-face ground-neighbour offset (ix,iz); a face is an internal WALL when that neighbour is filled.
        // Top/Bottom (indices 2,3) have no ground neighbour -> always a surface.
        const glm::ivec2 kSideNeighbor[6] = { { 0, 1 }, { 0, -1 }, { 0, 0 }, { 0, 0 }, { -1, 0 }, { 1, 0 } };
        const bool       kIsSide[6]       = { true, true, false, false, true, true };

        constexpr float kMinHeight = 0.02f; // smallest extrude (world units)
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
        const float          H          = std::max( ms.Height, kMinHeight );
        m_CellSize                      = cs;
        if ( ms.ReqClear )
        {
            ms.ReqClear = false;
            m_Cells.clear();
            m_HasLastPaint = false;
            RegenMesh( scene );
        }

        // --- Targeting: the ground cell (y=0) under the cursor. ---
        m_HasCell = false;
        if ( std::abs( ray.Direction.y ) > 1e-5f )
        {
            const float t = -ray.Origin.y / ray.Direction.y;
            if ( t > 0.0f )
            {
                const glm::vec3 p = ray.Origin + ray.Direction * t;
                m_Cell            = { static_cast<int>( std::floor( p.x / cs ) ),
                                      static_cast<int>( std::floor( p.z / cs ) ) };
                m_HasCell         = true;
            }
        }

        ImDrawList* dl = ::ImGui::GetWindowDrawList();
        auto        occupied = [&]( int ix, int iz ) { return m_Cells.count( Pack( ix, iz ) ) > 0; };

        // Draw a painted cell as a SOLID slab (0..H) — internal walls (shared with a filled neighbour) and
        // back faces are culled, so the painted footprint reads as one clean solid volume in the viewport.
        auto drawCellSolid = [&]( int ix, int iz, ImU32 fill, ImU32 outline )
        {
            const glm::vec3 mn( ix * cs, 0.0f, iz * cs );
            const glm::vec3 size( cs, H, cs );
            const glm::vec3 center = mn + size * 0.5f;
            for ( int f = 0; f < 6; ++f )
            {
                if ( kIsSide[f] && occupied( ix + kSideNeighbor[f].x, iz + kSideNeighbor[f].y ) )
                    continue; // internal wall
                const glm::vec3 fn = kFace[f][0].N;
                const glm::vec3 fc = center + 0.5f * size * fn;
                if ( glm::dot( fn, ray.Origin - fc ) <= 0.0f )
                    continue; // back face
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

        // Existing cells: grey checkerboard solids.
        if ( toolActive )
            for ( uint64_t k : m_Cells )
            {
                const glm::ivec2 c = Unpack( k );
                drawCellSolid( c.x, c.y,
                               ( ( c.x + c.y ) & 1 ) ? IM_COL32( 120, 125, 135, 200 )
                                                     : IM_COL32( 150, 155, 165, 200 ),
                               IM_COL32( 90, 95, 105, 230 ) );
            }

        const int bw = std::max( 1, ms.BrushW );
        const int bd = std::max( 1, ms.BrushD );

        // Flat cell face on the ground plane (a square) for the hover brush footprint.
        auto drawCellFace = [&]( int ix, int iz, ImU32 fill, ImU32 outline )
        {
            glm::vec2 s[4];
            const int du[4] = { 0, 1, 1, 0 };
            const int dv[4] = { 0, 0, 1, 1 };
            for ( int k = 0; k < 4; ++k )
            {
                const glm::vec3 w( ( ix + du[k] ) * cs, 0.0f, ( iz + dv[k] ) * cs );
                if ( !WorldToScreen( w, viewProj, viewportPos, viewportSize, s[k] ) )
                    return;
            }
            dl->AddQuadFilled( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                               ImVec2( s[3].x, s[3].y ), fill );
            dl->AddQuad( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                         ImVec2( s[3].x, s[3].y ), outline, 1.5f );
        };

        // Working grid + brush footprint under the cursor.
        if ( toolActive && m_HasCell )
        {
            const int   G      = 8;
            const ImU32 gcol   = IM_COL32( 120, 145, 180, 90 );
            auto        gridPt = [&]( int iu, int iv )
            { return glm::vec3( ( m_Cell.x + iu ) * cs, 0.0f, ( m_Cell.y + iv ) * cs ); };
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

            const ImU32 ff = removeMode ? IM_COL32( 240, 80, 60, 70 ) : IM_COL32( 70, 220, 100, 70 );
            const ImU32 fo = removeMode ? IM_COL32( 240, 90, 70, 255 ) : IM_COL32( 90, 240, 120, 255 );
            for ( int i = 0; i < bw; ++i )
                for ( int j = 0; j < bd; ++j )
                    drawCellFace( m_Cell.x + i, m_Cell.y + j, ff, fo );
        }

        // --- Paint: hold LMB and sweep to fill the W×D footprint (build as much as you move); Shift+LMB / RMB
        //     erases. Cells between frames are interpolated so a fast drag leaves no gaps. ---
        auto stamp = [&]( glm::ivec2 c, bool erase )
        {
            for ( int i = 0; i < bw; ++i )
                for ( int j = 0; j < bd; ++j )
                {
                    const uint64_t key = Pack( c.x + i, c.y + j );
                    if ( erase )
                        changed |= m_Cells.erase( key ) > 0;
                    else
                        changed |= m_Cells.insert( key ).second;
                }
        };

        if ( interact )
        {
            const bool lmb = ::ImGui::IsMouseDown( ImGuiMouseButton_Left );
            const bool rmb = ::ImGui::IsMouseDown( ImGuiMouseButton_Right );
            if ( ::ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
            {
                m_Painting     = !removeMode;
                m_Erasing      = removeMode;
                m_HasLastPaint = false;
            }
            else if ( ::ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
            {
                m_Painting     = false;
                m_Erasing      = true;
                m_HasLastPaint = false;
            }
            const bool stroke = ( m_Painting && lmb ) || ( m_Erasing && ( lmb || rmb ) );
            if ( stroke && m_HasCell )
            {
                const bool erase = m_Erasing;
                if ( m_HasLastPaint && m_LastPaintCell != m_Cell ) // interpolate the swept line
                {
                    const glm::ivec2 a = m_LastPaintCell, b = m_Cell;
                    const int        steps = std::max( std::abs( b.x - a.x ), std::abs( b.y - a.y ) );
                    for ( int s = 1; s <= steps; ++s )
                    {
                        const float t = static_cast<float>( s ) / steps;
                        stamp( { static_cast<int>( std::lround( a.x + ( b.x - a.x ) * t ) ),
                                 static_cast<int>( std::lround( a.y + ( b.y - a.y ) * t ) ) },
                               erase );
                    }
                }
                else
                {
                    stamp( m_Cell, erase );
                }
                m_LastPaintCell = m_Cell;
                m_HasLastPaint  = true;
            }
            if ( !lmb && !rmb )
            {
                m_Painting     = false;
                m_Erasing      = false;
                m_HasLastPaint = false;
            }
        }

        // --- Keys: E / Q (or Up / Down) raise / lower the Height; Ctrl+E / Ctrl+Q grow / shrink grid step. ---
        if ( interact )
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

        // A Height edit (panel field or keys) re-extrudes the whole slab.
        if ( std::abs( H - m_LastHeight ) > 1e-5f && !m_Cells.empty() )
            changed = true;

        ms.Cubes = static_cast<int>( m_Cells.size() );

        // --- Viewport bottom bar: tool name + Accept / Cancel (shown while a blockout is in progress). ---
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
                    m_HasLastPaint = false;
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
        const float          H  = std::max( ms.Height, kMinHeight );
        m_LastHeight            = H;

        if ( m_Cells.empty() )
        {
            Cancel( scene ); // no cells left -> remove the entity
            return;
        }

        auto occupied = [&]( int ix, int iz ) { return m_Cells.count( Pack( ix, iz ) ) > 0; };

        std::vector<Vertex> verts;
        std::vector<Index>  inds;
        glm::vec3           mn( FLT_MAX ), mx( -FLT_MAX );
        for ( uint64_t key : m_Cells )
        {
            const glm::ivec2 c = Unpack( key );
            const glm::vec3  bmin( c.x * m_CellSize, 0.0f, c.y * m_CellSize );
            const glm::vec3  bsize( m_CellSize, H, m_CellSize );
            for ( int f = 0; f < 6; ++f )
            {
                if ( kIsSide[f] && occupied( c.x + kSideNeighbor[f].x, c.y + kSideNeighbor[f].y ) )
                    continue; // internal wall — cull
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
            if ( auto ref = scene.FindEntityByID( m_Entity ) )
                scene.DestroyEntity( ref->get() );
        m_Entity = Common::UUID::Null();
        m_Cells.clear();
        m_HasLastPaint = false;
        m_Painting     = false;
        m_Erasing      = false;
    }
} // namespace Desert::Editor::Tools
