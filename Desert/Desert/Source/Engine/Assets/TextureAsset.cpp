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

        m_SourcePath = dataReflected->SourcePath;

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