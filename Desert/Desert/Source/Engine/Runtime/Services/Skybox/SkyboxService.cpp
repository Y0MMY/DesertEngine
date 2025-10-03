#include "SkyboxService.hpp"

namespace Desert::Runtime
{
    Common::BoolResultStr SkyboxService::Register( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAsset )
    {
        if ( !skyboxAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Skybox asset is invalid" );
        }

        m_Skyboxes[skyboxAsset->GetMetadata().Handle] =
             std::make_shared<Graphic::MaterialSkybox>(skyboxAsset);
        return BOOLSUCCESS;
    }

    std::shared_ptr<Desert::Graphic::MaterialSkybox> SkyboxService::Get( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Skyboxes.find( handle );
        return ( it != m_Skyboxes.end() ) ? it->second : nullptr;
    }

    void SkyboxService::Clear()
    {
    }

} // namespace Desert::Runtime