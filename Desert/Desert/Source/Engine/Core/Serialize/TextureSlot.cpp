#include <Engine/Core/Serialize/TextureSlot.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/TextureAsset.hpp>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <filesystem>

namespace Desert::Core::Serialize
{
    std::string TextureSlotToPath( const Assets::AssetManager& manager, uint64_t handle )
    {
        if ( handle == 0 )
            return "";

        const auto asset = manager.FindByHandle<Assets::TextureAsset>( Common::UUID( handle ) );
        if ( !asset )
        {
            LOG_ERROR( "[Textures] Handle {0} is set on a texture slot and no texture with that handle is "
                       "registered, so the slot is being written out EMPTY and the reference is lost. "
                       "Cooked textures are scanned from '{1}'.",
                       handle, Common::Constants::Path::TEXTURE_PATH_COOKED.string() );
            return "";
        }

        return Common::AssetHandle::StableKeyForPath( asset->GetMetadata().Filepath );
    }

    uint64_t TextureSlotFromPath( Assets::AssetManager& manager, const std::string& stored )
    {
        if ( stored.empty() )
            return 0;

        const std::filesystem::path full = Common::AssetHandle::PathForStableKey( stored );

        auto asset = manager.FindByPath<Assets::TextureAsset>( full );

        // THE EXISTENCE CHECK IS NOT DEFENSIVE, IT IS LOAD-BEARING. CreateAsset loads the asset, and
        // TextureAsset::Load reads through Common::Utils::FileSystem::ReadFileContent, whose miss path is
        // a DESERT_VERIFY — i.e. in Debug a scene naming a texture that is not on disk would abort the
        // editor instead of reporting a missing texture. Asked first, the same case becomes the log line
        // below.
        if ( !asset && Common::Utils::FileSystem::Exists( full ) )
            asset = manager.CreateAsset<Assets::TextureAsset>( Assets::AssetPriority::High, full );

        if ( !asset )
        {
            LOG_ERROR( "[Textures] Texture '{0}' named by the scene did not resolve (it expands to '{1}'; "
                       "cooked textures live under '{2}', content textures under '{3}'). The slot stays "
                       "unset.",
                       stored, full.string(), Common::Constants::Path::TEXTURE_PATH_COOKED.string(),
                       Common::Constants::Path::TEXTUREDIR_PATH.string() );
            return 0;
        }

        return static_cast<uint64_t>( asset->GetMetadata().Handle );
    }
} // namespace Desert::Core::Serialize
