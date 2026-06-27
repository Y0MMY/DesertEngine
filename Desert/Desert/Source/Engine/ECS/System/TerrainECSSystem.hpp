#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Render/Commands/DrawTerrainCommand.hpp>

namespace Desert::ECS
{
    // Collects every TerrainComponent entity each frame and forwards its transform + params to the
    // GPU TerrainRenderer (via a DrawTerrainCommand). Mirrors MeshECSSystem / SkyboxECSSystem.
    class TerrainECSSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& ts ) override
        {
            auto view = registry.view<TerrainComponent, TransformComponent>();
            for ( const auto entity : view )
            {
                if ( registry.has<VisibilityComponent>( entity ) &&
                     !registry.get<VisibilityComponent>( entity ).Visible )
                    continue;

                auto&       terrainComp = view.get<TerrainComponent>( entity );
                const auto& terrain     = terrainComp.Data;
                const auto& transform   = view.get<TransformComponent>( entity );

                // Optional MaterialComponent on the same entity supplies generic param + texture overrides
                // (Tint, DetailTiling, u_GrassTex/u_RockTex/u_SnowTex splat layers, ...).
                std::vector<std::pair<std::string, glm::vec4>> overrides;
                std::vector<std::pair<std::string, uint64_t>>  textureOverrides;
                if ( registry.has<MaterialComponent>( entity ) )
                {
                    const auto& mat = registry.get<MaterialComponent>( entity );
                    overrides.reserve( mat.Params.size() );
                    for ( const auto& p : mat.Params )
                        overrides.emplace_back( p.Name, p.Value );

                    textureOverrides.reserve( mat.Textures.size() );
                    for ( const auto& t : mat.Textures )
                        textureOverrides.emplace_back( t.Name, t.TextureHandle );
                }

                const glm::vec3 layerModes( static_cast<float>( terrain.GrassMode ),
                                            static_cast<float>( terrain.RockMode ),
                                            static_cast<float>( terrain.SnowMode ) );

                const glm::vec4 grassParams( terrain.EnableGrass ? 1.0f : 0.0f,
                                             static_cast<float>( terrain.GrassDensity ), terrain.GrassHeight,
                                             terrain.GrassWidth );

                renderCommandBuffer.Emplace<Graphic::Render::DrawTerrainCommand>(
                     transform.GetTransform(), terrain.Size, terrain.Resolution, terrain.HeightScale,
                     terrain.NoiseFrequency, terrain.Seed, layerModes, terrainComp.SplatMap.get(),
                     grassParams,
                     glm::vec3( terrain.GrassBrightness, static_cast<float>( terrain.GrassBladesPerClump ),
                                0.0f ), // x=brightness, y=bladesPerClump (packed into the GrassTint channel)
                     std::move( overrides ), std::move( textureOverrides ) );
            }
        }
    };
} // namespace Desert::ECS
