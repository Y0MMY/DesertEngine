#include "StaticMaterialPBR.hpp"

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <rflcpp/rfl/json/write.hpp>

#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic
{
    void StaticMaterialPBR::Bind( const UpdateMaterialPBRInfo& info )
    {
        UpdateCamera( *this, info.MainCamera );

        UpdatePointLights( *this, info.PointLights );
        UpdateDirectionLights( *this, info.DirectionLights );
        UpdateLightsMetadata( *this, info.PointLights, info.DirectionLights );

        UpdatePBRTextures( *this, info.PBREnvTextures );
        UpdatePBRMaterial( *this, {} );

        UpdateTextures( m_MaterialExecutor.get() );

        m_MaterialExecutor->PushConstant( &info.MeshTransform, sizeof( glm::mat4 ) );
    }

} // namespace Desert::Graphic