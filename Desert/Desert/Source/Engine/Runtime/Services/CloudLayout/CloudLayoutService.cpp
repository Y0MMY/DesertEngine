#include "CloudLayoutService.hpp"

#include <Common/Core/Logger.hpp>

namespace Desert::Runtime
{
    Common::BoolResultStr CloudLayoutService::Register( const std::shared_ptr<Assets::CloudLayoutAsset>& asset )
    {
        if ( !asset )
            return Common::MakeError( "CloudLayoutService::Register was given a null asset" );

        if ( !asset->IsReadyForUse() )
            return Common::MakeFormattedError<bool>( "cloud layout '{}' is not loaded",
                                                     asset->GetMetadata().Filepath.string() );

        const Assets::AssetHandle& handle = asset->GetMetadata().Handle;
        const uint32_t             hash   = asset->GetLayout().ContentHash;

        // THE CONTENT HASH IS THE REVISION HERE, where the sibling services keep a counter. It is the
        // stronger statement of the two: a counter says "the file was read again", the hash says "the
        // pixels differ". Re-saving a painting without changing it therefore does NOT invalidate the baked
        // volume, and re-baking two million voxels is a stall an artist feels.
        if ( auto it = m_Layouts.find( handle ); it != m_Layouts.end() && it->second.ContentHash == hash )
            return BOOLSUCCESS;

        m_Layouts[handle] = Entry{ asset, hash };

        const Assets::CloudLayoutData& layout = asset->GetLayout();
        LOG_INFO( "[Clouds] Cloud layout '{}' registered as {} ({}x{}, pattern {}, mask {}, content {:08x}).",
                  asset->GetMetadata().Filepath.filename().string(), static_cast<uint64_t>( handle ),
                  layout.Resolution, layout.Resolution, layout.HasPattern() ? "yes" : "no",
                  layout.HasMask() ? "yes" : "no", hash );
        return BOOLSUCCESS;
    }

    std::shared_ptr<const Assets::CloudLayoutData> CloudLayoutService::Get( const Assets::AssetHandle& handle )
    {
        if ( handle == 0 )
            return nullptr;

        if ( auto it = m_Layouts.find( handle ); it != m_Layouts.end() && it->second.Asset )
        {
            // The aliasing constructor: a share of the ASSET's lifetime, addressing its layout member. See
            // the header for why the bake must not be handed a borrowed pointer.
            return std::shared_ptr<const Assets::CloudLayoutData>( it->second.Asset,
                                                                   &it->second.Asset->GetLayout() );
        }

        // Said once per handle rather than once per frame: a missing layout is a permanent state of the
        // scene, and a message repeated sixty times a second is a log nobody reads.
        if ( m_Reported.insert( handle ).second )
            LOG_ERROR( "[Clouds] Cloud layout {} is referenced but not registered — the layer places its "
                       "clouds procedurally instead. The scene names a .dclayout the asset scan did not find.",
                       static_cast<uint64_t>( handle ) );

        return nullptr;
    }

    void CloudLayoutService::Clear()
    {
        m_Layouts.clear();
        m_Reported.clear();
    }
} // namespace Desert::Runtime
