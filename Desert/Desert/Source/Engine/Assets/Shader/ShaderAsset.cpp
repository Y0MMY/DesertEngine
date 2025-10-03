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
        m_ShaderContent = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        m_ReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr ShaderAsset::Unload()
    {
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets