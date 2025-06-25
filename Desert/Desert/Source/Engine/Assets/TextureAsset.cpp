#include <Engine/Assets/TextureAsset.hpp>

namespace Desert::Assets
{

    TextureAsset::TextureAsset( AssetPriority priority, const Common::Filepath& filepath, Type type )
         : AssetBase( priority, filepath ), m_Type( type )
    {
    }

    Common::BoolResult TextureAsset::Load()
    {
        m_Texture = Graphic::Texture2D::Create( { true }, m_Filepath );
        m_Texture->Invalidate();

        m_IsReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResult TextureAsset::Unload()
    {
        return BOOLSUCCESS;
    }

} // namespace Desert::Assets