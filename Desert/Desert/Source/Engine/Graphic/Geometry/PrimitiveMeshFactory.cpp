#include "PrimitiveMeshFactory.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

namespace Desert
{
    bool PrimitiveMeshFactory::s_Initialized = false;
    std::array<std::shared_ptr<Mesh>, static_cast<size_t>( PrimitiveType::Count )>
         PrimitiveMeshFactory::s_Primitives;

    const std::array<std::string, static_cast<size_t>( PrimitiveType::Count )>
         PrimitiveMeshFactory::s_PrimitiveNames = { "Cube",     "Sphere",  "Pyramid", "Plane",
                                                    "Cylinder", "Capsule", "Terrain", "LightCube" };

    void PrimitiveMeshFactory::Initialize()
    {
        if ( s_Initialized )
            return;

        CreateCube();
        CreateSphere();
        CreatePyramid();
        CreatePlane();
        CreateCylinder();
        CreateCapsule();
        CreateTerrain();
        CreateLightCube();

        s_Initialized = true;
    }

    void PrimitiveMeshFactory::Shutdown()
    {
        for ( auto& mesh : s_Primitives )
        {
            mesh.reset();
        }
        s_Initialized = false;
    }

    std::shared_ptr<Mesh> PrimitiveMeshFactory::GetPrimitive( PrimitiveType type )
    {
        if ( !s_Initialized )
            Initialize();

        return s_Primitives[static_cast<size_t>( type )];
    }

    const std::string& PrimitiveMeshFactory::GetPrimitiveName( PrimitiveType type )
    {
        return s_PrimitiveNames[static_cast<size_t>( type )];
    }

    void PrimitiveMeshFactory::CreateCube()
    {
        std::vector<Vertex> vertices;
        std::vector<Index>  indices;

        // Cube vertices (8 vertices)
        const float size = 0.5f;
        vertices         = {
             // Front face
             { { -size, -size, size },
                       { 0.0f, 0.0f, 1.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 1.0f, 0.0f },
                       { 0.0f, 0.0f } },
             { { size, -size, size },
                       { 0.0f, 0.0f, 1.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 1.0f, 0.0f },
                       { 1.0f, 0.0f } },
             { { size, size, size },
                       { 0.0f, 0.0f, 1.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 1.0f, 0.0f },
                       { 1.0f, 1.0f } },
             { { -size, size, size },
                       { 0.0f, 0.0f, 1.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 1.0f, 0.0f },
                       { 0.0f, 1.0f } },

             // Back face
             { { -size, -size, -size },
                       { 0.0f, 0.0f, -1.0f },
                       { -1.0f, 0.0f, 0.0f },
                       { 0.0f, 1.0f, 0.0f },
                       { 1.0f, 0.0f } },
             { { -size, size, -size },
                       { 0.0f, 0.0f, -1.0f },
                       { -1.0f, 0.0f, 0.0f },
                       { 0.0f, 1.0f, 0.0f },
                       { 1.0f, 1.0f } },
             { { size, size, -size },
                       { 0.0f, 0.0f, -1.0f },
                       { -1.0f, 0.0f, 0.0f },
                       { 0.0f, 1.0f, 0.0f },
                       { 0.0f, 1.0f } },
             { { size, -size, -size },
                       { 0.0f, 0.0f, -1.0f },
                       { -1.0f, 0.0f, 0.0f },
                       { 0.0f, 1.0f, 0.0f },
                       { 0.0f, 0.0f } },

             // Right, Left, Top, Bottom faces would be added similarly...
        };

        indices = { // Front face
                    { 0, 1, 2 },
                    { 2, 3, 0 },
                    // Back face
                    { 4, 5, 6 },
                    { 6, 7, 4 },
                    // Right face
                    { 1, 7, 6 },
                    { 6, 2, 1 },
                    // Left face
                    { 0, 3, 5 },
                    { 5, 4, 0 },
                    // Top face
                    { 3, 2, 6 },
                    { 6, 5, 3 },
                    // Bottom face
                    { 0, 4, 7 },
                    { 7, 1, 0 } };

        s_Primitives[static_cast<size_t>( PrimitiveType::Cube )] =
             std::make_shared<Mesh>( vertices, indices, "Cube" );
    }

