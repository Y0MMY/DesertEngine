#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Assets/AssetManager.hpp>

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
                     [&]( auto entity, const auto& mesh, const auto& transform )
                     {
                         auto resolvedMesh = resourceManager->GetMesh( mesh.MeshHandle );
                         if ( !resolvedMesh || !mesh.Material )
                         {
                             return;
                         }

                         // Add to render list
                         renderer->AddToRenderMeshList( resolvedMesh, mesh.Material, transform.GetTransform() );
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