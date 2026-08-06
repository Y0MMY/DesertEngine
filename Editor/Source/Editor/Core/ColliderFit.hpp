#pragma once

#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <glm/glm.hpp>

#include <limits>
#include <optional>

namespace Desert::Editor::Core
{
    // Fitting a collider to the mesh an entity actually draws. Shared because TWO places must agree on
    // it: the Collision menu in the toolbar and the Collider section in Details (whose "off the mesh
    // bounds by N%" warning is computed from the same measurement, so the warning and the button that
    // silences it can never disagree).

    // The mesh this entity really draws — the same precedence the renderer uses.
    inline const ::Desert::Mesh* ColliderSourceMesh( const ECS::Entity& entity )
    {
        if ( !entity.HasComponent<ECS::StaticMeshComponent>() )
            return nullptr;

        const auto& smc = entity.GetComponent<ECS::StaticMeshComponent>();
        if ( smc.MeshHandle )
            return Runtime::ResourceRegistry::GetMeshService()->Get( smc.MeshHandle );
        if ( smc.RuntimeMesh )
            return smc.RuntimeMesh.get();
        if ( smc.Primitive.has_value() )
            return Geometry::PrimitiveMeshFactory::GetShared( smc.Primitive.value() );
        return nullptr;
    }

    // World-space half-extents of that mesh (its local bounds times the entity's scale), or nothing when
    // there is no mesh to measure.
    inline std::optional<glm::vec3> MeshHalfExtents( const ECS::Entity& entity )
    {
        const ::Desert::Mesh* mesh = ColliderSourceMesh( entity );
        if ( !mesh )
            return std::nullopt;

        glm::vec3 mn( std::numeric_limits<float>::max() );
        glm::vec3 mx( std::numeric_limits<float>::lowest() );
        for ( const auto& sm : mesh->GetSubmeshes() )
        {
            mn = glm::min( mn, sm.BoundingBox.Min );
            mx = glm::max( mx, sm.BoundingBox.Max );
        }
        if ( mn.x > mx.x )
            return std::nullopt; // no submeshes / empty mesh

        const glm::vec3 scale = entity.GetComponent<ECS::TransformComponent>().Scale;
        return glm::abs( ( mx - mn ) * 0.5f * scale );
    }

    // Sizes @p col to wrap the entity's mesh. Leaves the shape alone — box, sphere and capsule are all
    // sized from the same half-extents, so switching shape afterwards keeps the fit.
    inline void FitColliderToMesh( const ECS::Entity& entity, ECS::ColliderData& col )
    {
        const auto half = MeshHalfExtents( entity );
        if ( !half )
            return;

        col.HalfExtents = *half;
        col.Radius      = glm::max( half->x, glm::max( half->y, half->z ) );
        // Capsule cylinder half-height = total half-height minus the two hemispherical caps (radius).
        col.HalfHeight = glm::max( 0.01f, half->y - col.Radius );
    }
} // namespace Desert::Editor::Core