    void PrimitiveMeshFactory::CreateSphere()
    {
        std::vector<Vertex> vertices;
        std::vector<Index>  indices;

        const int   segments = 32;
        const int   rings    = 16;
        const float radius   = 0.5f;

        // Generate vertices
        for ( int ring = 0; ring <= rings; ++ring )
        {
            float v   = static_cast<float>( ring ) / rings;
            float phi = v * glm::pi<float>();

            for ( int segment = 0; segment <= segments; ++segment )
            {
                float u     = static_cast<float>( segment ) / segments;
                float theta = u * 2.0f * glm::pi<float>();

                float x = radius * std::sin( phi ) * std::cos( theta );
                float y = radius * std::cos( phi );
                float z = radius * std::sin( phi ) * std::sin( theta );

                glm::vec3 normal    = glm::normalize( glm::vec3( x, y, z ) );
                glm::vec3 tangent   = glm::normalize( glm::vec3( -std::sin( theta ), 0.0f, std::cos( theta ) ) );
                glm::vec3 bitangent = glm::cross( normal, tangent );

                vertices.push_back( { { x, y, z }, normal, tangent, bitangent, { u, v } } );
            }
        }

        // Generate indices
        for ( int ring = 0; ring < rings; ++ring )
        {
            for ( int segment = 0; segment < segments; ++segment )
            {
                uint32_t first  = ring * ( segments + 1 ) + segment;
                uint32_t second = first + segments + 1;

                indices.push_back( { first, second, first + 1 } );
                indices.push_back( { second, second + 1, first + 1 } );
            }
        }

        s_Primitives[static_cast<size_t>( PrimitiveType::Sphere )] =
             std::make_shared<Mesh>( vertices, indices, "Sphere" );
    }

    void PrimitiveMeshFactory::CreatePyramid()
    {
        std::vector<Vertex> vertices;
        std::vector<Index>  indices;

        const float size   = 0.5f;
        const float height = 1.0f;

        vertices = { // Base
                     { { -size, -size, -size },
                       { 0.0f, -1.0f, 0.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 0.0f, 1.0f },
                       { 0.0f, 0.0f } },
                     { { size, -size, -size },
                       { 0.0f, -1.0f, 0.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 0.0f, 1.0f },
                       { 1.0f, 0.0f } },
                     { { size, -size, size },
                       { 0.0f, -1.0f, 0.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 0.0f, 1.0f },
                       { 1.0f, 1.0f } },
                     { { -size, -size, size },
                       { 0.0f, -1.0f, 0.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 0.0f, 1.0f },
                       { 0.0f, 1.0f } },

                     // Apex
                     { { 0.0f, height, 0.0f },
                       { 0.0f, 0.0f, 0.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 1.0f, 0.0f },
                       { 0.5f, 0.5f } } };

        indices = { // Base
                    { 0, 1, 2 },
                    { 2, 3, 0 },
                    // Sides
                    { 0, 4, 1 },
                    { 1, 4, 2 },
                    { 2, 4, 3 },
                    { 3, 4, 0 } };

        s_Primitives[static_cast<size_t>( PrimitiveType::Pyramid )] =
             std::make_shared<Mesh>( vertices, indices, "Pyramid" );
    }

    void PrimitiveMeshFactory::CreatePlane()
    {
        std::vector<Vertex> vertices;
        std::vector<Index>  indices;

        const float size         = 1.0f;
        const int   subdivisions = 10;

        // Simple plane implementation
        vertices = { { { -size, 0.0f, -size },
                       { 0.0f, 1.0f, 0.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 0.0f, 1.0f },
                       { 0.0f, 0.0f } },
                     { { size, 0.0f, -size },
                       { 0.0f, 1.0f, 0.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 0.0f, 1.0f },
                       { 1.0f, 0.0f } },
                     { { size, 0.0f, size },
                       { 0.0f, 1.0f, 0.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 0.0f, 1.0f },
                       { 1.0f, 1.0f } },
                     { { -size, 0.0f, size },
                       { 0.0f, 1.0f, 0.0f },
                       { 1.0f, 0.0f, 0.0f },
                       { 0.0f, 0.0f, 1.0f },
                       { 0.0f, 1.0f } } };

        indices = { { 0, 1, 2 }, { 2, 3, 0 } };

        s_Primitives[static_cast<size_t>( PrimitiveType::Plane )] =
             std::make_shared<Mesh>( vertices, indices, "Plane" );
    }

    void PrimitiveMeshFactory::CreateCylinder()
    {
        // Implementation for cylinder
        s_Primitives[static_cast<size_t>( PrimitiveType::Cylinder )] =
             std::make_shared<Mesh>( std::vector<Vertex>{}, std::vector<Index>{}, "Cylinder" );
    }

    void PrimitiveMeshFactory::CreateCapsule()
    {
        // Implementation for capsule
        s_Primitives[static_cast<size_t>( PrimitiveType::Capsule )] =
             std::make_shared<Mesh>( std::vector<Vertex>{}, std::vector<Index>{}, "Capsule" );
    }

    void PrimitiveMeshFactory::CreateTerrain()
    {
        // Implementation for terrain
        s_Primitives[static_cast<size_t>( PrimitiveType::Terrain )] =
             std::make_shared<Mesh>( std::vector<Vertex>{}, std::vector<Index>{}, "Terrain" );
    }

    void PrimitiveMeshFactory::CreateLightCube()
    {
        // Light cube - similar to regular cube but with different properties if needed
        CreateCube(); // Reuse cube for now
        s_Primitives[static_cast<size_t>( PrimitiveType::LightCube )] =
             s_Primitives[static_cast<size_t>( PrimitiveType::Cube )];
    }
} // namespace Desert