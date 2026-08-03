#include "CubeGridTool.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>

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
        const float cs = m_CellSize;

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
        const bool interact   = interactive && !::ImGui::IsAnyItemActive();
        bool       changed    = false;

        // Highlight the cell under the cursor (green = paint, red = erase).
        if ( removeMode && m_HasRemove )
            drawBox( m_Remove, m_Remove, IM_COL32( 240, 80, 60, 255 ) );
        else if ( m_HasAdd )
            drawBox( m_Add, m_Add, IM_COL32( 90, 220, 120, 255 ) );

        // --- Continuous cell painting: hold LMB and move to add every cell the cursor passes over (a paint
        //     stroke); Shift+LMB or RMB erases. Each stroke's painted cells become the region for E/Q. ---
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
                if ( m_Cells.insert( Pack( m_Add ) ).second ) // paint this cell (skip if already filled)
                {
                    m_Region.push_back( m_Add );
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
                m_CellSize = std::min( m_CellSize * 2.0f, 100000.0f );
                changed    = true;
            }
            else if ( ctrl && ::ImGui::IsKeyPressed( ImGuiKey_Q, false ) )
            {
                m_CellSize = std::max( m_CellSize * 0.5f, 1.0f );
                changed    = true;
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

        // --- Tool panel (Accept / Cancel / Clear + stats + hints). ---
        ::ImGui::SetNextWindowPos( ImVec2( viewportPos.x + 12.0f, viewportPos.y + 46.0f ), ImGuiCond_Always );
        ::ImGui::SetNextWindowBgAlpha( 0.88f );
        if ( ::ImGui::Begin( "CubeGrid##modeling", nullptr,
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoNav ) )
        {
            ::ImGui::TextUnformatted( "CubeGrid Blockout" );
            ::ImGui::Separator();
            ::ImGui::Text( "Grid step: %.0f", m_CellSize );
            ::ImGui::SameLine();
            if ( ::ImGui::SmallButton( "-##g" ) )
            {
                m_CellSize = std::max( m_CellSize * 0.5f, 1.0f );
                changed    = true;
            }
            ::ImGui::SameLine();
            if ( ::ImGui::SmallButton( "+##g" ) )
            {
                m_CellSize = std::min( m_CellSize * 2.0f, 100000.0f );
                changed    = true;
            }
            ::ImGui::Text( "Cubes: %d", static_cast<int>( m_Cells.size() ) );
            ::ImGui::Separator();
            if ( ::ImGui::Button( "Accept" ) && !m_Cells.empty() )
            {
                Core::SelectionManager::SetSelected( m_Entity );
                m_Entity = Common::UUID::Null(); // keep the mesh; next edits start a fresh blockout
                m_Cells.clear();
                m_Region.clear();
            }
            ::ImGui::SameLine();
            if ( ::ImGui::Button( "Cancel" ) )
                Cancel( scene );
            ::ImGui::SameLine();
            if ( ::ImGui::Button( "Clear" ) )
            {
                m_Cells.clear();
                m_Region.clear();
                changed = true;
            }
            ::ImGui::Separator();
            ::ImGui::TextDisabled( "LMB drag: paint cells  -  Shift/RMB: erase" );
            ::ImGui::TextDisabled( "E / Q: extrude stroke out / in" );
            ::ImGui::TextDisabled( "Ctrl+E / Ctrl+Q: grid step" );
        }
        ::ImGui::End();

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
