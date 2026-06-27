#pragma once

#include "../RenderCommand.hpp"
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <string>
#include <utility>
#include <vector>

namespace Desert::Graphic
{
    class Image2D;
}

namespace Desert::Graphic::Render
{
    // Carries one terrain entity's transform + params from the ECS (TerrainComponent) to the
    // TerrainRenderer (via SceneRenderer::SubmitTerrain) for this frame. ParamOverrides come from an
    // optional MaterialComponent on the same entity (name -> value), applied generically by name.
    struct DrawTerrainCommand : RenderCommand
    {
        glm::mat4 Transform;
        float     Size;
        int       Resolution;
        float     HeightScale;
        float     NoiseFrequency;
        int       Seed;

        glm::vec3 LayerModes  = glm::vec3( 0.0f ); // grass/rock/snow: 0=Auto, 1=Manual, 2=Off
        Image2D*  SplatMap    = nullptr;           // per-terrain painted splat map (non-owning)
        glm::vec4 GrassParams = glm::vec4( 0.0f ); // x=enable, y=density, z=height, w=width (Stage 7 grass)
        glm::vec3 GrassTint   = glm::vec3( 1.0f ); // RGB tint multiplier on grass color

        std::vector<std::pair<std::string, glm::vec4>> ParamOverrides;
        std::vector<std::pair<std::string, uint64_t>>  TextureOverrides;

        DrawTerrainCommand( const glm::mat4& transform, float size, int resolution, float heightScale,
                            float noiseFrequency, int seed, const glm::vec3& layerModes = glm::vec3( 0.0f ),
                            Image2D* splatMap = nullptr, const glm::vec4& grassParams = glm::vec4( 0.0f ),
                            const glm::vec3& grassTint = glm::vec3( 1.0f ),
                            std::vector<std::pair<std::string, glm::vec4>> paramOverrides   = {},
                            std::vector<std::pair<std::string, uint64_t>>  textureOverrides = {} )
             : Transform( transform ), Size( size ), Resolution( resolution ), HeightScale( heightScale ),
               NoiseFrequency( noiseFrequency ), Seed( seed ), LayerModes( layerModes ), SplatMap( splatMap ),
               GrassParams( grassParams ), GrassTint( grassTint ), ParamOverrides( std::move( paramOverrides ) ),
               TextureOverrides( std::move( textureOverrides ) )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SubmitTerrain( Transform, Size, Resolution, HeightScale, NoiseFrequency, Seed,
                                    LayerModes, SplatMap, GrassParams, GrassTint, ParamOverrides,
                                    TextureOverrides );
        }
    };
} // namespace Desert::Graphic::Render
