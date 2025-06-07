#include <Engine/Graphic/Materials/Models/PBR/PBRMaterialHelper.hpp>

namespace Desert::Graphic::Models::PBR
{

    void PBRMaterial::UpdatePBR( PBRUniforms&& pbr )
    {
        m_PBRUniforms = std::move(pbr);

        m_Uniform->RT_SetData( &m_PBRUniforms, sizeof( PBRUniforms ), 0 );
    }

} // namespace Desert::Graphic::Models::PBR