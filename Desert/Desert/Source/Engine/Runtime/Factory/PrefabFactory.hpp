#pragma once

#include <Engine/ECS/Entity.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>

namespace Desert::Runtime::Factory
{

    class PrefabFactory
    {
    public:
        static ECS::Entity Instantiate( const Assets::PrefabAsset& prefab, Core::Scene& scene,
                                        const Assets::AssetManager&       assetManager,
                                        std::unordered_set<Common::UUID>& stack );
    };
} // namespace Desert::Runtime::Factory
