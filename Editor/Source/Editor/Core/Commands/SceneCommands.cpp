#include "SceneCommands.hpp"

#include <Editor/Core/CommandHistory.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Logger.hpp>

#include <glm/gtc/epsilon.hpp>

#include <functional>
#include <optional>
#include <unordered_map>

namespace Desert::Editor::Commands
{
    namespace
    {
        // NOTE: fully qualified — inside Desert::Editor, an unqualified `Core::` means Editor::Core.
        ::Desert::Core::Scene*                s_Scene        = nullptr;
        const ::Desert::Assets::AssetManager* s_AssetManager = nullptr;

        // Editor clipboard: serialized subtree snapshots (NOT entity references), so pasting works even
        // after the originals were deleted or the scene changed.
        std::vector<std::vector<Assets::EntityData>> s_Clipboard;

        bool Ready()
        {
            return s_Scene && s_AssetManager;
        }

        // Entity is a lightweight value handle — copying out of the const ref gives a mutable handle.
        std::optional<ECS::Entity> FindEntity( const Common::UUID& uuid )
        {
            if ( !s_Scene || uuid.IsNull() )
                return std::nullopt;
            if ( auto ref = s_Scene->FindEntityByID( uuid ) )
                return ref->get();
            return std::nullopt;
        }

        Common::UUID ParentUUIDOf( ECS::Entity entity )
        {
            if ( entity.HasComponent<ECS::RelationshipComponent>() )
            {
                const auto parentHandle = entity.GetComponent<ECS::RelationshipComponent>().Parent;
                if ( parentHandle != entt::null )
                {
                    ECS::Entity parent( parentHandle, *entity.GetRegistry() );
                    if ( parent.HasComponent<ECS::UUIDComponent>() )
                        return parent.GetComponent<ECS::UUIDComponent>().UUID;
                }
            }
            return Common::UUID::Null();
        }

        // Pre-order subtree snapshot through the prefab serialization path: full component fidelity,
        // root first. The root's own `parent` field records where the subtree hung in the scene.
        std::vector<Assets::EntityData> CaptureSubtree( ECS::Entity root )
        {
            std::vector<Assets::EntityData> data;

            std::function<void( ECS::Entity )> traverse = [&]( ECS::Entity e )
            {
                if ( !e )
                    return;
                data.push_back( ::Desert::Core::Serialize::EntitySerializer::SerializeEntity( e, *s_AssetManager ) );
                if ( e.HasComponent<ECS::RelationshipComponent>() )
                {
                    for ( auto childHandle : e.GetComponent<ECS::RelationshipComponent>().Children )
                        traverse( ECS::Entity( childHandle, *e.GetRegistry() ) );
                }
            };
            traverse( root );

            return data;
        }

        // Rebuilds a captured subtree. preserveIds=true (undo of a delete) recreates the exact same
        // UUIDs; false (duplicate) mints fresh ones. In-snapshot parent links are remapped; the root's
        // outer parent is re-attached by UUID if it still exists in the scene. Returns the new root.
        ECS::Entity RestoreSnapshot( const std::vector<Assets::EntityData>& data, bool preserveIds )
        {
            std::unordered_map<Common::UUID, ECS::Entity> idMap;

            for ( const auto& ed : data )
            {
                const Common::UUID original = ed.id.value_or( Common::UUID::Null() );
                if ( preserveIds && FindEntity( original ) )
                    continue; // already alive (double-restore guard)

                const Common::UUID newId = preserveIds ? original : Common::UUID();
                ECS::Entity        e = s_Scene->CreateEntityWithUUID( newId, ed.Tag.value_or( "Entity" ) );
                idMap.insert( { original, e } );
            }

            ECS::Entity root;
            for ( const auto& ed : data )
            {
                auto it = idMap.find( ed.id.value_or( Common::UUID::Null() ) );
                if ( it == idMap.end() )
                    continue;

                ECS::Entity e = it->second;
                ::Desert::Core::Serialize::EntitySerializer::DeserializeEntity( ed, e, *s_AssetManager );

                if ( ed.parent.has_value() && !ed.parent->IsNull() )
                {
                    if ( auto parentIt = idMap.find( *ed.parent ); parentIt != idMap.end() )
                        s_Scene->Attach( parentIt->second, e ); // in-snapshot child
                    else if ( auto outer = FindEntity( *ed.parent ) )
                        s_Scene->Attach( *outer, e ); // the root, back onto its original scene parent
                }

                if ( !root )
                    root = e;
            }
            return root;
        }

        Common::UUID UUIDOf( ECS::Entity e )
        {
            return e.HasComponent<ECS::UUIDComponent>() ? e.GetComponent<ECS::UUIDComponent>().UUID
                                                        : Common::UUID::Null();
        }

        void DestroyByUUID( const Common::UUID& uuid )
        {
            if ( auto e = FindEntity( uuid ) )
            {
                s_Scene->DestroyEntity( *e );
                if ( const auto& sel = Core::SelectionManager::GetSelected();
                     sel.has_value() && *sel == uuid )
                    Core::SelectionManager::ClearSelection();
            }
        }

        // Entity creation/destruction relocates entt component pools — any raw property-byte entries in
        // the history would then point at freed memory, so drop them around every structural mutation.
        void OnStructuralChange()
        {
            CommandHistory::Get().DropVolatile();
        }

        // ---------------------------------------------------------------- commands

        class TransformCommand final : public ICommand
        {
        public:
            TransformCommand( const Common::UUID& entity, const glm::vec3& oldT, const glm::vec3& oldR,
                              const glm::vec3& oldS, const glm::vec3& newT, const glm::vec3& newR,
                              const glm::vec3& newS )
                 : m_Entity( entity ), m_Old{ oldT, oldR, oldS }, m_New{ newT, newR, newS }
            {
            }

            std::string GetLabel() const override
            {
                return "Move / Transform";
            }

            bool Undo() override
            {
                return Apply( m_Old );
            }
            bool Redo() override
            {
                return Apply( m_New );
            }

        private:
            struct TRS
            {
                glm::vec3 T, R, S;
            };

            bool Apply( const TRS& trs )
            {
                auto e = FindEntity( m_Entity );
                if ( !e || !e->HasComponent<ECS::TransformComponent>() )
                    return false;
                auto& tc       = e->GetComponent<ECS::TransformComponent>();
                tc.Translation = trs.T;
                tc.Rotation    = trs.R;
                tc.Scale       = trs.S;
                return true;
            }

            Common::UUID m_Entity;
            TRS          m_Old, m_New;
        };

        class RenameCommand final : public ICommand
        {
        public:
            RenameCommand( const Common::UUID& entity, std::string oldName, std::string newName )
                 : m_Entity( entity ), m_OldName( std::move( oldName ) ), m_NewName( std::move( newName ) )
            {
            }

            std::string GetLabel() const override
            {
                return "Rename";
            }

            bool Undo() override
            {
                return Apply( m_OldName );
            }
            bool Redo() override
            {
                return Apply( m_NewName );
            }

        private:
            bool Apply( const std::string& name )
            {
                auto e = FindEntity( m_Entity );
                if ( !e || !e->HasComponent<ECS::TagComponent>() )
                    return false;
                e->GetComponent<ECS::TagComponent>().Tag = name;
                return true;
            }

            Common::UUID m_Entity;
            std::string  m_OldName, m_NewName;
        };

        class ReparentCommand final : public ICommand
        {
        public:
            ReparentCommand( const Common::UUID& child, const Common::UUID& oldParent,
                             const Common::UUID& newParent )
                 : m_Child( child ), m_OldParent( oldParent ), m_NewParent( newParent )
            {
            }

            std::string GetLabel() const override
            {
                return "Reparent";
            }

            bool Undo() override
            {
                return SetParent( m_OldParent );
            }
            bool Redo() override
            {
                return SetParent( m_NewParent );
            }

        private:
            bool SetParent( const Common::UUID& parent )
            {
                auto child = FindEntity( m_Child );
                if ( !child )
                    return false;
                if ( parent.IsNull() )
                {
                    s_Scene->Detach( *child );
                    return true;
                }
                auto parentEntity = FindEntity( parent );
                if ( !parentEntity )
                    return false;
                s_Scene->Attach( *parentEntity, *child );
                return true;
            }

