#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Assets
{
    Common::BoolResult MeshAsset::Load()
    {
        m_Mesh = std::make_shared<Mesh>( m_Filepath.string() );
        m_Mesh->Invalidate();

        return BOOLSUCCESS;
    }

    Common::BoolResult MeshAsset::Unload()
    {
        m_Mesh.reset();

        return BOOLSUCCESS;
    }

} // namespace Desert::Assets