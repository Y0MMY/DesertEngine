#include "Internal/ScriptRuntime.hpp"

namespace Desert::Scripting
{
    // Base Entity usertype: identity, transform, messaging, attachment.
    void RegisterEntityCoreBindings( ScriptEngine::Impl& implRef )
    {
        auto& lua  = implRef.Lua;
        auto* impl = &implRef;
        (void)lua; (void)impl;

        // The generic entity API (used for `self`, World.find/spawn/raycast results, etc.).
        // BASE surface only — identity, transform, messaging, attachment. Domain modules
        // (character, materials, ...) EXTEND this usertype from their own translation units.
        lua.new_usertype<ScriptEntity>(
             "Entity",
             // identity / lifetime
             "valid", &ScriptEntity::Valid, "name", &ScriptEntity::Name, "destroy", &ScriptEntity::Destroy,
             "call", &ScriptEntity::Call, "attachTo", &ScriptEntity::AttachTo, "detach", &ScriptEntity::Detach,
             // transform
             "getPosition", &ScriptEntity::GetPosition, "setPosition", &ScriptEntity::SetPosition, "translate",
             &ScriptEntity::Translate, "getRotation", &ScriptEntity::GetRotation, "setRotation",
             &ScriptEntity::SetRotation, "getScale", &ScriptEntity::GetScale, "setScale",
             &ScriptEntity::SetScale,
             // spatial helpers
             "forward", &ScriptEntity::Forward, "right", &ScriptEntity::Right, "distanceTo",
             &ScriptEntity::DistanceTo );
    }
} // namespace Desert::Scripting
