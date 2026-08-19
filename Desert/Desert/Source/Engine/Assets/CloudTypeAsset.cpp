#include <Engine/Assets/CloudTypeAsset.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/CloudNoiseVolumeAsset.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/VFS.hpp>

#include <fstream>

namespace Desert::Assets
{
    CloudTypeAsset::CloudTypeAsset( AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::CloudType )
    {
        // The base class hands out a RANDOM uuid, which is correct for an asset with no source of identity
        // and wrong for this one: a fresh id every launch is a service cache that misses on every restart,
        // and it is what the noise volume asset next door still does. Derived from the PATH, like a mesh's,
        // and that is enough because a scene stores this reference as a path rather than as a handle (see
        // the CloudTypeAsset branch of Core::MakeAssetResolver) — the handle only has to be stable and
        // unique within a session.
        m_Metadata.Handle = Common::AssetHandle::FromCookedPath( m_Metadata.Filepath );
        m_DisplayName     = m_Metadata.Filepath.stem().string();
    }

    Common::BoolResultStr CloudTypeAsset::Load()
    {
        const std::string path = m_Metadata.Filepath.string();

        // Through the VFS first, so a packaged build reads the type out of its .dpak exactly like every
        // other asset, then off the disk for a loose file the pak does not carry.
        std::string text;
        if ( const auto packed = Common::Utils::VFS::Exists( m_Metadata.Filepath )
                                      ? Common::Utils::VFS::ReadFile( m_Metadata.Filepath )
                                      : std::nullopt;
             packed.has_value() )
        {
            text = packed.value();
        }
        else
        {
            text = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );
        }

        if ( text.empty() )
        {
            m_Ready = false;
            return Common::MakeFormattedError<bool>( "Cloud type '{}' is empty or could not be opened", path );
        }

        auto parsed = ParseCloudType( text );
        if ( !parsed )
        {
            m_Ready = false;
            return Common::MakeFormattedError<bool>( "Cloud type '{}' is not usable: {}", path,
                                                     parsed.GetError() );
        }

        m_Data        = parsed.ExtractValue();
        m_DisplayName = m_Data.DisplayName.value_or( m_Metadata.Filepath.stem().string() );

        ++m_Revision;
        m_Ready = true;

        LOG_INFO( "[Clouds] Cloud type '{}' loaded: {}, base {:.2f} km, top {:.2f} km, edge {:.2f}, "
                  "anvil {:.2f} at {:.2f} km, detail {:.2f} ({}), density x{:.2f}, extinction x{:.2f}.",
                  path, m_DisplayName, m_Data.Shape.BaseAltitudeKm, m_Data.Shape.TopAltitudeKm,
                  m_Data.Shape.EdgeTopFraction, m_Data.Shape.AnvilStrength, m_Data.Shape.AnvilAltitudeKm,
                  m_Data.Shape.DetailFactor, m_Data.Shape.DetailCharacter < 0.5f ? "wispy" : "billowy",
                  m_Data.Shape.DensityFactor, m_Data.Shape.ExtinctionFactor );

        return BOOLSUCCESS;
    }

    void CloudTypeAsset::ResolveDependencies( AssetManager& manager )
    {
        m_NoiseVolume = AssetHandle::Null();

        const std::string relative = m_Data.NoiseVolume.value_or( std::string{} );
        if ( relative.empty() )
            return; // the documented "use the built-in default volume"

        // RELATIVE TO THE ASSETS ROOT, joined here and nowhere else. The file stores
        // "Clouds/CloudNoise_FineWisp.dcnv" so that the library is the same library on another machine;
        // the AssetManager indexes volumes under their full project-rooted path, so exactly one join has
        // to happen and this is it.
        const Common::Filepath full = ( Common::Constants::Path::ASSETS_PATH / relative ).lexically_normal();

        if ( const auto volume = manager.FindByPath<CloudNoiseVolumeAsset>( full ) )
        {
            m_NoiseVolume = volume->GetMetadata().Handle;
            return;
        }

        // NOT a silent fall-through to the default: the type names a volume, the volume is not there, and
        // the sky that comes out will be the default one wearing this type's name. §1.4.
        LOG_ERROR( "[Clouds] Cloud type '{}' names noise volume '{}' ({}), which is not loaded. The layer "
                   "will use the built-in default volume and its edge will not be the authored one.",
                   m_Metadata.Filepath.string(), relative, full.string() );
    }

    Common::BoolResultStr CloudTypeAsset::Unload()
    {
        m_Ready = false;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr CloudTypeAsset::Save( const Common::Filepath& filepath, const CloudTypeData& data )
    {
        if ( auto valid = ValidateCloudTypeShape( data.Shape ); !valid )
            return Common::MakeFormattedError<bool>( "refusing to write '{}': {}", filepath.string(),
                                                     valid.GetError() );

        std::error_code ec;
        if ( filepath.has_parent_path() )
            std::filesystem::create_directories( filepath.parent_path(), ec );

        CloudTypeData written = data;
        written.FormatVersion = kCloudTypeFormatVersion;

        const std::string text = WriteCloudType( written );

        std::ofstream file( filepath, std::ios::trunc );
        if ( !file )
            return Common::MakeFormattedError<bool>( "'{}' could not be opened for writing", filepath.string() );

        file.write( text.data(), static_cast<std::streamsize>( text.size() ) );
        if ( !file )
            return Common::MakeFormattedError<bool>( "'{}' was opened but the {} bytes could not be written",
                                                     filepath.string(), text.size() );

        LOG_INFO( "[Clouds] Cloud type written: '{}', base {:.2f} km, top {:.2f} km, {} bytes.", filepath.string(),
                  written.Shape.BaseAltitudeKm, written.Shape.TopAltitudeKm, text.size() );
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets
