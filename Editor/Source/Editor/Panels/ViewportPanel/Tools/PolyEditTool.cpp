#include "PolyEditTool.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/ModelingState.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/MeshTypes.hpp>

#include <ImGui/imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace Desert::Editor::Tools
{
    namespace
    {
        // Möller–Trumbore ray/triangle (front+back). Returns the ray parameter t (>0) on hit.
        bool RayTri( const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b,
                     const glm::vec3& c, float& t )
        {
            const glm::vec3 e1 = b - a, e2 = c - a, p = glm::cross( d, e2 );
            const float     det = glm::dot( e1, p );
            if ( std::abs( det ) < 1e-9f )
                return false;
            const float     inv = 1.0f / det;
            const glm::vec3 tv  = o - a;
            const float     u   = glm::dot( tv, p ) * inv;
            if ( u < 0.0f || u > 1.0f )
                return false;
            const glm::vec3 q = glm::cross( tv, e1 );
            const float     v = glm::dot( d, q ) * inv;
            if ( v < 0.0f || u + v > 1.0f )
                return false;
            t = glm::dot( e2, q ) * inv;
            return t > 1e-4f;
        }

        // Weld a local position into an integer key (0.01-unit tolerance) so coincident verts share an index.
        uint64_t WeldKey( const glm::vec3& p )
        {
            auto q = []( float v )
            { return static_cast<uint64_t>( std::llround( v * 100.0 ) + ( 1 << 20 ) ) & 0x1FFFFF; };
            return q( p.x ) | ( q( p.y ) << 21 ) | ( q( p.z ) << 42 );
        }
        uint64_t EdgeKey( int a, int b )
        {
            if ( a > b )
                std::swap( a, b );
            return ( static_cast<uint64_t>( static_cast<uint32_t>( a ) ) << 32 ) | static_cast<uint32_t>( b );
        }

        // Resolve the selected entity's editable mesh (RuntimeMesh) + its world transform. null on miss.
        DynamicMesh* GetMesh( ::Desert::Core::Scene& scene, const Common::UUID& id, glm::mat4& world )
        {
            if ( static_cast<uint64_t>( id ) == 0 )
                return nullptr;
            auto ref = scene.FindEntityByID( id );
            if ( !ref )
                return nullptr;
            ECS::Entity e = ref->get();
            if ( !e.HasComponent<ECS::StaticMeshComponent>() )
                return nullptr;
            auto& smc = e.GetComponent<ECS::StaticMeshComponent>();
            if ( !smc.RuntimeMesh )
                return nullptr;
            world = e.HasComponent<ECS::TransformComponent>()
                         ? e.GetComponent<ECS::TransformComponent>().GetTransform()
                         : glm::mat4( 1.0f );
            return smc.RuntimeMesh.get();
        }
    } // namespace

    bool PolyEditTool::WorldToScreen( const glm::vec3& world, const glm::mat4& vp, const glm::vec2& pos,
                                      const glm::vec2& size, glm::vec2& out )
    {
        const glm::vec4 clip = vp * glm::vec4( world, 1.0f );
        if ( clip.w <= 0.0001f )
            return false;
        const glm::vec3 ndc = glm::vec3( clip ) / clip.w;
        out.x               = pos.x + ( ndc.x * 0.5f + 0.5f ) * size.x;
        out.y               = pos.y + ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * size.y;
        return true;
    }

    void PolyEditTool::ClearSelection()
    {
        m_SelVerts.clear();
        m_SelTris.clear();
        m_HasSel   = false;
        m_Dragging = false;
    }

    bool PolyEditTool::PickFace( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray )
    {
        glm::mat4    world;
        DynamicMesh* mesh = GetMesh( scene, m_Entity, world );
        if ( !mesh )
            return false;

        const auto& verts = mesh->GetVertices();
        const auto& inds  = mesh->GetIndices();
        const int   T     = static_cast<int>( inds.size() );
        if ( T == 0 )
            return false;

        // Nearest triangle under the ray (world space).
        int   hit   = -1;
        float bestT = FLT_MAX;
        for ( int i = 0; i < T; ++i )
        {
            const glm::vec3 a = glm::vec3( world * glm::vec4( verts[inds[i].V1].Position, 1.0f ) );
            const glm::vec3 b = glm::vec3( world * glm::vec4( verts[inds[i].V2].Position, 1.0f ) );
            const glm::vec3 c = glm::vec3( world * glm::vec4( verts[inds[i].V3].Position, 1.0f ) );
            float           t;
            if ( RayTri( ray.Origin, ray.Direction, a, b, c, t ) && t < bestT )
            {
                bestT = t;
                hit   = i;
            }
        }
        if ( hit < 0 )
        {
            ClearSelection();
            return false;
        }

        // Weld + per-triangle geometric (local) normal + edge -> triangles adjacency.
        std::vector<int> weldOf( verts.size() );
        {
            std::unordered_map<uint64_t, int> wm;
            for ( size_t i = 0; i < verts.size(); ++i )
            {
                const uint64_t k  = WeldKey( verts[i].Position );
                auto           it = wm.find( k );
                weldOf[i]         = it != wm.end() ? it->second : ( wm[k] = static_cast<int>( wm.size() ) );
            }
        }
        std::vector<std::array<int, 3>>                triW( T );
        std::vector<glm::vec3>                         triN( T );
        std::unordered_map<uint64_t, std::vector<int>> edgeTris;
        for ( int i = 0; i < T; ++i )
        {
            triW[i]           = { weldOf[inds[i].V1], weldOf[inds[i].V2], weldOf[inds[i].V3] };
            const glm::vec3 a = verts[inds[i].V1].Position, b = verts[inds[i].V2].Position,
                            c = verts[inds[i].V3].Position;
            triN[i]           = glm::normalize( glm::cross( b - a, c - a ) );
            edgeTris[EdgeKey( triW[i][0], triW[i][1] )].push_back( i );
            edgeTris[EdgeKey( triW[i][1], triW[i][2] )].push_back( i );
            edgeTris[EdgeKey( triW[i][2], triW[i][0] )].push_back( i );
        }

        // Flood-fill the coplanar polygroup from the hit triangle (shared edge + near-equal normal).
        const glm::vec3   hN = triN[hit];
        std::vector<int>  face{ hit };
        std::vector<char> seen( T, 0 );
        seen[hit] = 1;
        std::unordered_set<int> faceWeld;
        for ( size_t f = 0; f < face.size(); ++f )
        {
            const int i = face[f];
            faceWeld.insert( triW[i][0] );
            faceWeld.insert( triW[i][1] );
            faceWeld.insert( triW[i][2] );
            const uint64_t edges[3] = { EdgeKey( triW[i][0], triW[i][1] ), EdgeKey( triW[i][1], triW[i][2] ),
                                        EdgeKey( triW[i][2], triW[i][0] ) };
            for ( uint64_t ek : edges )
                for ( int nb : edgeTris[ek] )
                    if ( !seen[nb] && glm::dot( triN[nb], hN ) > 0.99f )
                    {
                        seen[nb] = 1;
                        face.push_back( nb );
                    }
        }

        // Selection = every ORIGINAL vertex whose welded index is in the face (so shared corners move too).
        m_SelTris = face;
        m_SelVerts.clear();
        glm::vec3 centroidLocal( 0.0f );
        for ( size_t i = 0; i < verts.size(); ++i )
            if ( faceWeld.count( weldOf[i] ) )
                m_SelVerts.push_back( static_cast<int>( i ) );
        // Centroid from unique weld positions (average the face triangles' vertices).
        {
            std::unordered_set<int> done;
            int                     n = 0;
            for ( int i : m_SelVerts )
                if ( done.insert( weldOf[i] ).second )
                {
                    centroidLocal += verts[i].Position;
                    ++n;
                }
            if ( n > 0 )
                centroidLocal /= static_cast<float>( n );
        }
        m_FaceNormalLocal = hN;
        m_CentroidWorld   = glm::vec3( world * glm::vec4( centroidLocal, 1.0f ) );
        m_HasSel          = !m_SelVerts.empty();
        return m_HasSel;
    }

    void PolyEditTool::Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray,
                               const glm::mat4& viewProj, const glm::vec2& viewportPos,
                               const glm::vec2& viewportSize, bool interactive )
    {
        if ( Core::ModelingState::Get().ActiveTool != Core::ModelingState::Tool::PolyEdit )
        {
            ClearSelection();
            return;
        }

        // The edited entity follows the current selection; a changed selection drops the face.
        const auto&        selOpt = Core::SelectionManager::GetSelected();
        const Common::UUID sel    = selOpt.has_value() ? *selOpt : Common::UUID::Null();
        if ( static_cast<uint64_t>( sel ) != static_cast<uint64_t>( m_Entity ) )
        {
            m_Entity = sel;
            ClearSelection();
        }

        glm::mat4    world;
        DynamicMesh* mesh = GetMesh( scene, m_Entity, world );

        ImDrawList* dl = ::ImGui::GetWindowDrawList();
        if ( !mesh )
        {
            ClearSelection();
            return;
        }

        const bool interact = interactive && !::ImGui::IsAnyItemActive();

        // Pick a face on click (when not mid-drag).
        if ( interact && !m_Dragging && ::ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
            PickFace( scene, ray );

        // Push/pull the selected face along its world normal while dragging.
        if ( m_HasSel )
        {
            const glm::vec3 wN = glm::normalize( glm::mat3( world ) * m_FaceNormalLocal );
            // Closest parameter s on the line (centroid + s*wN) to the cursor ray (Ericson's two-line form).
            const glm::vec3 u = wN, v = ray.Direction, w = m_CentroidWorld - ray.Origin;
            const float     b = glm::dot( u, v ), d = glm::dot( u, w ), e = glm::dot( v, w );
            const float     denom = 1.0f - b * b;
            const float     s     = std::abs( denom ) > 1e-5f ? ( b * e - d ) / denom : 0.0f;

            if ( interact && ::ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
            {
                m_Dragging = true;
                m_DragS    = s;
            }
            if ( m_Dragging && ::ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
            {
                const float ds = s - m_DragS;
                if ( std::abs( ds ) > 1e-4f )
                {
                    const glm::vec3 deltaLocal = glm::vec3( glm::inverse( glm::mat3( world ) ) * ( ds * wN ) );
                    auto&           verts      = mesh->GetVertices();
                    for ( int i : m_SelVerts )
                        verts[i].Position += deltaLocal;
                    m_CentroidWorld += ds * wN;
                    m_DragS = s;

                    // Recompute flat normals so lighting follows the deform, then re-upload.
                    auto& inds = mesh->GetIndices();
                    for ( const auto& tri : inds )
                    {
                        const glm::vec3 n =
                             glm::normalize( glm::cross( verts[tri.V2].Position - verts[tri.V1].Position,
                                                         verts[tri.V3].Position - verts[tri.V1].Position ) );
                        verts[tri.V1].Normal = verts[tri.V2].Normal = verts[tri.V3].Normal = n;
                    }
                    mesh->Update( verts, inds );
                }
            }
            if ( !::ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
                m_Dragging = false;

            // Highlight the selected face (translucent green + outline).
            const auto& verts = mesh->GetVertices();
            const auto& inds  = mesh->GetIndices();
            for ( int ti : m_SelTris )
            {
                if ( ti < 0 || ti >= static_cast<int>( inds.size() ) )
                    continue;
                glm::vec2       s0, s1, s2;
                const glm::vec3 a  = glm::vec3( world * glm::vec4( verts[inds[ti].V1].Position, 1.0f ) );
                const glm::vec3 bb = glm::vec3( world * glm::vec4( verts[inds[ti].V2].Position, 1.0f ) );
                const glm::vec3 c  = glm::vec3( world * glm::vec4( verts[inds[ti].V3].Position, 1.0f ) );
                if ( WorldToScreen( a, viewProj, viewportPos, viewportSize, s0 ) &&
                     WorldToScreen( bb, viewProj, viewportPos, viewportSize, s1 ) &&
                     WorldToScreen( c, viewProj, viewportPos, viewportSize, s2 ) )
                {
                    dl->AddTriangleFilled( ImVec2( s0.x, s0.y ), ImVec2( s1.x, s1.y ), ImVec2( s2.x, s2.y ),
                                           IM_COL32( 60, 220, 90, 90 ) );
                    dl->AddTriangle( ImVec2( s0.x, s0.y ), ImVec2( s1.x, s1.y ), ImVec2( s2.x, s2.y ),
                                     IM_COL32( 90, 240, 120, 255 ), 1.5f );
                }
            }
        }
    }
} // namespace Desert::Editor::Tools
