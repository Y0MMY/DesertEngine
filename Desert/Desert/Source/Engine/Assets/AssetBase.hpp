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

        virtual void ResolveDependencies( AssetManager& manager )
        {
        }

        virtual Common::BoolResultStr Load()   = 0;
        virtual Common::BoolResultStr Unload() = 0;

        virtual bool IsReadyForUse() const = 0;

        // EVERY asset's identity is derived from its path, here, once.
        //
        // This used to be `Common::UUID()`, which minted a fresh random id per construction, so an asset's
        // handle was a property of the LAUNCH rather than of the file. Five types lived with that — prefab,
        // skybox, shader, skeleton, animation — while mesh, texture, material and the three cloud types had
        // each grown their own copy of the path-derived line to escape it. Nine copies of one rule is how
        // the tenth type gets forgotten, and the symptom when it is forgotten is a reference that resolves
        // to nothing after a restart with nothing logged.
        //
        // Subclasses whose FILE carries an id of its own (a cooked `.tex` header, a `.demat`'s MaterialId)
        // still overwrite this in Load: an id stored in the file additionally survives a rename, which a
        // path-derived one cannot. That is a strictly better identity for the same asset, not a second way
        // of doing the same thing.
        explicit AssetBase( const AssetPriority priority, const Common::Filepath& filepath, AssetTypeID assetType )
             : m_Metadata{ Common::AssetHandle::FromCookedPath( filepath ), filepath, priority, assetType }
        {
        }

    protected:
        AssetMetadata m_Metadata;
    };

} // namespace Desert::Assets