            Common::UUID m_Child, m_OldParent, m_NewParent;
        };

        class DeleteCommand final : public ICommand
        {
        public:
            explicit DeleteCommand( std::vector<Assets::EntityData>&& snapshot )
                 : m_Snapshot( std::move( snapshot ) )
            {
            }

            std::string GetLabel() const override
            {
                return "Delete";
            }

            bool Undo() override
            {
                if ( m_Snapshot.empty() )
                    return false;
                ECS::Entity root = RestoreSnapshot( m_Snapshot, /*preserveIds=*/true );
                OnStructuralChange();
                if ( !root )
                    return false;
                Core::SelectionManager::SetSelected( UUIDOf( root ) );
                return true;
            }

            bool Redo() override
            {
                const Common::UUID root =
                     m_Snapshot.empty() ? Common::UUID::Null() : m_Snapshot.front().id.value_or( Common::UUID::Null() );
                if ( !FindEntity( root ) )
                    return false;
                DestroyByUUID( root );
                OnStructuralChange();
                return true;
            }

        private:
            std::vector<Assets::EntityData> m_Snapshot;
        };

        // Groups sub-commands into ONE history entry (multi-selection operations). Undo runs the
        // sub-commands in reverse; a stale sub-command is skipped, not fatal.
        class CompositeCommand final : public ICommand
        {
        public:
            void Add( std::unique_ptr<ICommand> command )
            {
                m_Commands.push_back( std::move( command ) );
            }

            bool Empty() const
            {
                return m_Commands.empty();
            }

            size_t Size() const
            {
                return m_Commands.size();
            }

            std::unique_ptr<ICommand> TakeSingle()
            {
                auto cmd = std::move( m_Commands.front() );
                m_Commands.clear();
                return cmd;
            }

            std::string GetLabel() const override
            {
                if ( m_Commands.size() == 1 )
                    return m_Commands.front()->GetLabel();
                return "Grouped edit (" + std::to_string( m_Commands.size() ) + ")";
            }

            bool Undo() override
            {
                bool any = false;
                for ( auto it = m_Commands.rbegin(); it != m_Commands.rend(); ++it )
                    any |= ( *it )->Undo();
                return any;
            }

            bool Redo() override
            {
                bool any = false;
                for ( auto& cmd : m_Commands )
                    any |= cmd->Redo();
                return any;
            }

        private:
            std::vector<std::unique_ptr<ICommand>> m_Commands;
        };

        // Keeps only the "top-level" entries: entities whose ancestor is ALSO in the list are dropped
        // (group delete/duplicate/reparent must not process a subtree twice).
        std::vector<Common::UUID> FilterTopLevel( const std::vector<Common::UUID>& uuids )
        {
            auto contains = [&]( const Common::UUID& id )
            {
                for ( const auto& u : uuids )
                    if ( u == id )
                        return true;
                return false;
            };

            std::vector<Common::UUID> result;
            for ( const auto& id : uuids )
            {
                auto e = FindEntity( id );
                if ( !e )
                    continue;

                bool covered = false;
                for ( Common::UUID p = ParentUUIDOf( *e ); !p.IsNull(); )
                {
                    if ( contains( p ) )
                    {
                        covered = true;
                        break;
                    }
                    auto pe = FindEntity( p );
                    p = pe ? ParentUUIDOf( *pe ) : Common::UUID::Null();
                }
                if ( !covered )
                    result.push_back( id );
            }
            return result;
        }

        // Performs the reparent and returns the undo command (nullptr when it was a no-op / refused).
        std::unique_ptr<ICommand> DoReparent( const Common::UUID& child, const Common::UUID& newParent )
        {
            auto childEntity = FindEntity( child );
            if ( !childEntity )
                return nullptr;

            const Common::UUID oldParent = ParentUUIDOf( *childEntity );
            if ( oldParent == newParent )
                return nullptr;

            if ( newParent.IsNull() )
            {
                s_Scene->Detach( *childEntity );
            }
            else
            {
                auto parentEntity = FindEntity( newParent );
                if ( !parentEntity )
                    return nullptr;
                s_Scene->Attach( *parentEntity, *childEntity );
                // Attach refuses cycles (dropping a parent onto its own child) — record nothing then.
                if ( ParentUUIDOf( *childEntity ) != newParent )
                    return nullptr;
            }
            return std::make_unique<ReparentCommand>( child, oldParent, newParent );
        }

