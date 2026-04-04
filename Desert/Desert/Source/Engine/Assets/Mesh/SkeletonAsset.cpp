#include "SkeletonAsset.hpp"
#include <Engine/Assets/Serialization/Skeleton.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    SkeletonAsset::SkeletonAsset( const AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, GetTypeID() )
    {
    }

    Common::BoolResultStr SkeletonAsset::Load()
    {
        auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        const auto dataReflected = rfl::json::read<Serialization::SkeletonAssetData>( raw );
        if ( !dataReflected.has_value() )
        {
            return Common::MakeError( dataReflected.error().what() );
        }

        auto data = dataReflected.value();

        m_Skeleton = std::make_unique<Animation::Skeleton>( std::move( data.Bones ) );

        return BOOLSUCCESS;
    }

    Common::BoolResultStr SkeletonAsset::Unload()
    {
        return BOOLSUCCESS
    }
} // namespace Desert::Assets