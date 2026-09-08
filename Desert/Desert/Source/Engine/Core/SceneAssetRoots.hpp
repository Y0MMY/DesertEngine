#pragma once

#include <Engine/Assets/AssetRootSet.hpp>

namespace Desert::Core
{
    class Scene;

    /**
     * @brief WHAT A LIVE WORLD STILL NEEDS — the root set eviction traces from.
     *
     * WHY IT IS HERE AND NOT IN Engine/Assets. The roots are ECS components and scene settings, and
     * `Engine/ECS/Components.hpp` includes the asset layer — so the asset layer cannot include it back.
     * The layering already decided where this function lives; the evictor takes a plain
     * `Assets::AssetRootSet` and knows nothing about entities.
     *
     * EVERY LIVE SCENE COUNTS, not "the current one". A running editor holds several at once and every one
     * of them is drawing: extra scene views, the Details panel's preview, the Material Editor's preview
     * ball, the thumbnail renderer's offscreen world. Evicting an asset because the MAIN scene stopped
     * using it, while a preview is still drawing it, is the defect this rule exists to prevent — and it is
     * exactly the shape that a sweep run "on scene change" would produce if it looked only at the scene
     * that changed. `Scene` therefore keeps a process-wide list of the live ones (Scene::LiveScenes) and
     * `CollectAssetRoots` walks all of them.
     *
     * THE COMPONENT WALK IS HELD BY A CENSUS. `Desert/Tests/Engine/AssetRoots` reads Components.hpp,
     * finds every member whose type is `Assets::AssetHandle` or a container of them, and asserts that this
     * file names each one. That is not belt-and-braces: a component type added next month and forgotten
     * here produces "my mesh disappears when I open another level", with nothing in any log, and it is the
     * "a middle link drops a property" shape this project has paid for seven times in one day.
     */

    /// Everything @p scene's entities and settings name, with a reason per handle.
    void CollectAssetRoots( Scene& scene, Assets::AssetRootSet& roots );

    /// The union over every live scene. This is what the eviction trigger uses.
    [[nodiscard]] Assets::AssetRootSet CollectAssetRootsFromLiveScenes();

} // namespace Desert::Core