        // Two serialized states of one entity subtree; undo/redo swap between them by delete + recreate
        // with preserved UUIDs (component add/remove has no cheaper reversible form — the registry has no
        // per-component "remove by key" path, and delete+recreate reuses the proven snapshot machinery).
        class EntityStateCommand final : public ICommand
        {
        public:
            EntityStateCommand( std::vector<Assets::EntityData>&& before,
                                std::vector<Assets::EntityData>&& after )
                 : m_Before( std::move( before ) ), m_After( std::move( after ) )
            {
            }

            std::string GetLabel() const override
            {
                return "Component / entity edit";
            }

            bool Undo() override
            {
                return Apply( m_Before );
            }
            bool Redo() override
            {
                return Apply( m_After );
            }

        private:
            bool Apply( const std::vector<Assets::EntityData>& state )
            {
                if ( state.empty() )
                    return false;
                const Common::UUID root = state.front().id.value_or( Common::UUID::Null() );
                if ( root.IsNull() )
                    return false;
                DestroyByUUID( root );
                const bool ok = static_cast<bool>( RestoreSnapshot( state, /*preserveIds=*/true ) );
                OnStructuralChange();
                return ok;
            }

            std::vector<Assets::EntityData> m_Before, m_After;
        };

        // Records ALREADY-created entities. The snapshot is taken lazily on the first Undo — so it also
        // captures any edits made between creation and the undo (redo brings the entity back exactly as
        // it was when it disappeared, which is what the user expects).
        class CreateCommand final : public ICommand
        {
        public:
            explicit CreateCommand( std::vector<Common::UUID> roots ) : m_Roots( std::move( roots ) )
            {
            }

            std::string GetLabel() const override
            {
                return m_Roots.size() > 1 ? "Create (" + std::to_string( m_Roots.size() ) + ")" : "Create";
            }

            bool Undo() override
            {
                bool any = false;
                m_Snapshots.clear();
                for ( const auto& root : m_Roots )
                {
                    if ( auto e = FindEntity( root ) )
                    {
                        m_Snapshots.push_back( CaptureSubtree( *e ) );
                        DestroyByUUID( root );
                        any = true;
                    }
                }
                if ( any )
                    OnStructuralChange();
                return any;
            }

            bool Redo() override
            {
                bool any = false;
                for ( const auto& snapshot : m_Snapshots )
                {
                    if ( ECS::Entity root = RestoreSnapshot( snapshot, /*preserveIds=*/true ) )
                    {
                        Core::SelectionManager::SetSelected( UUIDOf( root ) );
                        any = true;
                    }
                }
                if ( any )
                    OnStructuralChange();
                return any;
            }

        private:
            std::vector<Common::UUID>                    m_Roots;
            std::vector<std::vector<Assets::EntityData>> m_Snapshots;
        };
    } // namespace

    // ---------------------------------------------------------------- public helpers

    void SetContext( ::Desert::Core::Scene* scene, const ::Desert::Assets::AssetManager* assetManager )
    {
        s_Scene        = scene;
        s_AssetManager = assetManager;
    }

    void NotifyCreated( const std::vector<Common::UUID>& roots )
    {
        if ( !Ready() )
            return;

        std::vector<Common::UUID> alive;
        for ( const auto& uuid : roots )
            if ( FindEntity( uuid ) )
                alive.push_back( uuid );
        if ( alive.empty() )
            return;

        CommandHistory::Get().PushCommand( std::make_unique<CreateCommand>( std::move( alive ) ) );
        OnStructuralChange();
    }

