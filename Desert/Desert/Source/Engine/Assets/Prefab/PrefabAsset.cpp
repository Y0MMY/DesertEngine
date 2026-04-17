#include "PrefabAsset.hpp"

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    Common::BoolResultStr PrefabAsset::Load()
    {
      /*  auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        const auto dataReflected = rfl::json::read<PrefabData>( raw );

        if ( !dataReflected.has_value() )
        {
            return Common::MakeError( dataReflected.error().what() );
        }

        const auto& data = dataReflected.value();

        m_EntityData.clear();
        m_EntityData.reserve( data.Entities.size() );

        for ( const auto& e : data.Entities )
        {
            EntityData entity;

            entity.id        = e.id;
            entity.parent    = e.parent;
            entity.PrefabRef = e.PrefabRef;

            m_EntityData.emplace_back( std::move( entity ) );
        }

        m_IsLoaded = true;*/

        return BOOLSUCCESS;
    }

    Common::BoolResultStr PrefabAsset::Unload()
    {
        return BOOLSUCCESS;
    }

} // namespace Desert::Assets