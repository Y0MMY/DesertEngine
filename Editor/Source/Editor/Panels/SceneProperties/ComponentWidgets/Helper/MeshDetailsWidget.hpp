#pragma once

#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Editor
{
    class MeshDetailsWidget final
    {
    public:
        static void ShowMeshInfo( const Assets::Asset<Assets::MeshAsset>& currentSelectedMesh,
                                  const Assets::AssetHandle&              meshHandle );
    };
} // namespace Desert::Editor