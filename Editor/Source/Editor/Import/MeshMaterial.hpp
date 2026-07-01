#pragma once

// Engine/Assets/Common.hpp is NOT self-contained (uses Common::UUID / std::shared_ptr / nullptr_t without
// including them) — pull its prerequisites first so this header works as a first include.
#include <Common/Core/UUID.hpp>
#include <cstddef>
#include <memory>
#include <Engine/Assets/Common.hpp> // Assets::AssetHandle

#include <string>

namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Editor::MeshMaterial
{
    // Resolve a mesh's "sidecar" material — the mesh<->material link: <stem>.demat next to the mesh, else any
    // *.demat in the mesh's folder, else in the parent (collection) folder. Registers it and returns its
    // handle (null if none found). Shared by mesh spawning (ViewportPanel) and the mesh thumbnail renderer so
    // a previewed/spawned mesh shows the pack's real material. (Per-slot from a collection manifest is a
    // future extension of this.)
    Assets::AssetHandle ResolveSidecar( Assets::AssetManager& mgr, const std::string& meshSourcePath );
} // namespace Desert::Editor::MeshMaterial
