#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <Engine/Graphic/Render/Commands/SkyboxCommand.hpp>

namespace Desert::ECS
{
    class SkyboxECSSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& ts ) override
        {

            const auto& skyboxes = registry.view<ECS::SkyboxComponent>();
            for ( const auto skyboxEntity : skyboxes )
            {
                const auto& skybox = registry.get<ECS::SkyboxComponent>( skyboxEntity );
                /*if ( skybox.SkyboxHandle == m_CurrentSkyboxHandle )
                    return;*/
                // m_CurrentSkyboxHandle = skybox.SkyboxHandle;
                auto skyboxAsset = Runtime::ResourceRegistry::GetSkyboxService()->Get( skybox.SkyboxHandle );

                if ( !skyboxAsset )
                {
                    return;
                }
              //  renderCommandBuffer.Emplace<Graphic::Render::SkyboxCommand>( skyboxAsset );
                break;
            }
        }
    };
} // namespace Desert::ECS