    void DeleteEntity( const Common::UUID& uuid )
    {
        if ( !Ready() )
            return;
        auto e = FindEntity( uuid );
        if ( !e )
            return;

        auto snapshot = CaptureSubtree( *e );
        DestroyByUUID( uuid );
        CommandHistory::Get().PushCommand( std::make_unique<DeleteCommand>( std::move( snapshot ) ) );
        OnStructuralChange();
    }

    void DeleteEntities( const std::vector<Common::UUID>& uuids )
    {
        if ( !Ready() )
            return;

        auto composite = std::make_unique<CompositeCommand>();
        for ( const auto& uuid : FilterTopLevel( uuids ) )
        {
            auto e = FindEntity( uuid );
            if ( !e )
                continue;
            auto snapshot = CaptureSubtree( *e );
            DestroyByUUID( uuid );
            composite->Add( std::make_unique<DeleteCommand>( std::move( snapshot ) ) );
        }
        if ( composite->Empty() )
            return;

        if ( composite->Size() == 1 )
            CommandHistory::Get().PushCommand( composite->TakeSingle() );
        else
            CommandHistory::Get().PushCommand( std::move( composite ) );
        OnStructuralChange();
    }

    Common::UUID DuplicateEntity( const Common::UUID& uuid )
    {
        if ( !Ready() )
            return Common::UUID::Null();
        auto source = FindEntity( uuid );
        if ( !source )
            return Common::UUID::Null();

        ECS::Entity duplicate = RestoreSnapshot( CaptureSubtree( *source ), /*preserveIds=*/false );
        if ( !duplicate )
            return Common::UUID::Null();

        if ( duplicate.HasComponent<ECS::TagComponent>() )
        {
            auto& tag = duplicate.GetComponent<ECS::TagComponent>().Tag;
            if ( tag.find( " Copy" ) == std::string::npos )
                tag += " Copy";
        }

        const Common::UUID duplicateId = UUIDOf( duplicate );
        CommandHistory::Get().PushCommand( std::make_unique<CreateCommand>(
             std::vector<Common::UUID>{ duplicateId } ) );
        OnStructuralChange();
        return duplicateId;
    }

    std::vector<Common::UUID> DuplicateEntities( const std::vector<Common::UUID>& uuids )
    {
        std::vector<Common::UUID> duplicates;
        if ( !Ready() )
            return duplicates;

        for ( const auto& uuid : FilterTopLevel( uuids ) )
        {
            auto source = FindEntity( uuid );
            if ( !source )
                continue;
            ECS::Entity duplicate = RestoreSnapshot( CaptureSubtree( *source ), /*preserveIds=*/false );
            if ( !duplicate )
                continue;
            if ( duplicate.HasComponent<ECS::TagComponent>() )
            {
                auto& tag = duplicate.GetComponent<ECS::TagComponent>().Tag;
                if ( tag.find( " Copy" ) == std::string::npos )
                    tag += " Copy";
            }
            duplicates.push_back( UUIDOf( duplicate ) );
        }
        if ( duplicates.empty() )
            return duplicates;

        CommandHistory::Get().PushCommand( std::make_unique<CreateCommand>( duplicates ) );
        OnStructuralChange();
        return duplicates;
    }

    void Reparent( const Common::UUID& child, const Common::UUID& newParent )
    {
        if ( !Ready() )
            return;
        if ( auto cmd = DoReparent( child, newParent ) )
            CommandHistory::Get().PushCommand( std::move( cmd ) );
    }

    void ReparentMany( const std::vector<Common::UUID>& children, const Common::UUID& newParent )
    {
        if ( !Ready() )
            return;

        auto composite = std::make_unique<CompositeCommand>();
        for ( const auto& child : FilterTopLevel( children ) )
        {
            if ( child == newParent )
                continue;
            if ( auto cmd = DoReparent( child, newParent ) )
                composite->Add( std::move( cmd ) );
        }
        if ( composite->Empty() )
            return;
        if ( composite->Size() == 1 )
            CommandHistory::Get().PushCommand( composite->TakeSingle() );
        else
            CommandHistory::Get().PushCommand( std::move( composite ) );
    }

