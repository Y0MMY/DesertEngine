#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic::MaterialHelpers
{
    struct PBRUniforms
    {
        glm::vec3 Albedo{ 1.0f };
        float     Metallic  = 0.0f;
        float     Roughness = 0.5f;
        float     AO        = 1.0f;
        glm::vec3 Emissive{ 0.0f };
        float     _padding; 
    };

    class PBRMaterial
    {
    public:
        explicit PBRMaterial( const std::shared_ptr<Material>& baseMaterial );

        void SetAlbedo( const glm::vec3& color );
        void SetMetallic( float value );
        void SetRoughness( float value );
        void ApplyAll( const PBRUniforms& params );

        void Override(const std::shared_ptr<UniformBuffer>& uniform) const;

    private:
        std::shared_ptr<Material> m_Material;
        PBRUniforms               m_CurrentParams;
    };
} // namespace Desert::Graphic::MaterialHelpers