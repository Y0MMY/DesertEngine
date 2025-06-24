#include <Engine/Graphic/Materials/Models/PBR/PBRMaterialHelper.hpp>

namespace Desert::Graphic::Models::PBR
{

    void PBRMaterial::UpdatePBR( PBRUniforms&& pbr )
    {
        m_PBRUniforms = std::move( pbr );

        m_UniformProperty->SetData( &m_PBRUniforms, sizeof( PBRUniforms ) );
    }

} // namespace Desert::Graphic::Models::PBR