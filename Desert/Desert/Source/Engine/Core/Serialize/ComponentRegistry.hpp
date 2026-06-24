#pragma once

#include <Engine/ECS/Entity.hpp>

#include <rflcpp/rfl/Generic.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Core::Serialize
{
    // One registered component (de)serializer. The unifying mechanism behind entity serialization:
    // every serializable component contributes one of these instead of a hand-written branch in
    // EntitySerializer. Reflected components (light/camera data blocks) get an auto-generated handler
    // driven by the reflection registry; asset-bearing components (mesh/skybox) provide a custom handler
    // because they map asset handles <-> file paths, which reflection cannot know about.
    struct ComponentSerializer
    {
        std::string Key; // JSON key under the entity's "Components" object

        std::function<bool( ECS::Entity )>                                                   Has;
        std::function<rfl::Generic( ECS::Entity, const Assets::AssetManager& )>               Serialize;
        std::function<void( ECS::Entity, const rfl::Generic&, const Assets::AssetManager& )>  Deserialize;
    };

    // Process-wide table of component serializers, built once. Adding a new serializable component means
    // registering it here (see ComponentRegistry.cpp) — not editing EntitySerializer.
    class ComponentRegistry
    {
    public:
        static const ComponentRegistry& Get();

        const std::vector<ComponentSerializer>& All() const
        {
            return m_Serializers;
        }

    private:
        ComponentRegistry();

        void Register( ComponentSerializer serializer );
        void RegisterBuiltins();

        std::vector<ComponentSerializer> m_Serializers;
    };
} // namespace Desert::Core::Serialize
