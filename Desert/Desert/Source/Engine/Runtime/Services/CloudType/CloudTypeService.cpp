#include "CloudTypeService.hpp"

#include <Common/Core/Logger.hpp>

namespace Desert::Runtime
{
    Common::BoolResultStr CloudTypeService::Register( const std::shared_ptr<Assets::CloudTypeAsset>& asset )
    {
        if ( !asset )
            return Common::MakeError( "CloudTypeService::Register was given a null asset" );

        if ( !asset->IsReadyForUse() )
            return Common::MakeFormattedError<bool>( "cloud type '{}' is not loaded",
                                                     asset->GetMetadata().Filepath.string() );

        const Assets::AssetHandle& handle = asset->GetMetadata().Handle;

        if ( auto it = m_Types.find( handle ); it != m_Types.end() && it->second.Revision == asset->GetRevision() )
            return BOOLSUCCESS;

        m_Types[handle] = Entry{ asset->GetShape(), asset->GetNoiseVolume(), asset->GetRevision() };
        ++m_Generation;

        LOG_INFO( "[Clouds] Cloud type '{}' registered as {} ({}).", asset->GetDisplayName(),
                  static_cast<uint64_t>( handle ), asset->GetMetadata().Filepath.filename().string() );
        return BOOLSUCCESS;
    }

    const Graphic::CloudTypeShape& CloudTypeService::GetShape( const Assets::AssetHandle& handle )
    {
        if ( handle != 0 )
        {
            if ( auto it = m_Types.find( handle ); it != m_Types.end() )
                return it->second.Shape;

            // Said once per handle rather than once per frame: a missing type is a permanent state of the
            // scene, and a message repeated sixty times a second is a log nobody reads. The set is what
            // makes the FIRST occurrence findable.
            if ( m_Reported.insert( handle ).second )
                LOG_ERROR( "[Clouds] Cloud type {} is referenced but not registered — the layer falls back "
                           "to the built-in default. The scene names a .decloudtype the asset scan did not "
                           "find.",
                           static_cast<uint64_t>( handle ) );
        }

        return Assets::CloudTypeDefaultShape();
    }

    Assets::AssetHandle CloudTypeService::GetNoiseVolume( const Assets::AssetHandle& handle ) const
    {
        if ( handle != 0 )
        {
            if ( auto it = m_Types.find( handle ); it != m_Types.end() )
                return it->second.NoiseVolume;
        }

        // The built-in type has no volume of its own, and a null handle is exactly what
        // CloudNoiseService reads as "the default volume". The two empty slots therefore mean the same
        // thing, which is the property that keeps a scene with nothing authored in it renderable.
        return Assets::AssetHandle::Null();
    }

    void CloudTypeService::Clear()
    {
        m_Types.clear();
        m_Reported.clear();
        ++m_Generation;
    }
} // namespace Desert::Runtime
