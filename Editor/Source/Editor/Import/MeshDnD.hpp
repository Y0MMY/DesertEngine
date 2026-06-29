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
    // (e.g. a skinned source, which cooks to .skmesh — not handled here).
    Assets::AssetHandle ResolveOrImport( Assets::AssetManager& mgr, const std::string& sourcePath );

} // namespace Desert::Editor::MeshDnD
