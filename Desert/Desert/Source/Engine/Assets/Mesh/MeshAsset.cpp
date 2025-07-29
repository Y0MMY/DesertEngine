#include <Engine/Assets/Mesh/MeshAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Assets
{
    MeshAsset::MeshAsset( const AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::Mesh )
    {
    }

    Common::BoolResult MeshAsset::Load()
    {
        m_RawData = Common::Utils::FileSystem::ReadByteFileContent( m_Metadata.Filepath );
        m_ReadyForUse = true;
        Notify( EventType::OnReady );
        return BOOLSUCCESS;
    }

    Common::BoolResult MeshAsset::Unload()
    {
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets