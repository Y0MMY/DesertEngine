#pragma once

#include <Editor/Core/Selection/ModelingState.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/ShapeGenerators.hpp>

#include <Common/Core/Math/Ray.hpp>
#include <Common/Core/UUID.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cfloat>

namespace Desert::Editor::Tools
{
    // UE5 Modeling-mode "Create" palette: Box / Sphere / Cylinder / Cone / Stairs.
    //
    // The flow mirrors UE's: picking a shape starts a LIVE preview that follows the cursor along the
    // ground plane, LMB drops it where you clicked (it stops following), the panel's parameters keep
    // editing it in place, and Accept keeps it while Cancel deletes it. Nothing is committed until you
    // say so — the preview is an ordinary scene entity with a runtime mesh, exactly like the CubeGrid
    // blockout, so it lights and shades the way the final object will.
    //
    // Header-only on purpose: the geometry lives in Engine/Geometry/ShapeGenerators (unit-tested there),
    // and what remains here is placement + the entity's lifetime.
    class CreateShapeTool
    {
    public:
        void Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray, bool interactive )
        {
            auto& ms = Core::ModelingState::Get();

            if ( ms.ActiveTool != Core::ModelingState::Tool::CreateShape )
            {
                // Leaving the tool with an uncommitted preview discards it: a half-placed shape must not
                // survive as a mystery object in the scene.
                if ( m_Entity != Common::UUID::Null() )
                    Cancel( scene );
                return;
            }

            if ( ms.ReqCancel )
            {
                ms.ReqCancel = false;
                Cancel( scene );
                return;
            }

            // Follow the cursor until it is dropped. The ground plane is Y = 0 — the same plane the
            // CubeGrid starts on, so shapes and blockouts land on one floor.
            if ( interactive && !m_Placed && std::abs( ray.Direction.y ) > 1e-4f )
            {
                const float t = -ray.Origin.y / ray.Direction.y;
                if ( t > 0.0f )
                {
                    const glm::vec3 hit = ray.Origin + ray.Direction * t;
                    m_Position          = { hit.x, 0.0f, hit.z };
                    if ( ms.SnapToGrid )
                    {
                        const float g = std::max( ms.CellSize, 1.0f );
                        m_Position.x  = std::round( m_Position.x / g ) * g;
                        m_Position.z  = std::round( m_Position.z / g ) * g;
                    }
                }
            }

            // Rebuild only when something actually changed — this runs every frame.
            const ShapeKey key = KeyOf( ms );
            if ( m_Entity == Common::UUID::Null() || key != m_Key || m_Position != m_BuiltPosition )
                Rebuild( scene, ms, key );

            if ( interactive && !m_Placed && ::ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
                m_Placed = true; // dropped here; the parameters keep editing it in place

            if ( ms.ReqAccept )
            {
                ms.ReqAccept = false;
                Accept( scene, ms );
            }
        }

        // True while a preview exists (the viewport shows its Accept / Cancel bar for it).
        [[nodiscard]] bool HasPreview() const
        {
            return m_Entity != Common::UUID::Null();
        }

    private:
        // Everything that changes the geometry. Compared as a whole so one rebuild covers any edit.
        struct ShapeKey
        {
            int        Kind = 0;
            glm::vec3  Size{ 0.0f };
            glm::ivec3 Subdiv{ 1 };
            int        Segments = 0;
            int        Steps    = 0;

            bool operator==( const ShapeKey& o ) const
            {
                return Kind == o.Kind && Size == o.Size && Subdiv == o.Subdiv && Segments == o.Segments &&
                       Steps == o.Steps;
            }
            bool operator!=( const ShapeKey& o ) const
            {
                return !( *this == o );
            }
        };

        static ShapeKey KeyOf( const Core::ModelingState& ms )
        {
            ShapeKey k;
            k.Kind     = static_cast<int>( ms.Shape );
            k.Size     = ms.ShapeSize;
            k.Subdiv   = ms.ShapeSubdivisions;
            k.Segments = ms.ShapeSegments;
            k.Steps    = ms.ShapeSteps;
            return k;
        }

        static Geometry::ShapeMesh Generate( const Core::ModelingState& ms )
        {
            using S = Core::ModelingState::ShapeKind;
            switch ( ms.Shape )
            {
                case S::Sphere:
                    return Geometry::MakeSphere( ms.ShapeSize.x, ms.ShapeSegments,
                                                 std::max( ms.ShapeSegments / 2, 2 ) );
                case S::Cylinder:
                    return Geometry::MakeCylinder( ms.ShapeSize.x, ms.ShapeSize.y, ms.ShapeSegments );
                case S::Cone:
                    return Geometry::MakeCone( ms.ShapeSize.x, ms.ShapeSize.y, ms.ShapeSegments );
                case S::Stairs:
                    return Geometry::MakeStairs( ms.ShapeSize.x, ms.ShapeSize.z, ms.ShapeSize.y, ms.ShapeSteps );
                case S::Box:
                default:
                    return Geometry::MakeBox( ms.ShapeSize, ms.ShapeSubdivisions );
            }
        }

        static const char* NameOf( Core::ModelingState::ShapeKind kind )
        {
            using S = Core::ModelingState::ShapeKind;
            switch ( kind )
            {
                case S::Sphere:
                    return "Sphere";
                case S::Cylinder:
                    return "Cylinder";
                case S::Cone:
                    return "Cone";
                case S::Stairs:
                    return "Stairs";
                case S::Box:
                default:
                    return "Box";
            }
        }

        void Rebuild( ::Desert::Core::Scene& scene, const Core::ModelingState& ms, const ShapeKey& key )
        {
            const Geometry::ShapeMesh mesh = Generate( ms );
            if ( mesh.Vertices.empty() )
                return;

            if ( m_Entity == Common::UUID::Null() )
            {
                auto& e = scene.CreateNewEntity( NameOf( ms.Shape ) );
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

            const Common::Math::AABB bounds = mesh.Bounds();
            std::vector<Submesh> subs = { { NameOf( ms.Shape ), 0, static_cast<uint32_t>( mesh.Vertices.size() ),
                                            0, static_cast<uint32_t>( mesh.Indices.size() ) * 3, glm::mat4( 1.0f ),
                                            bounds } };

            smc.RuntimeMesh = std::make_shared<DynamicMesh>( mesh.Vertices, mesh.Indices, subs );
            smc.RuntimeMesh->Invalidate();

            entity.GetComponent<ECS::TransformComponent>().Translation = m_Position;

            m_Key           = key;
            m_BuiltPosition = m_Position;
        }

        void Accept( ::Desert::Core::Scene& scene, const Core::ModelingState& ms )
        {
            if ( m_Entity == Common::UUID::Null() )
                return;

            // Same deal as the CubeGrid bake: a shape you can walk into, with the honest caveat that this
            // is the bounding BOX (the physics layer has no triangle-mesh shape yet).
            if ( ms.GenerateCollision )
            {
                if ( auto ref = scene.FindEntityByID( m_Entity ) )
                {
                    ECS::Entity e = ref->get();
                    if ( e.HasComponent<ECS::StaticMeshComponent>() )
                    {
                        const auto& smc = e.GetComponent<ECS::StaticMeshComponent>();
                        if ( smc.RuntimeMesh && !smc.RuntimeMesh->GetSubmeshes().empty() )
                        {
                            const auto& box      = smc.RuntimeMesh->GetSubmeshes().front().BoundingBox;
                            auto&       col      = e.HasComponent<ECS::ColliderComponent>()
                                                        ? e.GetComponent<ECS::ColliderComponent>()
                                                        : e.AddComponent<ECS::ColliderComponent>();
                            col.Data.Shape       = Physics::ShapeType::Box;
                            col.Data.HalfExtents = glm::max( ( box.Max - box.Min ) * 0.5f, glm::vec3( 1.0f ) );
                            col.Data.Radius =
                                 glm::max( col.Data.HalfExtents.x,
                                           glm::max( col.Data.HalfExtents.y, col.Data.HalfExtents.z ) );

                            auto& rb     = e.HasComponent<ECS::RigidBodyComponent>()
                                                ? e.GetComponent<ECS::RigidBodyComponent>()
                                                : e.AddComponent<ECS::RigidBodyComponent>();
                            rb.Data.Type = Physics::BodyType::Static;
                        }
                    }
                }
            }

            Core::SelectionManager::SetSelected( m_Entity );

            // Hand the tool a clean slate: the next shape starts following the cursor again, exactly like
            // UE's "create another one" flow, and the parameters stay where you set them.
            m_Entity = Common::UUID::Null();
            m_Placed = false;
            m_Key    = {};
        }

        void Cancel( ::Desert::Core::Scene& scene )
        {
            if ( m_Entity != Common::UUID::Null() )
                if ( auto ref = scene.FindEntityByID( m_Entity ) )
                    scene.DestroyEntity( ref->get() );
            m_Entity = Common::UUID::Null();
            m_Placed = false;
            m_Key    = {};
        }

        Common::UUID m_Entity = Common::UUID::Null();
        ShapeKey     m_Key;
        glm::vec3    m_Position{ 0.0f };
        glm::vec3    m_BuiltPosition{ FLT_MAX };
        bool         m_Placed = false; // stopped following the cursor
    };
} // namespace Desert::Editor::Tools