    void Rename( const Common::UUID& uuid, const std::string& newName )
    {
        if ( !Ready() || newName.empty() )
            return;
        auto e = FindEntity( uuid );
        if ( !e || !e->HasComponent<ECS::TagComponent>() )
            return;

        auto& tag = e->GetComponent<ECS::TagComponent>().Tag;
        if ( tag == newName )
            return;

        CommandHistory::Get().PushCommand( std::make_unique<RenameCommand>( uuid, tag, newName ) );
        tag = newName;
    }

    void RecordTransformEdit( const Common::UUID& uuid, const glm::vec3& oldTranslation,
                              const glm::vec3& oldRotation, const glm::vec3& oldScale )
    {
        if ( !Ready() )
            return;
        auto e = FindEntity( uuid );
        if ( !e || !e->HasComponent<ECS::TransformComponent>() )
            return;

        const auto& tc      = e->GetComponent<ECS::TransformComponent>();
        const float epsilon = 1e-6f;
        if ( glm::all( glm::epsilonEqual( tc.Translation, oldTranslation, epsilon ) ) &&
             glm::all( glm::epsilonEqual( tc.Rotation, oldRotation, epsilon ) ) &&
             glm::all( glm::epsilonEqual( tc.Scale, oldScale, epsilon ) ) )
            return; // click without an actual drag

        CommandHistory::Get().PushCommand( std::make_unique<TransformCommand>(
             uuid, oldTranslation, oldRotation, oldScale, tc.Translation, tc.Rotation, tc.Scale ) );
    }

    void RecordTransformEdits( const std::vector<TransformSnapshot>& before )
    {
        if ( !Ready() )
            return;

        const float epsilon   = 1e-6f;
        auto        composite = std::make_unique<CompositeCommand>();
        for ( const auto& snap : before )
        {
            auto e = FindEntity( snap.Entity );
            if ( !e || !e->HasComponent<ECS::TransformComponent>() )
                continue;
            const auto& tc = e->GetComponent<ECS::TransformComponent>();
            if ( glm::all( glm::epsilonEqual( tc.Translation, snap.Translation, epsilon ) ) &&
                 glm::all( glm::epsilonEqual( tc.Rotation, snap.Rotation, epsilon ) ) &&
                 glm::all( glm::epsilonEqual( tc.Scale, snap.Scale, epsilon ) ) )
                continue;
            composite->Add( std::make_unique<TransformCommand>( snap.Entity, snap.Translation,
                                                                snap.Rotation, snap.Scale, tc.Translation,
                                                                tc.Rotation, tc.Scale ) );
        }
        if ( composite->Empty() )
            return;
        if ( composite->Size() == 1 )
            CommandHistory::Get().PushCommand( composite->TakeSingle() );
        else
            CommandHistory::Get().PushCommand( std::move( composite ) );
    }

    void CopySelectionToClipboard( const std::vector<Common::UUID>& uuids )
    {
        if ( !Ready() )
            return;

        std::vector<std::vector<Assets::EntityData>> snapshots;
        for ( const auto& uuid : FilterTopLevel( uuids ) )
            if ( auto e = FindEntity( uuid ) )
                snapshots.push_back( CaptureSubtree( *e ) );

        if ( !snapshots.empty() )
            s_Clipboard = std::move( snapshots );
    }

    bool ClipboardHasContent()
    {
        return !s_Clipboard.empty();
    }

    std::vector<Common::UUID> PasteClipboard()
    {
        std::vector<Common::UUID> pasted;
        if ( !Ready() || s_Clipboard.empty() )
            return pasted;

        for ( const auto& snapshot : s_Clipboard )
            if ( ECS::Entity root = RestoreSnapshot( snapshot, /*preserveIds=*/false ) )
                pasted.push_back( UUIDOf( root ) );

        if ( pasted.empty() )
            return pasted;

        CommandHistory::Get().PushCommand( std::make_unique<CreateCommand>( pasted ) );
        OnStructuralChange();
        return pasted;
    }

