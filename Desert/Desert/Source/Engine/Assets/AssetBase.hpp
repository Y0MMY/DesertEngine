#pragma once

#include <Common/Core/ResultStr.hpp>
#include <Common/Core/Core.hpp>
#include <Common/Core/UUID.hpp>

#include "Common.hpp"
#include "AssetMetadata.hpp"

namespace Desert::Assets
{
    class MeshAsset;
    class AssetManager;

    class AssetBase
    {
    public:
        virtual ~AssetBase() = default;

        virtual const AssetMetadata& GetMetadata() const final
        {
            return m_Metadata;
        }

        virtual void ResolveDependencies(AssetManager& manager) {}

        virtual Common::BoolResultStr Load()   = 0;
        virtual Common::BoolResultStr Unload() = 0;

        virtual bool IsReadyForUse() const = 0;

        explicit AssetBase( const AssetPriority priority, const Common::Filepath& filepath, AssetTypeID assetType )
             : m_Metadata{ Common::UUID(), filepath, priority, assetType }
        {
        }
    protected:
        AssetMetadata m_Metadata;
    };

} // namespace Desert::Assets