#pragma once

#include <Engine/Geometry/DynamicMesh.hpp>
#include <Common/Core/Math/AABB.hpp>

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace Desert::Geometry
{
    // Header-only procedural heightmap terrain (a grid plane on XZ with fBm value-noise heights). Used for a
    // quick playground location; mirrors PrimitiveMeshFactory (build vertices/indices/submesh -> DynamicMesh).
    namespace TerrainNoise
    {
        inline float Hash( int x, int z, uint32_t seed )
        {
            uint32_t h = seed + (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
            h          = ( h ^ ( h >> 13 ) ) * 1274126177u;
            return (float)( ( h ^ ( h >> 16 ) ) & 0xFFFFFFu ) / (float)0xFFFFFFu; // 0..1
        }

        inline float ValueNoise( float x, float z, uint32_t seed )
        {
            const int   xi = (int)std::floor( x ), zi = (int)std::floor( z );
            const float xf = x - xi, zf = z - zi;
            const float u = xf * xf * ( 3.0f - 2.0f * xf );
            const float v = zf * zf * ( 3.0f - 2.0f * zf );
            const float a = Hash( xi, zi, seed ), b = Hash( xi + 1, zi, seed );
            const float c = Hash( xi, zi + 1, seed ), d = Hash( xi + 1, zi + 1, seed );
            return glm::mix( glm::mix( a, b, u ), glm::mix( c, d, u ), v );
        }

        inline float Fbm( float x, float z, uint32_t seed )
        {
            float sum = 0.0f, amp = 0.5f, freq = 1.0f;
            for ( int i = 0; i < 5; ++i )
            {
                sum += amp * ValueNoise( x * freq, z * freq, seed + (uint32_t)i * 101u );
                freq *= 2.0f;
                amp *= 0.5f;
            }
            return sum; // ~0..1
        }
    } // namespace TerrainNoise

    class TerrainMeshFactory
    {
    public:
        static std::shared_ptr<DynamicMesh> Create( uint32_t resolution, float size, float heightScale,
                                                    float noiseFrequency, uint32_t seed )
        {
            resolution                = resolution < 1 ? 1 : resolution;
            const uint32_t vertsPerRow = resolution + 1;
            const float    half        = size * 0.5f;
            const float    step        = size / (float)resolution;

            const auto heightAt = [&]( float wx, float wz )
            { return ( TerrainNoise::Fbm( wx * noiseFrequency, wz * noiseFrequency, seed ) - 0.5f ) * heightScale; };

            std::vector<Vertex> vertices;
            vertices.reserve( (size_t)vertsPerRow * vertsPerRow );

            Common::Math::AABB aabb;
            aabb.Min = glm::vec3( 1e9f );
            aabb.Max = glm::vec3( -1e9f );

            for ( uint32_t z = 0; z < vertsPerRow; ++z )
            {
                for ( uint32_t x = 0; x < vertsPerRow; ++x )
                {
                    const float wx = -half + x * step;
                    const float wz = -half + z * step;
                    const float wy = heightAt( wx, wz );

                    // Normal from central differences of the height field.
                    const float d  = step;
                    const float hl = heightAt( wx - d, wz ), hr = heightAt( wx + d, wz );
                    const float hd = heightAt( wx, wz - d ), hu = heightAt( wx, wz + d );
                    const glm::vec3 normal = glm::normalize( glm::vec3( hl - hr, 2.0f * d, hd - hu ) );

                    Vertex v;
                    v.Position  = { wx, wy, wz };
                    v.Normal    = normal;
                    v.Tangent   = { 1.0f, 0.0f, 0.0f };
                    v.Bitangent = { 0.0f, 0.0f, 1.0f };
                    v.TexCoord  = { (float)x / resolution, (float)z / resolution };
                    vertices.push_back( v );

                    aabb.Min = glm::min( aabb.Min, v.Position );
                    aabb.Max = glm::max( aabb.Max, v.Position );
                }
            }

            std::vector<Index> indices;
            indices.reserve( (size_t)resolution * resolution * 2 );
            for ( uint32_t z = 0; z < resolution; ++z )
            {
                for ( uint32_t x = 0; x < resolution; ++x )
                {
                    const uint32_t a = z * vertsPerRow + x;
                    const uint32_t b = a + vertsPerRow;
                    // CCW-up winding (top face visible), consistent with the cube/sphere convention.
                    indices.push_back( { a, a + 1, b } );
                    indices.push_back( { a + 1, b + 1, b } );
                }
            }

            std::vector<Submesh> submeshes = {
                { "Terrain", 0, (uint32_t)vertices.size(), 0, (uint32_t)indices.size() * 3, glm::mat4( 1.0f ),
                  aabb } };

            return std::make_shared<DynamicMesh>( vertices, indices, submeshes );
        }
    };
} // namespace Desert::Geometry
