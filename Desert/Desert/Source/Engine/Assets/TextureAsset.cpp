#include <Engine/Assets/TextureAsset.hpp>

#include <Engine/Assets/Serialization/Texture.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    TextureAsset::TextureAsset( AssetPriority priority, const Common::Filepath& filepath /*, Type type*/ )
         : AssetBase( priority, filepath, AssetTypeID::Texture2D ) /*, m_Type( type )*/
    {
    }

    Common::BoolResultStr TextureAsset::Load()
    {
        auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        const auto dataReflected = rfl::json::read<Serialization::TextureAssetData>( raw );
        if ( !dataReflected.has_value() )
        {
            return Common::MakeError( dataReflected.error().what() );
        }

        m_Handle        = dataReflected->Handle;
        m_SourcePath    = dataReflected->SourcePath;

        // Keep the asset metadata handle in lock-step with the cooked .tex handle. TextureService::Register
        // keys runtime textures by GetHandle() (this m_Handle), while editor/material code looks textures
        // up by GetMetadata().Handle — if they diverge, every handle-based resolve (drag-drop assignment,
        // thumbnails, material binding) misses. Syncing here makes all of those agree.
        m_Metadata.Handle = dataReflected->Handle;

        m_IsReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr TextureAsset::Unload()
    {
        return BOOLSUCCESS;
    }

} // namespace Desert::Assets