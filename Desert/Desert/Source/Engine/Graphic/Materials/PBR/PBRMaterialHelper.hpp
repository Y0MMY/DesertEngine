#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialTechnique.hpp>

namespace Desert::Graphic::MaterialHelper
{
    struct alignas( 16 ) PBRUniforms
    {
        glm::vec3 Albedo{ 1.0f };
        float     Metallic  = 0.0f;
        float     Roughness = 0.5f;
        float     AO        = 1.0f;
        glm::vec3 Emissive{ 0.0f };
    };

    class PBRMaterial : public MateriaTtechniques
    {
    public:
        using MateriaTtechniques::MateriaTtechniques;

        void Override( const PBRUniforms& pbr, const std::shared_ptr<UniformBuffer>& uniform ) const;
    };
} // namespace Desert::Graphic::MaterialHelper