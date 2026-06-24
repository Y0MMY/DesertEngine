#pragma once

#include <Engine/Reflection/ReflectionTypes.hpp>

#include <rflcpp/rfl/Generic.hpp>

namespace Desert::Reflection
{
    // Generic, reflection-driven (de)serialization. Walks a TypeInfo's fields and reads/writes the raw
    // object bytes at each field offset into an rfl::Generic tree. This is the single code path that makes
    // "everything reflected serializes the same way" true: materials, light/camera data blocks, and any
    // future reflected struct all round-trip through here without a hand-written mirror struct.
    //
    // When the reflection core gains enum/nested/container support (#4), this serializer picks it up for
    // free — no per-type serialization code to update.

    // Serializes a reflected object (described by `type`) into a generic JSON object.
    rfl::Generic::Object SerializeReflected( const TypeInfo& type, const void* obj );

    // Deserializes from a generic JSON object into a reflected object, in place. Fields missing from
    // `src` keep their current (default) value — this gives forward/backward field compatibility.
    void DeserializeReflected( const TypeInfo& type, void* obj, const rfl::Generic::Object& src );
} // namespace Desert::Reflection
