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
            auto meshView = registry.view<StaticMeshComponent, TransformComponent>();
            meshView.each(
                 [&]( auto entity, const auto& mesh, const auto& transform )
                 {
                     auto meshAsset = m_ResourceManager->GetMeshCache().Get( mesh.MeshHandle );
                     if ( !meshAsset )
                         return;

                     
                     // Add to render list
                    // m_Renderer->AddToRenderMeshList( meshAsset->GetMesh(), material, transform.GetTransform() );
                 } );
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