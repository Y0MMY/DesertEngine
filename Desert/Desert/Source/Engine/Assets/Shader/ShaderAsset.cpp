#include "ShaderAsset.hpp"

#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Assets
{
    ShaderAsset::ShaderAsset( AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, GetTypeID() )
    {
    }

    Common::BoolResultStr ShaderAsset::Load()
    {
        // A missing .shader file used to "load" as empty content and fail later, inside the
        // compiler, with a message that no longer named the file. Refuse here, with the path.
        auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );
        if ( !raw )
            return Common::MakeError( raw.GetError() );
        m_ShaderContent = raw.ExtractValue();

        m_ReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr ShaderAsset::Unload()
    {
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets