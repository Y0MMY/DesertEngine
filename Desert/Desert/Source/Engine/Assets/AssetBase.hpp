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

        // LOADING AND RESOLVING ARE ONE STEP, and this is the only entry point that says so.
        //
        // A dependency named by a field INSIDE the file cannot be resolved before the file is parsed.
        // AssetManager::CreateAsset resolves at construction, which is correct for an eagerly-loaded asset
        // and MEANINGLESS for a lazy shell: the field the lookup keys on is still zero, so the resolve runs,
        // finds nothing, and is never repeated. That is exactly how every skinned mesh in a scene became
        // invisible — SkinnedMeshAsset's skeleton is matched by a signature stored in the .skmesh, the shell
        // carried 0, and the only code that ever asked a second time was the editor's drag-and-drop.
        //
        // Callers therefore never write `Load()` and `ResolveDependencies()` as two statements: the second
        // one is what gets forgotten, and nothing fails loudly when it is. `Load()` stays public for the
        // reload paths, which re-resolve deliberately because the EDIT may have been the dependency.
        NO_DISCARD Common::BoolResultStr EnsureLoaded( AssetManager& manager )
        {
            if ( IsReadyForUse() )
            {
                return BOOLSUCCESS;
            }

            if ( const auto loaded = Load(); !loaded )
            {
                return loaded;
            }

            ResolveDependencies( manager );
            return BOOLSUCCESS;
        }

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