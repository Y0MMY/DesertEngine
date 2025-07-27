#pragma once

#include <Common/Core/Result.hpp>
#include <Common/Core/Core.hpp>
#include <Common/Core/UUID.hpp>

#include "Common.hpp"
#include "AssetMetadata.hpp"

namespace Desert::Assets
{
    class MeshAsset;

    class AssetBase
    {
    public:
        virtual ~AssetBase() = default;

        virtual const AssetMetadata& GetMetadata() const final
        {
            return m_Metadata;
        }

        virtual Common::BoolResult Load()   = 0;
        virtual Common::BoolResult Unload() = 0;

        virtual bool IsReadyForUse() const = 0;

        explicit AssetBase( const AssetPriority priority, const Common::Filepath& filepath, AssetTypeID assetType )
             : m_Metadata{ Common::UUID(), filepath, priority, assetType }
        {
        }
    protected:
        AssetMetadata m_Metadata;
    };

} // namespace Desert::Assets