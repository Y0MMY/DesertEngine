#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Assets
{
    MeshAsset::MeshAsset( const AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::Mesh )
    {
    }

    Common::BoolResult MeshAsset::Load()
    {
        m_Mesh = std::make_shared<Mesh>( m_Metadata.Filepath.string() );
        m_Mesh->Invalidate();

        m_ReadyForUse = true;
        Notify( EventType::OnReady );
        return BOOLSUCCESS;
    }

    Common::BoolResult MeshAsset::Unload()
    {
        m_Mesh.reset();

        return BOOLSUCCESS;
    }
} // namespace Desert::Assets