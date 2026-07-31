#pragma once

#include <Engine/Desert.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <Common/Core/Constants.hpp>

#include "../IPanel.hpp"

namespace Desert::Editor
{
    class SceneHierarchyPanel final : public IPanel
    {
    public:
        explicit SceneHierarchyPanel( const std::shared_ptr<Desert::Core::Scene>&  scene,
                                      const std::shared_ptr<Assets::AssetManager>& assetManager )
             : IPanel( "Scene Outliner" ), m_Scene( scene ), m_AssetManager( assetManager )
        {
        }
        void OnUIRender() override;
        void SetScene( const std::shared_ptr<Desert::Core::Scene>& scene ) override
        {
            m_Scene = scene;
        }

    private:
        static const char* GetEntityTypeName( const ECS::Entity& entity );
        void               DrawEntityNode( ECS::Entity& entity );
        void               DrawInstantiatePrefabPopup();
        void               DrawSavePrefabPopup();
        void               SelectRangeTo( const Common::UUID& target ); // Shift+click

    private:
        std::shared_ptr<Desert::Core::Scene>        m_Scene;
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
        ImGuiTextFilter                             m_HierarchyFilter;
        ImGuiTextFilter                             m_AddComponentFilter; // shared grouped Add-Component menu
        ImGuiTextFilter                             m_AddEntityFilter;    // grouped "+ Add" (entity) menu
        // Member default -> evaluated at panel construction, i.e. AFTER the project path remap.
        std::string m_PrefabInstantiatePath =
             ( Common::Constants::Path::PREFAB_PATH / "MyPrefab.deprefab" ).string();
        bool m_OpenInstantiatePrefab = false; // deferred OpenPopup

        // "Save as Prefab..." (context menu -> modal at panel scope, same deferred pattern).
        bool                        m_OpenSavePrefab = false;
        std::optional<Common::UUID> m_SavePrefabTarget;
        std::string                 m_SavePrefabPath;

        // Prefab instance tools (Apply overwrites the source file -> confirmation modal; Revert mutates
        // the scene -> deferred like delete).
        bool                        m_OpenApplyPrefab = false;
        std::optional<Common::UUID> m_ApplyPrefabTarget;
        std::optional<Common::UUID> m_PendingPrefabRevert;
        std::optional<Common::UUID> m_PendingPrefabUnpack; // strip the prefab link from a subtree (make local)
        // Deferred structural edits, applied after the UI iteration (they create/destroy entities or edit
        // the Children vectors the tree walk is iterating). Lists: an operation on an entity that is part
        // of the multi-selection applies to the WHOLE selection.
        std::vector<Common::UUID> m_PendingDelete;
        std::vector<Common::UUID> m_PendingDuplicate;

        // {children, newParent} (Null parent = detach to root).
        std::optional<std::pair<std::vector<Common::UUID>, Common::UUID>> m_PendingReparent;

        // Inline rename (double-click the name / context menu -> Rename).
        std::optional<Common::UUID> m_RenamingEntity;
        std::string                 m_RenameBuffer;
        bool                        m_RenameFocusPending = false;

        // Visible draw order of the tree, rebuilt each frame; Shift+click ranges use LAST frame's order
        // (the list the user is actually looking at).
        std::vector<Common::UUID> m_VisibleOrder;
        std::vector<Common::UUID> m_VisibleOrderLast;
    };
} // namespace Desert::Editor