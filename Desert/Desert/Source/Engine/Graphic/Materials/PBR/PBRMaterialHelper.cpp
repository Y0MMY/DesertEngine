#include <Engine/Graphic/Materials/PBR/PBRMaterialHelper.hpp>

namespace Desert::Graphic::MaterialHelper
{

    void PBRMaterial::Override( const PBRUniforms& pbr, const std::shared_ptr<UniformBuffer>& uniform ) const
    {
        uniform->SetData( (void*)&pbr, sizeof( PBRUniforms ), 0 );
        m_Material->AddUniformToOverride( uniform );
    }

} // namespace Desert::Graphic::MaterialHelper