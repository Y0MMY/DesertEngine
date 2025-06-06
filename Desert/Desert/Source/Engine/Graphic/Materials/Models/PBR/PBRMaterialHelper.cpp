#include <Engine/Graphic/Materials/Models/PBR/PBRMaterialHelper.hpp>

namespace Desert::Graphic::Models::PBR
{

    void PBRMaterial::Override( const PBRUniforms& pbr, const std::shared_ptr<UniformBuffer>& uniform ) const
    {
        uniform->SetData( (void*)&pbr, sizeof( PBRUniforms ), 0 );
        m_Material->AddUniformToOverride( uniform );
    }

} // namespace Desert::Graphic::MaterialHelper