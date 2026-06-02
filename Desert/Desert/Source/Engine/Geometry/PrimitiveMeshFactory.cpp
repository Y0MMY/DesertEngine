#include "PrimitiveMeshFactory.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

#include <Engine/Geometry/MeshFactory.hpp>

namespace Desert::Geometry
{
    std::shared_ptr<DynamicMesh> PrimitiveMeshFactory::Create( PrimitiveType type )
    {
        switch ( type )
        {
            case PrimitiveType::Cube:
                return CreateCube();
            case PrimitiveType::Sphere:
                return CreateSphere();
            case PrimitiveType::Plane:
                return CreatePlane();
            case PrimitiveType::Pyramid:
                return CreatePyramid();
            default:
                return nullptr;
        }
    }

    std::shared_ptr<DynamicMesh> PrimitiveMeshFactory::CreateCube()
    {
        std::vector<Vertex> vertices = {
            // Front face
            { { -0.5f, -0.5f,  0.5f }, { 0.0f,  0.0f,  1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
            { {  0.5f, -0.5f,  0.5f }, { 0.0f,  0.0f,  1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
            { {  0.5f,  0.5f,  0.5f }, { 0.0f,  0.0f,  1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
            { { -0.5f,  0.5f,  0.5f }, { 0.0f,  0.0f,  1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
            // Back face
            { { -0.5f, -0.5f, -0.5f }, { 0.0f,  0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
            { { -0.5f,  0.5f, -0.5f }, { 0.0f,  0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
            { {  0.5f,  0.5f, -0.5f }, { 0.0f,  0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
            { {  0.5f, -0.5f, -0.5f }, { 0.0f,  0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
            // Top face
            { { -0.5f,  0.5f, -0.5f }, { 0.0f,  1.0f,  0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f } },
            { { -0.5f,  0.5f,  0.5f }, { 0.0f,  1.0f,  0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f } },
            { {  0.5f,  0.5f,  0.5f }, { 0.0f,  1.0f,  0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f } },
            { {  0.5f,  0.5f, -0.5f }, { 0.0f,  1.0f,  0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } },
            // Bottom face
            { { -0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f,  0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
            { {  0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f,  0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
            { {  0.5f, -0.5f,  0.5f }, { 0.0f, -1.0f,  0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
            { { -0.5f, -0.5f,  0.5f }, { 0.0f, -1.0f,  0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
            // Left face
            { { -0.5f, -0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
            { { -0.5f, -0.5f,  0.5f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
            { { -0.5f,  0.5f,  0.5f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
            { { -0.5f,  0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
            // Right face
            { {  0.5f, -0.5f, -0.5f }, { 1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
            { {  0.5f,  0.5f, -0.5f }, { 1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
            { {  0.5f,  0.5f,  0.5f }, { 1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
            { {  0.5f, -0.5f,  0.5f }, { 1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } }
        };

        std::vector<Index> indices = {
            { 0,  1,  2 },  { 2,  3,  0 },
            { 4,  5,  6 },  { 6,  7,  4 },
            { 8,  9,  10 }, { 10, 11, 8 },
            { 12, 13, 14 }, { 14, 15, 12 },
            { 16, 17, 18 }, { 18, 19, 16 },
            { 20, 21, 22 }, { 22, 23, 20 }
        };

        std::vector<Submesh> submeshes = {
            { "Cube", 0, (uint32_t)vertices.size(), 0, (uint32_t)indices.size() * 3, glm::mat4(1.0f), {} }
        };

        return std::make_shared<DynamicMesh>( vertices, indices, submeshes );
    }

    std::shared_ptr<DynamicMesh> PrimitiveMeshFactory::CreateSphere()
    {
        // TODO: Implement actual sphere vertex generation logic
        return nullptr;
    }

    std::shared_ptr<DynamicMesh> PrimitiveMeshFactory::CreatePlane()
    {
        // TODO: Implement actual plane vertex generation logic
        return nullptr;
    }

    std::shared_ptr<DynamicMesh> PrimitiveMeshFactory::CreatePyramid()
    {
        // TODO: Implement actual pyramid vertex generation logic
        return nullptr;
    }

} // namespace Desert::Geometry