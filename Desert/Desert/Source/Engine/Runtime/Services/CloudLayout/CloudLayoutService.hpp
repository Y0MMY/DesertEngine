#pragma once

#include <Engine/Assets/CloudLayoutAsset.hpp>

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Desert::Runtime
{
    /**
     * @brief The painted cloud layouts a scene can point at, resolved once for every viewport.
     *
     * The same shape as CloudTypeService next door and it exists for the same two reasons: the renderer
     * must not know how to read a file, and an editor with three viewports must not resolve the same asset
     * three times. Like that service it owns NO GPU resource, and here that is not a detail but the point —
     * a layout never reaches a sampler at all. It is consumed on the CPU by the placement bake, which is
     * what makes a painting cost the march nothing.
     *
     * THE EMPTY SLOT IS THE DEFAULT AND IT IS NOT AN ERROR. A layer with no layout bound places its clouds
     * exactly as it did before this asset existed: the procedural patch field decides which parts of the
     * sky are busy, and Get() answers with a null pointer that the bake reads as "there is no painting".
     * That is the phase's whole acceptance criterion — an empty slot must render the frame it rendered
     * before — so the absence has to be expressible rather than approximated.
     *
     * A handle that names a layout nobody registered IS an error and is logged as one, once, with the
     * handle in the message. Falling through to "no painting" silently would render a sky that is merely
     * not the one the artist painted, which is the least diagnosable thing this subsystem can do.
     */
    class CloudLayoutService
    {
    public:
        /// Caches @p asset under its handle. Called again for the same asset after a hot reload; a changed
        /// content hash replaces the entry, an unchanged one is a no-op.
        Common::BoolResultStr Register( const std::shared_ptr<Assets::CloudLayoutAsset>& asset );

        /**
         * @brief The painting a layer's slot resolves to, or NULL when there is none.
         *
         * A POINTER AND NOT A REFERENCE, unlike CloudTypeService::GetShape, and the difference is the
         * difference between the two slots. A cloud TYPE has a built-in default because a sky must exist;
         * a LAYOUT has no default because the absence of a painting is itself a meaningful, shipped state —
         * the one every scene in the repository is in. Returning a reference would have forced an empty
         * CloudLayoutData to stand for "none", and a table of zeros is not nothing: under the zero-mean
         * pattern rule it would push every cell's coverage the same way.
         *
         * SHARED AND NOT BORROWED, through the aliasing constructor: the pointer owns a share of the ASSET
         * and addresses its layout member. The bake's parameters are CACHED by the renderer across frames
         * and re-used whenever the region shifts, so a borrowed pointer would outlive an asset the user
         * unloaded — and the failure would be a read of freed pixels inside a bake, which is the least
         * diagnosable crash this subsystem could have.
         */
        std::shared_ptr<const Assets::CloudLayoutData> Get( const Assets::AssetHandle& handle );

        void Clear();

    private:
        struct Entry
        {
            std::shared_ptr<Assets::CloudLayoutAsset> Asset;
            uint32_t                                  ContentHash = 0;
        };

        std::unordered_map<Assets::AssetHandle, Entry> m_Layouts;
        // Handles already complained about. A missing layout is a permanent state of the scene, so without
        // this the error would be logged every frame of every viewport and bury everything else.
        std::unordered_set<Assets::AssetHandle> m_Reported;
    };
} // namespace Desert::Runtime
