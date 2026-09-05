#include <Engine/Assets/TextureAsset.hpp>

#include <Engine/Assets/Serialization/Texture.hpp>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    TextureAsset::TextureAsset( AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::Texture2D )
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

        // The file stores the source's PLACE in the project (`assets:Textures/T.png`), not a path — that is
        // what lets a committed .tex load pixels on a machine whose checkout sits anywhere. Expanded here,
        // once, through the same root table that wrote it, so every consumer of GetSourcePath() (the GPU
        // upload in TextureFactory, the GIF decode, the editor's filename labels) keeps seeing a path this
        // machine can open. A stored value with no known tag passes through unchanged; that covers .tex
        // files from before this form existed and sources genuinely outside the project.
        m_SourcePath = Common::AssetHandle::PathForStableKey( dataReflected->SourcePath ).string();

        // The cooked `.tex` carries an id of its own, which additionally survives a rename of the file, so
        // it wins over the path-derived handle AssetBase installed. GetHandle() reads this same field, so
        // TextureService and the editor cannot disagree about which id a texture has.
        m_Metadata.Handle = dataReflected->Handle;

        m_IsReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr TextureAsset::Unload()
    {
        return BOOLSUCCESS;
    }

} // namespace Desert::Assets