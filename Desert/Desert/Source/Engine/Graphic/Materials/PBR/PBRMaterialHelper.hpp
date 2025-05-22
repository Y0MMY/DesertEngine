#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialTechnique.hpp>

namespace Desert::Graphic::MaterialHelper
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

    class PBRMaterial : public MateriaTtechniques
    {
    public:
        using MateriaTtechniques::MateriaTtechniques;

        void SetAlbedo( const glm::vec3& color );
        void SetMetallic( float value );
        void SetRoughness( float value );
        void ApplyAll( const PBRUniforms& params );

        void Override( const std::shared_ptr<UniformBuffer>& uniform ) const;

    private:
        PBRUniforms               m_CurrentParams;
    };
} // namespace Desert::Graphic::MaterialHelper