#pragma once

#include <Engine/ECS/Entity.hpp>
#include <Engine/Reflection/ReflectionSerializer.hpp>

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

    // The one place an AssetResolver is built (the invariant is stated on AssetResolver itself). Exposed
    // because SceneSettings is reflected like a component but is NOT one, so it is serialized straight
    // from SceneSerializer — and until this was exposed that call had no resolver, which meant the
    // scene's own `SplashSprite` was the last field in the engine still written as a raw 64-bit number.
    Reflection::AssetResolver MakeAssetResolver( const Assets::AssetManager& mgr );

    // Standalone (de)serialization of a single MaterialComponent to/from a JSON string — the generic
    // ".demat" material file (reusable, data-driven). Reuses the same Ser mirror + asset resolver as the
    // scene/entity serializers (texture refs round-trip as paths). MVP for a reusable generic material;
    // full asset-system integration (handle/DnD/live-link) is a later milestone.
    std::string SaveMaterialComponentToJson( const ECS::MaterialComponent& mc, const Assets::AssetManager& mgr );
    bool        LoadMaterialComponentFromJson( const std::string& json, ECS::MaterialComponent& mc,
                                               const Assets::AssetManager& mgr );
} // namespace Desert::Core::Serialize
