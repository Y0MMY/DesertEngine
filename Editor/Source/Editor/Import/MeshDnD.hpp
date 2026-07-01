#pragma once

#include <Engine/Assets/AssetManager.hpp>

#include <string>

namespace Desert::Editor::MeshDnD
{
    // Drag-and-drop / import glue for meshes. The File Explorer drags SOURCE paths (Resources/Mesh/foo.obj)
    // but meshes only load from the COOKED form (Cooked/Meshes/foo.stmesh). These helpers bridge a dropped
    // source path to a registered runtime StaticMesh handle, cooking on demand if needed (mirrors
    // Editor::TextureDnD).

    // Resolve a dropped mesh source path to a usable StaticMesh handle, cooking the source into a .stmesh +
    // creating/registering the asset on demand if it isn't cooked yet. Returns a null handle on failure
    // (e.g. a skinned source, which cooks to .skmesh — use ResolveOrImportMesh for those).
    Assets::AssetHandle ResolveOrImport( Assets::AssetManager& mgr, const std::string& sourcePath );

    // A resolved mesh handle + whether it's a SKINNED (rigged character) mesh. The caller spawns the matching
    // component (StaticMeshComponent vs SkinnedMeshComponent + AnimationComponent).
    struct ResolvedMesh
    {
        Assets::AssetHandle Handle;
        bool                Skinned = false;
    };

    // Resolve a dropped mesh source to EITHER a static (.stmesh) or skinned (.skmesh) mesh — whichever the cook
    // produces — creating/registering the right asset type on demand. This is what the viewport drop uses so a
    // rigged character (e.g. a Mixamo FBX) spawns as a SkinnedMesh, not silently fails. Null handle on failure.
    ResolvedMesh ResolveOrImportMesh( Assets::AssetManager& mgr, const std::string& sourcePath );

} // namespace Desert::Editor::MeshDnD