    bool ApplyPrefabInstance( const Common::UUID& uuid )
    {
        if ( !Ready() )
            return false;
        auto root = FindEntity( uuid );
        if ( !root || !root->HasComponent<ECS::PrefabComponent>() )
            return false;

        auto asset = s_AssetManager->FindByHandle<Assets::PrefabAsset>(
             root->GetComponent<ECS::PrefabComponent>().Prefab );
        if ( !asset )
        {
            LOG_ERROR( "[Prefab] Apply failed: source asset not found for '{}'",
                       root->GetComponent<ECS::TagComponent>().Tag );
            return false;
        }

        asset->CreateFromEntity( *root, *s_AssetManager );
        // ofstream silently writes NOTHING when the directory is missing — make sure it exists.
        std::error_code ec;
        std::filesystem::create_directories( asset->GetMetadata().Filepath.parent_path(), ec );
        Common::Utils::FileSystem::WriteContentToFile( asset->GetMetadata().Filepath,
                                                       asset->Serialize() );
        LOG_INFO( "[Prefab] Applied instance changes -> {}", asset->GetMetadata().Filepath.string() );
        return true;
    }

    Common::UUID RevertPrefabInstance( const Common::UUID& uuid )
    {
        if ( !Ready() )
            return Common::UUID::Null();
        auto root = FindEntity( uuid );
        if ( !root || !root->HasComponent<ECS::PrefabComponent>() )
            return Common::UUID::Null();

        auto asset = s_AssetManager->FindByHandle<Assets::PrefabAsset>(
             root->GetComponent<ECS::PrefabComponent>().Prefab );
        if ( !asset )
        {
            LOG_ERROR( "[Prefab] Revert failed: source asset not found for '{}'",
                       root->GetComponent<ECS::TagComponent>().Tag );
            return Common::UUID::Null();
        }
        if ( !asset->IsReadyForUse() && !asset->Load() )
        {
            LOG_ERROR( "[Prefab] Revert failed: could not load {}", asset->GetMetadata().Filepath.string() );
            return Common::UUID::Null();
        }

        // Keep the instance's place in the world: same translation, same parent.
        glm::vec3 translation( 0.0f );
        if ( root->HasComponent<ECS::TransformComponent>() )
            translation = root->GetComponent<ECS::TransformComponent>().Translation;
        const Common::UUID parentId = ParentUUIDOf( *root );

        auto composite = std::make_unique<CompositeCommand>();

        // 1) Delete the modified instance (undo restores it).
        auto snapshot = CaptureSubtree( *root );
        DestroyByUUID( uuid );
        composite->Add( std::make_unique<DeleteCommand>( std::move( snapshot ) ) );

        // 2) Fresh instantiation from the source file.
        ECS::Entity fresh = asset->Instantiate( s_Scene, *s_AssetManager, &translation );
        if ( !fresh )
        {
            // Roll the delete back and report failure — better a live (modified) instance than nothing.
            composite->Undo();
            LOG_ERROR( "[Prefab] Revert failed to instantiate {}", asset->GetMetadata().Filepath.string() );
            return Common::UUID::Null();
        }
        if ( !parentId.IsNull() )
            if ( auto parent = FindEntity( parentId ) )
                s_Scene->Attach( *parent, fresh );

        const Common::UUID freshId = UUIDOf( fresh );
        composite->Add( std::make_unique<CreateCommand>( std::vector<Common::UUID>{ freshId } ) );

        CommandHistory::Get().PushCommand( std::move( composite ) );
        OnStructuralChange();
        Core::SelectionManager::SetSelected( freshId );
        return freshId;
    }

    void MutateEntityUndoable( const Common::UUID& uuid, const std::function<void()>& mutate )
    {
        if ( !Ready() || !mutate )
            return;
        auto e = FindEntity( uuid );
        if ( !e )
        {
            if ( mutate )
                mutate(); // still perform the action; just not undoable without a live entity
            return;
        }

        auto before = CaptureSubtree( *e );
        mutate();
        auto after = CaptureSubtree( *e );

        CommandHistory::Get().PushCommand(
             std::make_unique<EntityStateCommand>( std::move( before ), std::move( after ) ) );
        OnStructuralChange(); // the mutation itself may have relocated component pools
    }
} // namespace Desert::Editor::Commands
