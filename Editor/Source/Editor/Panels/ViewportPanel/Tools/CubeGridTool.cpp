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
        // Pack/unpack a 3D grid cell into a 63-bit key (21 bits/axis, centred so negatives fit).
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
        // Outward neighbour offset per face — a face is internal (culled) when that neighbour is filled.
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
        Core::ModelingState& ms         = Core::ModelingState::Get();
        const float          cs         = ms.CellSize;
        const bool           toolActive = ms.ActiveTool == Core::ModelingState::Tool::CubeGrid;
        const int            layers     = std::max( 1, static_cast<int>( std::lround( ms.Height ) ) );
        m_CellSize                      = cs;
        if ( ms.ReqClear )
        {
            ms.ReqClear = false;
            m_Cells.clear();
            m_HasLastPaint = false;
            RegenMesh( scene );
        }

        auto occupied = [&]( const glm::ivec3& c ) { return m_Cells.count( Pack( c ) ) > 0; };

        // --- Targeting: nearest occupied cube face under the cursor, else the ground plane (y=0). ---
        m_HasTarget = m_HasErase = false;
        float      bestT         = FLT_MAX;
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
            const glm::vec3 d  = p - ( glm::vec3( hitCell ) + 0.5f ) * cs;
            const glm::vec3 ad = glm::abs( d );
            glm::ivec3      n{ 0 };
            if ( ad.x >= ad.y && ad.x >= ad.z )
                n.x = d.x > 0 ? 1 : -1;
            else if ( ad.y >= ad.z )
                n.y = d.y > 0 ? 1 : -1;
            else
                n.z = d.z > 0 ? 1 : -1;
            m_TargetCell   = hitCell + n;
            m_TargetNormal = n;
            m_EraseCell    = hitCell;
            m_HasTarget = m_HasErase = true;
        }
        else if ( std::abs( ray.Direction.y ) > 1e-5f )
        {
            const float t = -ray.Origin.y / ray.Direction.y;
            if ( t > 0.0f )
            {
                const glm::vec3 p = ray.Origin + ray.Direction * t;
                m_TargetCell      = { static_cast<int>( std::floor( p.x / cs ) ), 0,
                                      static_cast<int>( std::floor( p.z / cs ) ) };
                m_TargetNormal    = { 0, 1, 0 };
                m_HasTarget       = true;
            }
        }

        // --- Overlay draw list + helpers. ---
        ImDrawList* dl = ::ImGui::GetWindowDrawList();

        // Draw a cell as SOLID shaded faces (only outward, camera-facing, non-internal faces).
        auto drawCellSolid = [&]( const glm::ivec3& c, ImU32 fill, ImU32 outline )
        {
            const glm::vec3 mn = glm::vec3( c ) * cs;
            for ( int f = 0; f < 6; ++f )
            {
                if ( occupied( c + kNeighbor[f] ) )
                    continue; // internal face
                const glm::vec3 fn = kFace[f][0].N;
                const glm::vec3 fc = mn + 0.5f * cs + 0.5f * cs * fn;
                if ( glm::dot( fn, ray.Origin - fc ) <= 0.0f )
                    continue; // back face
                glm::vec2 s[4];
                bool      ok = true;
                for ( int k = 0; k < 4; ++k )
                    if ( !WorldToScreen( mn + ( kFace[f][k].P + 0.5f ) * cs, viewProj, viewportPos, viewportSize,
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

        // Flat cell face on a plane (na = normal axis, planeW = world coord) — the hover brush footprint.
        auto drawPlaneCell = [&]( int uc, int vc, int na, float planeW, ImU32 fill, ImU32 outline )
        {
            const int ua = ( na + 1 ) % 3;
            const int va = ( na + 2 ) % 3;
            glm::vec2 s[4];
            const int du[4] = { 0, 1, 1, 0 };
            const int dv[4] = { 0, 0, 1, 1 };
            for ( int k = 0; k < 4; ++k )
            {
                glm::vec3 w( 0.0f );
                w[na] = planeW;
                w[ua] = static_cast<float>( uc + du[k] ) * cs;
                w[va] = static_cast<float>( vc + dv[k] ) * cs;
                if ( !WorldToScreen( w, viewProj, viewportPos, viewportSize, s[k] ) )
                    return;
            }
            dl->AddQuadFilled( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                               ImVec2( s[3].x, s[3].y ), fill );
            dl->AddQuad( ImVec2( s[0].x, s[0].y ), ImVec2( s[1].x, s[1].y ), ImVec2( s[2].x, s[2].y ),
                         ImVec2( s[3].x, s[3].y ), outline, 1.5f );
        };

        const bool removeMode = ::ImGui::GetIO().KeyShift;
        const bool interact   = interactive && toolActive && !::ImGui::IsAnyItemActive();
        bool       changed    = false;

        // Existing cells: grey checkerboard solids (the erase target is tinted red).
        if ( toolActive )
            for ( uint64_t k : m_Cells )
            {
                const glm::ivec3 c    = Unpack( k );
                const bool       eras = removeMode && m_HasErase && m_EraseCell == c;
                const ImU32      fill = eras                          ? IM_COL32( 200, 90, 80, 205 )
                                        : ( ( c.x + c.y + c.z ) & 1 ) ? IM_COL32( 120, 125, 135, 205 )
                                                                      : IM_COL32( 150, 155, 165, 205 );
                drawCellSolid( c, fill, IM_COL32( 90, 95, 105, 230 ) );
            }

        const int bw = std::max( 1, ms.BrushW );
        const int bd = std::max( 1, ms.BrushD );

        // World coord of the plane that fills `cell` along axis `na` with outward normal sign `sgn`.
        auto planeWorld = [cs]( const glm::ivec3& cell, int na, int sgn )
        { return ( sgn > 0 ? static_cast<float>( cell[na] ) : static_cast<float>( cell[na] + 1 ) ) * cs; };

        // Hover footprint on the target surface (green = add, red = erase).
        if ( toolActive && !m_Painting && !m_Erasing && m_HasTarget )
        {
            const int   na  = m_TargetNormal.x ? 0 : m_TargetNormal.y ? 1 : 2;
            const int   sgn = m_TargetNormal[na];
            const int   ua  = ( na + 1 ) % 3;
            const int   va  = ( na + 2 ) % 3;
            const float pw  = planeWorld( m_TargetCell, na, sgn );
            const ImU32 ff  = removeMode ? IM_COL32( 240, 80, 60, 70 ) : IM_COL32( 70, 220, 100, 70 );
            const ImU32 fo  = removeMode ? IM_COL32( 240, 90, 70, 255 ) : IM_COL32( 90, 240, 120, 255 );
            for ( int i = 0; i < bw; ++i )
                for ( int j = 0; j < bd; ++j )
                    drawPlaneCell( m_TargetCell[ua] + i, m_TargetCell[va] + j, na, pw, ff, fo );
        }

        // Stamp a W×D footprint (`layers` cells thick along the stroke normal) at plane cell (uc,vc).
        auto stampUV = [&]( int uc, int vc, bool erase )
        {
            const int na = m_StrokeNa;
            const int ua = ( na + 1 ) % 3;
            const int va = ( na + 2 ) % 3;
            for ( int i = 0; i < bw; ++i )
                for ( int j = 0; j < bd; ++j )
                    for ( int L = 0; L < layers; ++L )
                    {
                        glm::ivec3 c{ 0 };
                        c[na]              = m_StrokePlaneCell + L * m_StrokeSign;
                        c[ua]              = uc + i;
                        c[va]              = vc + j;
                        const uint64_t key = Pack( c );
                        if ( erase )
                            changed |= m_Cells.erase( key ) > 0;
                        else
                            changed |= m_Cells.insert( key ).second;
                    }
        };

        // --- Start a stroke only when the viewport is genuinely hovered/active. ---
        if ( interact )
        {
            if ( ::ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !removeMode && m_HasTarget )
            {
                m_Painting        = true;
                m_Erasing         = false;
                m_StrokeNa        = m_TargetNormal.x ? 0 : m_TargetNormal.y ? 1 : 2;
                m_StrokeSign      = m_TargetNormal[m_StrokeNa];
                m_StrokePlaneCell = m_TargetCell[m_StrokeNa];
                m_StrokePlaneW    = planeWorld( m_TargetCell, m_StrokeNa, m_StrokeSign );
                m_HasLastPaint    = false;
            }
            else if ( ::ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && removeMode )
            {
                m_Erasing  = true;
                m_Painting = false;
            }
            else if ( ::ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
            {
                m_Erasing  = true;
                m_Painting = false;
            }
        }

        // --- Paint: keep filling while LMB is PHYSICALLY held (independent of hover flicker). The cursor is
        //     projected onto the LOCKED work-plane, so the whole sweep paints one surface; cells between
        //     frames are interpolated to avoid gaps. ---
        if ( toolActive && m_Painting )
        {
            const bool lmb = ::ImGui::IsMouseDown( ImGuiMouseButton_Left );
            if ( lmb && std::abs( ray.Direction[m_StrokeNa] ) > 1e-6f )
            {
                const float t = ( m_StrokePlaneW - ray.Origin[m_StrokeNa] ) / ray.Direction[m_StrokeNa];
                if ( t > 0.0f )
                {
                    const int        ua = ( m_StrokeNa + 1 ) % 3;
                    const int        va = ( m_StrokeNa + 2 ) % 3;
                    const glm::vec3  p  = ray.Origin + ray.Direction * t;
                    const glm::ivec2 uv{ static_cast<int>( std::floor( p[ua] / cs ) ),
                                         static_cast<int>( std::floor( p[va] / cs ) ) };
                    if ( m_HasLastPaint && m_LastPaintUV != uv ) // interpolate the swept line
                    {
                        const glm::ivec2 a = m_LastPaintUV, b = uv;
                        const int        steps = std::max( std::abs( b.x - a.x ), std::abs( b.y - a.y ) );
                        for ( int s = 1; s <= steps; ++s )
                        {
                            const float f = static_cast<float>( s ) / steps;
                            stampUV( static_cast<int>( std::lround( a.x + ( b.x - a.x ) * f ) ),
                                     static_cast<int>( std::lround( a.y + ( b.y - a.y ) * f ) ), false );
                        }
                    }
                    else
                    {
                        stampUV( uv.x, uv.y, false );
                    }
                    m_LastPaintUV  = uv;
                    m_HasLastPaint = true;
                }
            }
            if ( !lmb )
            {
                m_Painting     = false;
                m_HasLastPaint = false;
            }
        }

        // --- Erase: remove whatever cell is under the cursor while a button is held (sweeps a trail). ---
        if ( toolActive && m_Erasing )
        {
            const bool held =
                 ::ImGui::IsMouseDown( ImGuiMouseButton_Left ) || ::ImGui::IsMouseDown( ImGuiMouseButton_Right );
            if ( held && m_HasErase )
                changed |= m_Cells.erase( Pack( m_EraseCell ) ) > 0;
            if ( !held )
                m_Erasing = false;
        }

        // --- Keys: E / Q (or Up / Down) raise / lower Brush Height; Ctrl+E / Ctrl+Q grow / shrink grid step. ---
        if ( interact )
        {
            const bool ctrl = ::ImGui::GetIO().KeyCtrl;
            if ( ctrl && ::ImGui::IsKeyPressed( ImGuiKey_E, false ) )
                ms.CellSize = std::min( ms.CellSize * 2.0f, 100000.0f );
            else if ( ctrl && ::ImGui::IsKeyPressed( ImGuiKey_Q, false ) )
                ms.CellSize = std::max( ms.CellSize * 0.5f, 0.01f );
            else if ( ::ImGui::IsKeyPressed( ImGuiKey_E, false ) ||
                      ::ImGui::IsKeyPressed( ImGuiKey_UpArrow, false ) )
                ms.Height = std::max( 1.0f, std::round( ms.Height ) + 1.0f );
            else if ( ::ImGui::IsKeyPressed( ImGuiKey_Q, false ) ||
                      ::ImGui::IsKeyPressed( ImGuiKey_DownArrow, false ) )
                ms.Height = std::max( 1.0f, std::round( ms.Height ) - 1.0f );
        }

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
        if ( m_Cells.empty() )
        {
            Cancel( scene ); // no cells left -> remove the entity
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
                if ( occupied( c + kNeighbor[f] ) ) // neighbour filled -> internal face, cull
                    continue;
                const uint32_t base = static_cast<uint32_t>( verts.size() );
                for ( int k = 0; k < 4; ++k )
                {
                    const FaceVert& fv = kFace[f][k];
                    Vertex          v;
                    v.Position  = ( glm::vec3( c ) + fv.P + 0.5f ) * m_CellSize;
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
