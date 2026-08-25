#include "PrefabAsset.hpp"
#include <Common/Utilities/FileSystem.hpp>
#include <rflcpp/rfl/json.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>
#include <Engine/ECS/Components.hpp>
#include <functional>
#include <Engine/Core/Scene.hpp>
#include <unordered_map>

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

    ECS::Entity PrefabAsset::Instantiate( Core::Scene* scene, const AssetManager& assetManager, const glm::vec3* position ) const
    {
        if ( m_EntityData.empty() )
            return {};

        std::unordered_map<Common::UUID, ECS::Entity> entityMap;
        ECS::Entity rootEntity;

        // The entity each record became, aligned with m_EntityData — see PrefabFactory::Instantiate for why
        // the second pass must not re-derive this from the record.
        std::vector<ECS::Entity> created;
        created.reserve( m_EntityData.size() );

        for ( const auto& entityData : m_EntityData )
        {
            ECS::Entity newEntity =
                 scene->CreateEntityWithUUID( Common::UUID::Generate(), entityData.Tag.value_or( "Entity" ) );
            created.push_back( newEntity );

            if ( entityData.id.has_value() && !entityData.id->IsNull() )
                entityMap.insert( { *entityData.id, newEntity } );
        }

        for ( size_t i = 0; i < m_EntityData.size(); ++i )
        {
            const auto& entityData = m_EntityData[i];
            ECS::Entity newEntity  = created[i];

            Core::Serialize::EntitySerializer::DeserializeEntity( entityData, newEntity, assetManager );

            if ( entityData.parent.has_value() && !entityData.parent->IsNull() )
            {
                auto parentIt = entityMap.find( *entityData.parent );
                if ( parentIt != entityMap.end() )
                {
                    scene->Attach( parentIt->second, newEntity );
                }
            }
            else
            {
                if ( !rootEntity )
                {
                    rootEntity = newEntity;
                }
            }
        }

        if ( position && rootEntity )
        {
            if ( rootEntity.HasComponent<ECS::TransformComponent>() )
            {
                auto& tc = rootEntity.GetComponent<ECS::TransformComponent>();
                tc.Translation = *position;
            }
            else
            {
                rootEntity.AddComponent<ECS::TransformComponent>().Translation = *position;
            }
        }

        // Ensure the root entity is tagged as a prefab instance
        if ( rootEntity && m_Metadata.Handle )
        {
            if ( !rootEntity.HasComponent<ECS::PrefabComponent>() )
                rootEntity.AddComponent<ECS::PrefabComponent>().Prefab = m_Metadata.Handle;
        }

        return rootEntity;
    }

} // namespace Desert::Assets