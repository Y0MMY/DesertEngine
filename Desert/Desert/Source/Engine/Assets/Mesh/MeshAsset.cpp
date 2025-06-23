#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Assets
{
    Common::BoolResult MeshAsset::Load()
    {
        m_Mesh = std::make_shared<Mesh>( m_Filepath.string() );
        m_Mesh->Invalidate();

        m_ReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResult MeshAsset::Unload()
    {
        m_Mesh.reset();

        return BOOLSUCCESS;
    }

} // namespace Desert::Assets