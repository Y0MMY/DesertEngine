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
        if ( !m_EntityData.empty() )
        {
            data.Root = m_EntityData.front().id.value_or( Common::UUID{} );
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
        
        m_IsLoaded = true;
    }

    ECS::Entity PrefabAsset::Instantiate( Core::Scene* scene, const AssetManager& assetManager, const glm::vec3* position ) const
    {
        if ( m_EntityData.empty() )
            return {};

        std::unordered_map<Common::UUID, ECS::Entity> entityMap;
        ECS::Entity rootEntity;

        for ( const auto& entityData : m_EntityData )
        {
            Common::UUID newUUID;
            Common::UUID originalID = entityData.id.value_or( Common::UUID{} );
            ECS::Entity newEntity = scene->CreateEntityWithUUID( newUUID, entityData.Tag.value_or( "Entity" ) );
            entityMap.insert( { originalID, newEntity } );
        }

        for ( const auto& entityData : m_EntityData )
        {
            Common::UUID originalID = entityData.id.value_or( Common::UUID{} );
            auto it = entityMap.find( originalID );
            if ( it == entityMap.end() ) continue;

            ECS::Entity newEntity = it->second;
            Core::Serialize::EntitySerializer::DeserializeEntity( entityData, newEntity, assetManager );

            if ( entityData.parent.has_value() && *entityData.parent != Common::UUID{} )
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

        return rootEntity;
    }

} // namespace Desert::Assets