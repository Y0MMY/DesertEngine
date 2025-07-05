#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Assets/AssetManager.hpp>

namespace Desert::ECS
{
    class SkyboxRenderSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, const Common::Timestep& ts ) override
        {
            const auto& renderer        = m_Renderer.lock();
            const auto& resourceManager = m_ResourceManager.lock();
            if ( renderer && resourceManager )
            {
                const auto& skyboxes = registry.view<ECS::SkyboxComponent>();
                for ( const auto skyboxEntity : skyboxes )
                {
                    const auto& skybox      = registry.get<ECS::SkyboxComponent>( skyboxEntity );
                    auto        skyboxAsset = resourceManager->GetSkyboxCache().Get( skybox.SkyboxHandle );

                    if ( !skyboxAsset )
                        return;
                    renderer->SetEnvironment( skyboxAsset );
                    break;
                }
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