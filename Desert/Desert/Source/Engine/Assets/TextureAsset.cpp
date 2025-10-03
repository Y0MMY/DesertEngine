#include <Engine/Assets/TextureAsset.hpp>

namespace Desert::Assets
{

    TextureAsset::TextureAsset( AssetPriority priority, const Common::Filepath& filepath, Type type )
         : AssetBase( priority, filepath, AssetTypeID::Texture2D ), m_Type( type )
    {
    }

    Common::BoolResultStr TextureAsset::Load()
    {
        m_Texture = Graphic::Texture2D::Create( { true }, m_Metadata.Filepath);
        m_Texture->Invalidate();

        m_IsReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr TextureAsset::Unload()
    {
        return BOOLSUCCESS;
    }

} // namespace Desert::Assets