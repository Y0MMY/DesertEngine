#include "CloudVolumeService.hpp"

namespace Desert::Runtime
{
    Common::BoolResultStr
    CloudVolumeService::Register( const std::shared_ptr<Assets::CloudVolumeAsset>& volumeAsset )
    {
        if ( !volumeAsset )
            return Common::MakeError( "Cannot register a null cloud volume asset" );

        if ( !volumeAsset->GetMetadata().IsValid() )
            return Common::MakeFormattedError( "Cloud volume asset '{}' has no valid metadata handle",
                                               volumeAsset->GetMetadata().Filepath.string() );

        m_Volumes[volumeAsset->GetMetadata().Handle] = volumeAsset;
        return BOOLSUCCESS;
    }

    std::shared_ptr<Assets::CloudVolumeAsset> CloudVolumeService::Get( const Assets::AssetHandle& handle ) const
    {
        const auto it = m_Volumes.find( handle );
        return it != m_Volumes.end() ? it->second : nullptr;
    }

    void CloudVolumeService::Clear()
    {
        m_Volumes.clear();
    }
} // namespace Desert::Runtime
