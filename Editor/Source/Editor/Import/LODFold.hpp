#pragma once

#include <Engine/Assets/Serialization/Mesh.hpp>

namespace Desert::Editor
{
    // Folds author-authored LOD sibling meshes (named "<base>_LOD<n>", n >= 1) into their base submesh: each
    // LOD's vertices are appended INTO the base submesh's contiguous vertex block and its (offset-adjusted)
    // triangle set is stored in SubmeshData.LODs; the LOD sibling submeshes are then removed. Because the LOD
    // indices end up inside the base submesh's vertex range, the load/draw paths need NO change — they treat
    // these exactly like generated LODs (index sets drawn with baseVertex = Submesh.VertexOffset).
    //
    // Static meshes only (skinned meshes are not LOD-ed). Meshes with no "_LOD" siblings are left untouched.
    // Returns the number of LOD levels folded (0 = nothing to do).
    int FoldExternalLODMeshes( Assets::Serialization::MeshAssetData& data );
} // namespace Desert::Editor
