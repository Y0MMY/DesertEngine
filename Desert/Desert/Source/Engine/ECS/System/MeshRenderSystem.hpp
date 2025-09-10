#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Geometry/PrimitiveMeshFactory.hpp>

namespace Desert::ECS
{
    class MeshRenderSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, const Common::Timestep& ts ) override
        {
            // Process static meshes
            const auto& renderer        = m_Renderer.lock();
            const auto& resourceManager = m_ResourceRegistry.lock();
            if ( renderer && resourceManager )
            {
                auto meshView = registry.view<StaticMeshComponent, TransformComponent>();
                meshView.each(
                     [&]( auto entity, const StaticMeshComponent& mesh, const TransformComponent& transform )
                     {
                         std::shared_ptr<Desert::Mesh> targetMesh = nullptr;
                         const auto                    meshType   = mesh.GetMeshType();
                         if ( meshType != StaticMeshComponent::Type::None )
                         {
                             if ( meshType == StaticMeshComponent::Type::Primitive )
                             {
                                 targetMesh = PrimitiveMeshFactory::GetPrimitive( *mesh.PrimitiveShape );
                             }
                             else
                             {
                                 auto resolvedMesh = resourceManager->GetMesh( *mesh.MeshHandle );
                                 if ( !resolvedMesh || !mesh.Material )
                                 {
                                     return;
                                 }
                                 targetMesh = resolvedMesh;
                             }

                             // Add to render list
                             renderer->AddToRenderMeshList( targetMesh, mesh.Material, transform.GetTransform() );
                         }
                     } );
            }
        }

        void OnInit( entt::registry& registry ) override
        {
            // Any initialization logic if needed
        }

        void OnShutdown( entt::registry& registry ) override
        {
            // Any cleanup logic if needed
        }
    };
} // namespace Desert::ECS