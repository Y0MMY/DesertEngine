#include "PrefabAsset.hpp"
#include <Common/Utilities/FileSystem.hpp>
#include <rflcpp/rfl/json.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>
#include <Engine/ECS/Components.hpp>
#include <functional>
#include <Engine/Core/Scene.hpp>
#include <Engine/Runtime/Factory/PrefabFactory.hpp>
#include <unordered_set>

namespace Desert::Assets
{
    Common::BoolResultStr PrefabAsset::Load()
    {
        auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );
        if ( raw.empty() )
        {
            return Common::MakeError<bool>( "Prefab file is empty or missing: " + m_Metadata.Filepath.string() );
        }

        const auto dataReflected = rfl::json::read<PrefabData>( raw );

        if ( !dataReflected.has_value() )
        {
            return Common::MakeError<bool>( dataReflected.error().what() );
        }

        const auto& data = dataReflected.value();

        m_EntityData = data.Entities;
        m_IsLoaded = true;

        return BOOLSUCCESS;
    }

    Common::BoolResultStr PrefabAsset::Unload()
    {
        m_EntityData.clear();
        m_IsLoaded = false;
        return BOOLSUCCESS;
    }

    std::string PrefabAsset::Serialize() const
    {
        PrefabData data;
        data.Name = m_Metadata.Filepath.stem().string();
        data.Entities = m_EntityData;
        // Root names the first record BY ID, so it can only be written when that record has one. It used to
        // fall back to a default-constructed UUID, which was a random number — a root id pointing at no
        // entity, baked into the file.
        if ( !m_EntityData.empty() && m_EntityData.front().id.has_value() )
        {
            data.Root = *m_EntityData.front().id;
        }

        return rfl::json::write( data );
    }

    void PrefabAsset::CreateFromEntity( ECS::Entity rootEntity, const AssetManager& assetManager )
    {
        if ( !rootEntity ) return;

        m_EntityData.clear();
        
        std::function<void( ECS::Entity )> traverse = [&]( ECS::Entity e )
        {
            if ( !e )
                return;

            // Serialize current entity
            m_EntityData.push_back( Core::Serialize::EntitySerializer::SerializeEntity( e, assetManager ) );

            // Traverse children
            if ( e.HasComponent<ECS::RelationshipComponent>() )
            {
                const auto& rel = e.GetComponent<ECS::RelationshipComponent>();
                for ( auto childHandle : rel.Children )
                {
                    traverse( ECS::Entity{ childHandle, *e.GetRegistry() } );
                }
            }
        };

        traverse( rootEntity );

        // Root entity's own PrefabComponent points to this very file — strip it from the
        // serialized data so that re-instantiating doesn't recurse into itself.
        if ( !m_EntityData.empty() )
            m_EntityData[0].PrefabPath = std::nullopt;

        m_IsLoaded = true;
    }

    // Placing an instance in the world. The BUILDING of the instance is not here and must not be: it is
    // PrefabFactory::Instantiate, and this used to be a second copy of it that had drifted in three ways at
    // once — it resolved duplicate ids the other way round (`insert` against the factory's `operator[]`),
    // it ignored PrefabPath entirely so a prefab nested inside a prefab silently did not appear, and it
    // guessed the root by looking for the first record with no `parent`. That guess is wrong for every
    // prefab cut from an entity that HAD a parent: the first record then carries a parent id naming an
    // entity outside the file, no record is parentless at all, and the function returned a null entity
    // while leaving its entities in the scene — the editor's "Instantiate Prefab" appeared to do nothing.
    // The factory takes the first record, which is the entity the prefab was cut from by construction.
    //
    // Everything this adds over the factory is the placement: the position argument the Lua binding and the
    // editor's drag-and-drop use.
    ECS::Entity PrefabAsset::Instantiate( Core::Scene* scene, const AssetManager& assetManager,
                                          const glm::vec3* position ) const
    {
        if ( !scene || m_EntityData.empty() )
            return {};

        std::unordered_set<Common::UUID> stack;
        ECS::Entity                      rootEntity =
             Runtime::Factory::PrefabFactory::Instantiate( *this, *scene, assetManager, stack );

        if ( position && rootEntity )
        {
            if ( rootEntity.HasComponent<ECS::TransformComponent>() )
            {
                auto& tc       = rootEntity.GetComponent<ECS::TransformComponent>();
                tc.Translation = *position;
            }
            else
            {
                rootEntity.AddComponent<ECS::TransformComponent>().Translation = *position;
            }
        }

        return rootEntity;
    }

} // namespace Desert::Assets