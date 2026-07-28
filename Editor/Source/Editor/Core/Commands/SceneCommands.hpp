#pragma once

#include <Common/Core/UUID.hpp>

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Desert::Core
{
    class Scene;
}
namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Editor::Commands
{
    // Undoable structural scene operations. Each helper PERFORMS the action (where there is one) and
    // records a UUID-addressed command in the CommandHistory, so entries survive anything except the
    // scene itself being replaced (EditorLayer clears the history on scene load / Play / Stop).
    //
    // Subtree snapshots reuse the prefab serialization path (EntitySerializer), so delete-undo and
    // duplication preserve every serializable component — same fidelity as saving the scene.

    // The scene/asset-manager the commands operate on. Set once by EditorLayer.
    void SetContext( ::Desert::Core::Scene* scene, const ::Desert::Assets::AssetManager* assetManager );

    // Record entities just created by UI code (Add menu, prefab instantiate, viewport drag-drop) as ONE
    // undo step. Undo deletes them (snapshotting first), redo restores them with the same UUIDs.
    void NotifyCreated( const std::vector<Common::UUID>& roots );

    // Destroy the entity (and subtree) undoably: undo restores it — same UUIDs, same parent.
    void DeleteEntity( const Common::UUID& uuid );

    // Multi-selection delete: drops entries whose ancestor is also in the list (the ancestor's delete
    // already covers them), destroys the rest as ONE undo step.
    void DeleteEntities( const std::vector<Common::UUID>& uuids );

    // Clone the entity subtree (fresh UUIDs, same parent, root tagged "<name> Copy").
    // Returns the duplicate's root UUID (Null on failure).
    Common::UUID DuplicateEntity( const Common::UUID& uuid );

    // Multi-selection duplicate (top-level roots only) as ONE undo step. Returns the new root UUIDs.
    std::vector<Common::UUID> DuplicateEntities( const std::vector<Common::UUID>& uuids );

    // Attach child to newParent (Null -> detach to scene root), recording the previous parent for undo.
    void Reparent( const Common::UUID& child, const Common::UUID& newParent );

    // Multi-selection reparent (top-level roots only) as ONE undo step.
    void ReparentMany( const std::vector<Common::UUID>& children, const Common::UUID& newParent );

    // Rename the entity's tag undoably. No-ops on empty/unchanged names.
    void Rename( const Common::UUID& uuid, const std::string& newName );

    // Record a finished transform edit (gizmo drag): oldT/R/S = values before the drag; the entity's
    // CURRENT transform is captured as the "new" state. No-ops if nothing actually changed.
    void RecordTransformEdit( const Common::UUID& uuid, const glm::vec3& oldTranslation,
                              const glm::vec3& oldRotation, const glm::vec3& oldScale );

    // Pre-drag TRS of one entity (group gizmo drags capture one per selected root).
    struct TransformSnapshot
    {
        Common::UUID Entity;
        glm::vec3    Translation{ 0.0f };
        glm::vec3    Rotation{ 0.0f };
        glm::vec3    Scale{ 1.0f };
    };

    // Multi-entity variant: one undo step for the whole group drag (entities whose transform did not
    // actually change are skipped).
    void RecordTransformEdits( const std::vector<TransformSnapshot>& before );

    // ---- Editor clipboard (in-memory, serialized snapshots — survives deleting the originals) ----

    // Ctrl+C: snapshot the top-level roots of the given entities into the clipboard.
    void CopySelectionToClipboard( const std::vector<Common::UUID>& uuids );

    bool ClipboardHasContent();

    // Ctrl+V: instantiate the clipboard with fresh UUIDs (each root re-attaches to its original parent
    // when that parent still exists). One undo step; returns the pasted root UUIDs.
    std::vector<Common::UUID> PasteClipboard();

    // ---- Prefab instance tools ----

    // Overwrites the instance's source .deprefab file with the instance's CURRENT state (no undo — it is
    // a file operation; the caller shows a confirmation). Returns false when the entity is not a prefab
    // instance or its asset cannot be resolved.
    bool ApplyPrefabInstance( const Common::UUID& uuid );

    // Replaces the instance subtree with a fresh instantiation of its source prefab (same world position,
    // same parent). ONE undo step (undo brings the modified instance back). Returns the new root UUID.
    Common::UUID RevertPrefabInstance( const Common::UUID& uuid );

    // Runs `mutate` (a component add/remove from the Details panel) undoably: the entity subtree is
    // snapshotted before and after, and undo/redo swap between the two serialized states (delete +
    // recreate with preserved UUIDs — so selection and later history entries stay valid).
    void MutateEntityUndoable( const Common::UUID& uuid, const std::function<void()>& mutate );
} // namespace Desert::Editor::Commands
